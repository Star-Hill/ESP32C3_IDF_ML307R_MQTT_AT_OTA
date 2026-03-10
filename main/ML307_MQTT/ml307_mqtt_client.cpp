/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-07 13:35:36
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 13:46:41
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\ML307_MQTT\ml307_mqtt_client.cpp
 * @Description: ML307 MQTT 客户端模块 - 实现文件
 *
 * 分时上传策略 (由 ml307_mqtt_config.h 宏定义控制):
 *   高频时段 [MQTT_HIGH_FREQ_START_HOUR, MQTT_HIGH_FREQ_END_HOUR):
 *       间隔 = MQTT_HIGH_FREQ_INTERVAL_MS
 *   低频时段 (其余):
 *       间隔 = MQTT_LOW_FREQ_INTERVAL_MS
 *
 * RTC 时间由 ml307_sntp_time 模块维护；若 RTC 尚未就绪则默认使用高频间隔。
 */

#include "ml307_mqtt_client.h"
#include "ml307_mqtt_config.h"
#include "ml307_sntp_time.h"   // 读取 RTC 时间

#include <stdio.h>
#include <string.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"

#include "at_uart.h"
#include "at_modem.h"
#include "ml307_mqtt.h"

static const char *TAG = "MqttClient";

// ==================== 模块内全局变量 ====================

static TaskHandle_t mqtt_task_handle_ = NULL;

// ML307 硬件对象
static std::shared_ptr<AtUart>   at_uart_;
static std::unique_ptr<AtModem>  modem_;
static std::unique_ptr<Ml307Mqtt> mqtt_;

// 状态
static bool     is_connected_ = false;
static bool     is_running_   = false;
static uint32_t message_count_ = 0;

// AT+CCLK? 响应缓存 (供时间模块使用)
static std::string    cclk_response_;
static SemaphoreHandle_t cclk_semaphore_ = NULL;
static bool           cclk_received_   = false;

// ==================== 内部辅助函数 ====================

/**
 * @brief 根据当前 RTC 时间返回应使用的发布间隔 (毫秒)
 *
 * RTC 未就绪时默认高频，避免首次同步前长时间沉默。
 */
static uint32_t get_publish_interval_ms(void)
{
    ml307_time_t now;
    if (!ml307_sntp_get_rtc_time(&now)) {
        // RTC 尚未就绪，保守使用高频
        return (uint32_t)MQTT_HIGH_FREQ_INTERVAL_MS;
    }

    int h = now.hour;
    bool high_freq = (MQTT_HIGH_FREQ_START_HOUR <= MQTT_HIGH_FREQ_END_HOUR)
                   ? (h >= MQTT_HIGH_FREQ_START_HOUR && h < MQTT_HIGH_FREQ_END_HOUR)
                   : (h >= MQTT_HIGH_FREQ_START_HOUR || h < MQTT_HIGH_FREQ_END_HOUR);
    // 注: 若 START > END (如 22:00~06:00 夜间高频) 上面的三元表达式也能正确处理
    //     本项目 START=7 < END=19，走第一分支即可

    return high_freq
           ? (uint32_t)MQTT_HIGH_FREQ_INTERVAL_MS
           : (uint32_t)MQTT_LOW_FREQ_INTERVAL_MS;
}

// ==================== 初始化步骤 ====================

static bool init_modem(void)
{
    ESP_LOGI(TAG, "初始化 ML307 模组...");

    modem_ = AtModem::Detect(ML307_UART_TX_PIN, ML307_UART_RX_PIN, ML307_UART_DTR_PIN, 921600);
    if (modem_ == nullptr) {
        ESP_LOGE(TAG, "未检测到 ML307 模组");
        return false;
    }

    ESP_LOGI(TAG, "✓ ML307 模组检测成功");
    at_uart_ = modem_->GetAtUart();

    std::string revision = modem_->GetModuleRevision();
    std::string imei     = modem_->GetImei();
    std::string iccid    = modem_->GetIccid();

    ESP_LOGI(TAG, "  版本 : %s", revision.c_str());
    ESP_LOGI(TAG, "  IMEI : %s", imei.c_str());
    ESP_LOGI(TAG, "  ICCID: %s", iccid.c_str());

    return true;
}

static bool wait_for_network(void)
{
    ESP_LOGI(TAG, "等待网络注册...");

    modem_->OnNetworkStateChanged([](bool network_ready) {
        if (network_ready) {
            ESP_LOGI(TAG, "✓ 网络已就绪");
        } else {
            ESP_LOGW(TAG, "⚠ 网络断开");
            is_connected_ = false;
        }
    });

    const int max_retries = 30;
    for (int i = 0; i < max_retries && is_running_; i++) {
        auto result = modem_->WaitForNetworkReady();
        if (result == NetworkStatus::Ready) {
            ESP_LOGI(TAG, "✓ 网络注册成功");
            ESP_LOGI(TAG, "  运营商   : %s", modem_->GetCarrierName().c_str());
            ESP_LOGI(TAG, "  信号强度 : %d/31", modem_->GetCsq());
            return true;
        } else if (result == NetworkStatus::ErrorInsertPin) {
            ESP_LOGE(TAG, "SIM卡需要PIN码");
            return false;
        } else if (result == NetworkStatus::ErrorRegistrationDenied) {
            ESP_LOGE(TAG, "网络注册被拒绝");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "网络注册超时");
    return false;
}

static bool connect_mqtt(void)
{
    ESP_LOGI(TAG, "连接 MQTT 服务器...");

    mqtt_ = std::make_unique<Ml307Mqtt>(at_uart_, MQTT_CONNECTION_ID);

    mqtt_->OnConnected([]() {
        ESP_LOGI(TAG, "✅ MQTT 已连接");
        is_connected_ = true;
    });

    mqtt_->OnDisconnected([]() {
        ESP_LOGW(TAG, "⚠️ MQTT 已断开");
        is_connected_ = false;
    });

    mqtt_->OnError([](const std::string &error) {
        ESP_LOGE(TAG, "MQTT 错误: %s", error.c_str());
    });

    mqtt_->OnMessage([](const std::string &topic, const std::string &payload) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📩 ========== 收到 MQTT 消息 ==========");
        ESP_LOGI(TAG, "   主题: %s", topic.c_str());
        ESP_LOGI(TAG, "   内容: %s", payload.c_str());
        ESP_LOGI(TAG, "   长度: %d 字节", (int)payload.length());
        ESP_LOGI(TAG, "=====================================");
        ESP_LOGI(TAG, "");
        mqtt_on_message(topic.c_str(), payload.c_str(), payload.length());
    });

    ESP_LOGI(TAG, "  服务器   : %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    ESP_LOGI(TAG, "  客户端ID : %s",    MQTT_CLIENT_ID);

    bool connected = mqtt_->Connect(
        MQTT_BROKER_HOST, MQTT_BROKER_PORT,
        MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);

    if (!connected) {
        ESP_LOGE(TAG, "MQTT 连接失败 (错误码: %d)", mqtt_->GetLastError());
        return false;
    }

    ESP_LOGI(TAG, "✅ MQTT 连接成功");
    is_connected_ = true;
    return true;
}

static bool subscribe_topic(void)
{
    ESP_LOGI(TAG, "订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);
    if (!mqtt_->Subscribe(MQTT_SUBSCRIBE_TOPIC, 0)) {
        ESP_LOGE(TAG, "订阅失败");
        return false;
    }
    ESP_LOGI(TAG, "✅ 订阅成功");
    return true;
}

// ==================== MQTT 主任务 ====================

static void mqtt_client_task(void *pvParameters)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 客户端任务启动");
    ESP_LOGI(TAG, "========================================");

    if (!init_modem())       { ESP_LOGE(TAG, "模组初始化失败"); goto task_exit; }
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!wait_for_network()) { ESP_LOGE(TAG, "网络连接失败");   goto task_exit; }
    vTaskDelay(pdMS_TO_TICKS(2000));

    if (!connect_mqtt())     { ESP_LOGE(TAG, "MQTT 连接失败");  goto task_exit; }
    vTaskDelay(pdMS_TO_TICKS(1000));

    if (!subscribe_topic())  { ESP_LOGW(TAG, "订阅失败，继续运行"); }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  初始化完成，开始分时调度上传");
    ESP_LOGI(TAG, "  高频时段 : %02d:00 ~ %02d:00 (间隔 %lu ms)",
             MQTT_HIGH_FREQ_START_HOUR, MQTT_HIGH_FREQ_END_HOUR,
             MQTT_HIGH_FREQ_INTERVAL_MS);
    ESP_LOGI(TAG, "  低频时段 : 其余      (间隔 %lu ms)",
             MQTT_LOW_FREQ_INTERVAL_MS);
    ESP_LOGI(TAG, "  发布主题 : %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "  订阅主题 : %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 3. 启动时间同步任务 (一行代码搞定!)
    ml307_sntp_time_start();

    {
        char message_buffer[MQTT_MAX_MESSAGE_SIZE];

        while (is_running_)
        {
            // ── 计算本次应使用的间隔 ────────────────────────
            uint32_t interval_ms = get_publish_interval_ms();

            if (is_connected_)
            {
                message_count_++;

                int msg_len = mqtt_build_message(message_buffer,
                                                 sizeof(message_buffer),
                                                 message_count_);
                if (msg_len > 0) {
                    if (mqtt_->Publish(MQTT_PUBLISH_TOPIC,
                                       std::string(message_buffer, msg_len), 0)) {
                        // 打印发送日志，同时附上当前时段信息
                        ml307_time_t now;
                        if (ml307_sntp_get_rtc_time(&now)) {
                            bool high = (now.hour >= MQTT_HIGH_FREQ_START_HOUR &&
                                         now.hour <  MQTT_HIGH_FREQ_END_HOUR);
                            ESP_LOGI(TAG, "📤 [%lu] %s  (%s %lu s)",
                                     message_count_, message_buffer,
                                     high ? "高频" : "低频",
                                     interval_ms / 1000);
                        } else {
                            ESP_LOGI(TAG, "📤 [%lu] %s", message_count_, message_buffer);
                        }
                        xl9555_ir_counter_reset(0xFF); // 重置全部通道计数
                    } else {
                        ESP_LOGE(TAG, "❌ [%lu] 发送失败", message_count_);
                    }
                } else {
                    ESP_LOGD(TAG, "跳过发送 (mqtt_build_message 返回 0)");
                }
            }
            else
            {
                ESP_LOGW(TAG, "MQTT 未连接，尝试重连...");
                if (mqtt_->Connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT,
                                   MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
                    ESP_LOGI(TAG, "✅ 重连成功");
                    is_connected_ = true;
                    mqtt_->Subscribe(MQTT_SUBSCRIBE_TOPIC, 0);
                } else {
                    ESP_LOGE(TAG, "❌ 重连失败，等待 5 秒后重试");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    continue;
                }
            }

            // ── 等待下次发送 ─────────────────────────────────
            // 将长间隔切成 1 秒的小片，以便 is_running_ = false 后能快速退出
            uint32_t elapsed = 0;
            while (is_running_ && elapsed < interval_ms) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                elapsed += 1000;
            }
        }
    }

task_exit:
    ESP_LOGI(TAG, "MQTT 客户端任务退出");
    mqtt_task_handle_ = NULL;
    vTaskDelete(NULL);
}

// ==================== 公共 API 实现 ====================

bool mqtt_client_start(void)
{
    if (mqtt_task_handle_ != NULL) {
        ESP_LOGW(TAG, "MQTT 客户端任务已经在运行");
        return false;
    }

    is_running_    = true;
    is_connected_  = false;
    message_count_ = 0;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 客户端配置");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "服务器   : %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    ESP_LOGI(TAG, "客户端ID : %s",    MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "发布主题 : %s",    MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "订阅主题 : %s",    MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "高频时段 : %02d:00~%02d:00 / %lu ms",
             MQTT_HIGH_FREQ_START_HOUR, MQTT_HIGH_FREQ_END_HOUR,
             MQTT_HIGH_FREQ_INTERVAL_MS);
    ESP_LOGI(TAG, "低频间隔 : %lu ms", MQTT_LOW_FREQ_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    BaseType_t ret = xTaskCreate(
        mqtt_client_task,
        "mqtt_client",
        MQTT_TASK_STACK_SIZE,
        NULL,
        MQTT_TASK_PRIORITY,
        &mqtt_task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建任务失败");
        is_running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "✅ MQTT 客户端任务已启动");
    return true;
}

void mqtt_client_stop(void)
{
    if (mqtt_task_handle_ == NULL) {
        ESP_LOGW(TAG, "MQTT 客户端任务未运行");
        return;
    }

    ESP_LOGI(TAG, "正在停止 MQTT 客户端任务...");
    is_running_ = false;
    while (mqtt_task_handle_ != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "MQTT 客户端任务已停止");
}

bool mqtt_client_publish(const char *message)
{
    if (!is_connected_ || mqtt_ == nullptr || message == NULL) {
        ESP_LOGW(TAG, "MQTT 未连接或消息为空");
        return false;
    }
    return mqtt_->Publish(MQTT_PUBLISH_TOPIC, message, 0);
}

bool mqtt_client_is_connected(void)
{
    return is_connected_;
}

uint32_t mqtt_client_get_message_count(void)
{
    return message_count_;
}

// ==================== 用户回调实现 ====================
// ==================== 用户回调实现 ====================

int mqtt_build_message(char *buffer, size_t buffer_size, uint32_t message_count)
{
    // 温湿度
    float temp  = dht11_get_temperature();
    float humid = dht11_get_humidity();

    // 16路红外计数
    uint32_t ch[XL9555_IR_CHANNEL_COUNT] = {0};
    xl9555_ir_counter_get_all(ch);

    uint32_t total_count = 0;
    for (int i = 0; i < XL9555_IR_CHANNEL_COUNT; i++) {
        total_count += ch[i];
    }

    return snprintf(buffer, buffer_size,
        "{"
            "\"method\":\"report\","
            "\"clientToken\":%lu,"
            "\"params\":{"
                "\"temp\":%.2f,"
                "\"hum\":%.2f,"
                "\"ch1\":%" PRIu32 ","
                "\"ch2\":%" PRIu32 ","
                "\"ch3\":%" PRIu32 ","
                "\"ch4\":%" PRIu32 ","
                "\"ch5\":%" PRIu32 ","
                "\"ch6\":%" PRIu32 ","
                "\"ch7\":%" PRIu32 ","
                "\"ch8\":%" PRIu32 ","
                "\"ch9\":%" PRIu32 ","
                "\"ch10\":%" PRIu32 ","
                "\"ch11\":%" PRIu32 ","
                "\"ch12\":%" PRIu32 ","
                "\"ch13\":%" PRIu32 ","
                "\"ch14\":%" PRIu32 ","
                "\"ch15\":%" PRIu32 ","
                "\"ch16\":%" PRIu32 ","
                "\"total_count\":%" PRIu32
            "}"
        "}",
        message_count,
        temp, humid,
        ch[0],  ch[1],  ch[2],  ch[3],
        ch[4],  ch[5],  ch[6],  ch[7],
        ch[8],  ch[9],  ch[10], ch[11],
        ch[12], ch[13], ch[14], ch[15],
        total_count
    );
}

void mqtt_on_message(const char *topic, const char *payload, size_t payload_len)
{
    if (strncmp(payload, "led_on", 6) == 0) {
        // 打开 LED
    }
}

// ==================== 时间同步桥接函数 ====================

/**
 * @brief 从 ML307 读取网络时间 (供 ml307_sntp_time.cpp 调用)
 *
 * 发送 AT+CCLK? 并等待 URC 响应，返回标准格式:
 * +CCLK: "yy/MM/dd,hh:mm:ss+tz"
 */
extern "C" bool ml307_get_network_time(char *time_str, size_t size)
{
    if (time_str == NULL || size == 0 || at_uart_ == nullptr) {
        return false;
    }

    if (cclk_semaphore_ == NULL) {
        cclk_semaphore_ = xSemaphoreCreateBinary();
        if (cclk_semaphore_ == NULL) return false;
    }

    cclk_received_ = false;
    cclk_response_.clear();

    auto urc_it = at_uart_->RegisterUrcCallback(
        [](const std::string &command, const std::vector<AtArgumentValue> &arguments) {
            if (command == "CCLK" && !arguments.empty()) {
                cclk_response_.clear();
                for (size_t i = 0; i < arguments.size(); i++) {
                    cclk_response_ += arguments[i].string_value;
                }
                cclk_received_ = true;
                if (cclk_semaphore_ != NULL) {
                    xSemaphoreGive(cclk_semaphore_);
                }
            }
        }
    );

    if (!at_uart_->SendCommand("AT+CCLK?", 3000)) {
        at_uart_->UnregisterUrcCallback(urc_it);
        return false;
    }

    if (xSemaphoreTake(cclk_semaphore_, pdMS_TO_TICKS(3000)) != pdTRUE) {
        at_uart_->UnregisterUrcCallback(urc_it);
        return false;
    }

    at_uart_->UnregisterUrcCallback(urc_it);

    if (!cclk_received_ || cclk_response_.empty()) return false;

    // 智能修复缺失的逗号: "26/03/0914:56:20+32" → "26/03/09,14:56:20+32"
    std::string fixed = cclk_response_;
    if (fixed.find(',') == std::string::npos) {
        size_t last_slash = fixed.rfind('/');
        if (last_slash != std::string::npos && last_slash + 2 < fixed.length()) {
            fixed.insert(last_slash + 2, ",");
        }
    }

    snprintf(time_str, size, "+CCLK: \"%s\"", fixed.c_str());
    return true;
}

/**
 * @brief 触发 ML307 NTP 时间同步 (供 ml307_sntp_time.cpp 调用)
 */
extern "C" bool ml307_sync_ntp_time(void)
{
    if (at_uart_ == nullptr) return false;

    if (!at_uart_->SendCommand("AT+MNTP=\"ntp.aliyun.com\",123,0", 10000)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    return true;
}
