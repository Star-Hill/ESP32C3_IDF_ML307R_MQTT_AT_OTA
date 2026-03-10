/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-07 13:35:36
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 01:18:05
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\ML307_MQTT\ml307_mqtt_client.cpp
 * @Description: ML307 MQTT 客户端模块 - 实现文件（极简版，专注于连接和日志输出）
 */
#include "ml307_mqtt_client.h"
#include "ml307_mqtt_config.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"

#include "at_uart.h"
#include "at_modem.h"
#include "ml307_mqtt.h"

static const char *TAG = "MqttClient";

// 全局变量
static TaskHandle_t mqtt_task_handle_ = NULL;

// MQTT 客户端对象
static std::shared_ptr<AtUart> at_uart_;
static std::unique_ptr<AtModem> modem_;
static std::unique_ptr<Ml307Mqtt> mqtt_;

// 状态变量
static bool is_connected_ = false;
static bool is_running_ = false;
static uint32_t message_count_ = 0;

// 用于接收CCLK响应的全局变量
static std::string cclk_response_;
static SemaphoreHandle_t cclk_semaphore_ = NULL;
static bool cclk_received_ = false;

/**
 * @brief 初始化 ML307 模组
 */
static bool init_modem(void)
{
    ESP_LOGI(TAG, "初始化 ML307 模组...");

    modem_ = AtModem::Detect(ML307_UART_TX_PIN, ML307_UART_RX_PIN, ML307_UART_DTR_PIN, 921600);

    if (modem_ == nullptr)
    {
        ESP_LOGE(TAG, "未检测到 ML307 模组");
        return false;
    }

    ESP_LOGI(TAG, "✓ ML307 模组检测成功");
    at_uart_ = modem_->GetAtUart();

    // 打印模组信息
    std::string revision = modem_->GetModuleRevision();
    std::string imei = modem_->GetImei();
    std::string iccid = modem_->GetIccid();

    ESP_LOGI(TAG, "  版本: %s", revision.c_str());
    ESP_LOGI(TAG, "  IMEI: %s", imei.c_str());
    ESP_LOGI(TAG, "  ICCID: %s", iccid.c_str());

    return true;
}

/**
 * @brief 等待网络注册
 */
static bool wait_for_network(void)
{
    ESP_LOGI(TAG, "等待网络注册...");

    modem_->OnNetworkStateChanged([](bool network_ready)
    {
        if (network_ready) {
            ESP_LOGI(TAG, "✓ 网络已就绪");
        } else {
            ESP_LOGW(TAG, "⚠ 网络断开");
            is_connected_ = false;
        }
    });

    int retry_count = 0;
    const int max_retries = 30;

    while (retry_count < max_retries && is_running_)
    {
        auto result = modem_->WaitForNetworkReady();

        if (result == NetworkStatus::Ready)
        {
            ESP_LOGI(TAG, "✓ 网络注册成功");

            std::string carrier = modem_->GetCarrierName();
            int csq = modem_->GetCsq();

            ESP_LOGI(TAG, "  运营商: %s", carrier.c_str());
            ESP_LOGI(TAG, "  信号强度: %d/31", csq);

            return true;
        }
        else if (result == NetworkStatus::ErrorInsertPin)
        {
            ESP_LOGE(TAG, "SIM卡需要PIN码");
            return false;
        }
        else if (result == NetworkStatus::ErrorRegistrationDenied)
        {
            ESP_LOGE(TAG, "网络注册被拒绝");
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
        retry_count++;
    }

    ESP_LOGE(TAG, "网络注册超时");
    return false;
}

/**
 * @brief 连接 MQTT 服务器
 */
static bool connect_mqtt(void)
{
    ESP_LOGI(TAG, "连接 MQTT 服务器...");

    // 创建 MQTT 客户端
    mqtt_ = std::make_unique<Ml307Mqtt>(at_uart_, MQTT_CONNECTION_ID);

    // 设置回调函数
    mqtt_->OnConnected([]()
    {
        ESP_LOGI(TAG, "✅ MQTT 已连接");
        is_connected_ = true;
    });

    mqtt_->OnDisconnected([]()
    {
        ESP_LOGW(TAG, "⚠️ MQTT 已断开");
        is_connected_ = false;
    });

    mqtt_->OnError([](const std::string &error)
    {
        ESP_LOGE(TAG, "MQTT 错误: %s", error.c_str());
    });

    // 设置消息接收回调，直接调用用户实现的函数
    mqtt_->OnMessage([](const std::string &topic, const std::string &payload)
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📩 ========== 收到 MQTT 消息 ==========");
        ESP_LOGI(TAG, "   主题: %s", topic.c_str());
        ESP_LOGI(TAG, "   内容: %s", payload.c_str());
        ESP_LOGI(TAG, "   长度: %d 字节", payload.length());
        ESP_LOGI(TAG, "=====================================");
        ESP_LOGI(TAG, "");

        // 调用用户实现的消息处理函数
        mqtt_on_message(topic.c_str(), payload.c_str(), payload.length());
    });

    // 连接到服务器
    ESP_LOGI(TAG, "  服务器: %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    ESP_LOGI(TAG, "  客户端ID: %s", MQTT_CLIENT_ID);

    bool connected = mqtt_->Connect(
        MQTT_BROKER_HOST,
        MQTT_BROKER_PORT,
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD);

    if (!connected)
    {
        int error_code = mqtt_->GetLastError();
        ESP_LOGE(TAG, "MQTT 连接失败 (错误码: %d)", error_code);
        return false;
    }

    ESP_LOGI(TAG, "✅ MQTT 连接成功");
    is_connected_ = true;
    return true;
}

/**
 * @brief 订阅主题
 */
static bool subscribe_topic(void)
{
    ESP_LOGI(TAG, "订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);

    if (!mqtt_->Subscribe(MQTT_SUBSCRIBE_TOPIC, 0))
    {
        ESP_LOGE(TAG, "订阅失败");
        return false;
    }

    ESP_LOGI(TAG, "✅ 订阅成功");
    return true;
}

/**
 * @brief MQTT 客户端主任务
 */
static void mqtt_client_task(void *pvParameters)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 客户端任务启动");
    ESP_LOGI(TAG, "========================================");

    // 步骤1: 初始化模组
    if (!init_modem())
    {
        ESP_LOGE(TAG, "模组初始化失败");
        goto task_exit;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 步骤2: 等待网络
    if (!wait_for_network())
    {
        ESP_LOGE(TAG, "网络连接失败");
        goto task_exit;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 步骤3: 连接 MQTT
    if (!connect_mqtt())
    {
        ESP_LOGE(TAG, "MQTT 连接失败");
        goto task_exit;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 步骤4: 订阅主题
    if (!subscribe_topic())
    {
        ESP_LOGW(TAG, "订阅失败，但继续运行");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  初始化完成，开始周期性发送");
    ESP_LOGI(TAG, "  发送间隔: %lu ms", MQTT_PUBLISH_INTERVAL_MS);
    ESP_LOGI(TAG, "  发布主题: %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "  订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 主循环：周期性发送消息
    char message_buffer[MQTT_MAX_MESSAGE_SIZE];
    
    while (is_running_)
    {
        if (is_connected_)
        {
            message_count_++;
            
            // 调用用户实现的消息构建函数
            int msg_len = mqtt_build_message(message_buffer, sizeof(message_buffer), message_count_);
            
            if (msg_len > 0)
            {
                // 发送消息
                if (mqtt_->Publish(MQTT_PUBLISH_TOPIC, std::string(message_buffer, msg_len), 0))
                {
                    ESP_LOGI(TAG, "📤 [%lu] %s", message_count_, message_buffer);
                }
                else
                {
                    ESP_LOGE(TAG, "❌ [%lu] 发送失败", message_count_);
                }
            }
            else
            {
                // 返回 0 表示跳过本次发送
                ESP_LOGD(TAG, "跳过发送 (mqtt_build_message 返回 0)");
            }
        }
        else
        {
            ESP_LOGW(TAG, "MQTT 未连接，尝试重连...");
            
            if (mqtt_->Connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT,
                              MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD))
            {
                ESP_LOGI(TAG, "✅ 重连成功");
                is_connected_ = true;
                mqtt_->Subscribe(MQTT_SUBSCRIBE_TOPIC, 0);
            }
            else
            {
                ESP_LOGE(TAG, "❌ 重连失败");
                vTaskDelay(pdMS_TO_TICKS(5000));
                continue;
            }
        }

        // 等待下一次发送
        vTaskDelay(pdMS_TO_TICKS(MQTT_PUBLISH_INTERVAL_MS));
    }

task_exit:
    ESP_LOGI(TAG, "MQTT 客户端任务退出");
    mqtt_task_handle_ = NULL;
    vTaskDelete(NULL);
}

// ==================== 公共 API 实现 ====================

bool mqtt_client_start(void)
{
    if (mqtt_task_handle_ != NULL)
    {
        ESP_LOGW(TAG, "MQTT 客户端任务已经在运行");
        return false;
    }

    // 重置状态
    is_running_ = true;
    is_connected_ = false;
    message_count_ = 0;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 客户端配置");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "服务器: %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    ESP_LOGI(TAG, "客户端ID: %s", MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "发布主题: %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "发送间隔: %lu ms", MQTT_PUBLISH_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 创建任务
    BaseType_t ret = xTaskCreate(
        mqtt_client_task,
        "mqtt_client",
        MQTT_TASK_STACK_SIZE,
        NULL,
        MQTT_TASK_PRIORITY,
        &mqtt_task_handle_);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "创建任务失败");
        return false;
    }

    ESP_LOGI(TAG, "✅ MQTT 客户端任务已启动");
    return true;
}

void mqtt_client_stop(void)
{
    if (mqtt_task_handle_ == NULL)
    {
        ESP_LOGW(TAG, "MQTT 客户端任务未运行");
        return;
    }

    ESP_LOGI(TAG, "正在停止 MQTT 客户端任务...");
    is_running_ = false;

    // 等待任务退出
    while (mqtt_task_handle_ != NULL)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "MQTT 客户端任务已停止");
}

bool mqtt_client_publish(const char *message)
{
    if (!is_connected_ || mqtt_ == nullptr)
    {
        ESP_LOGW(TAG, "MQTT 未连接");
        return false;
    }

    if (message == NULL)
    {
        ESP_LOGE(TAG, "消息为空");
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

// 回调1: 构建 JSON 消息
int mqtt_build_message(char *buffer, size_t buffer_size, uint32_t message_count)
{
    // 从 DHT11 获取实时温湿度
    float temp = dht11_get_temperature();
    float humid = dht11_get_humidity();

    // 拼接 JSON
    return snprintf(buffer, buffer_size,
        "{\"device\":\"BeeHive\",\"temp\":%.1f,\"humid\":%.1f,\"count\":%lu}",
        temp, humid, message_count);
}

// 回调2: 处理收到的消息
void mqtt_on_message(const char *topic, const char *payload, size_t payload_len)
{
    if (strncmp(payload, "led_on", 6) == 0) {
        // 打开 LED
    }
}

// ==================== 时间同步函数 (极简版 - 只提取时分秒) ====================

/**
 * @brief 从 ML307 获取网络时间 (标准AT响应格式)
 * 
 * 返回格式: +CCLK: "26/03/09,08:53:13+32"
 * ml307_sntp_time.cpp 会解析这个格式并转换为本地时间
 */
extern "C" bool ml307_get_network_time(char *time_str, size_t size)
{
    if (time_str == NULL || size == 0 || at_uart_ == nullptr) {
        return false;
    }

    // 创建信号量
    if (cclk_semaphore_ == NULL) {
        cclk_semaphore_ = xSemaphoreCreateBinary();
        if (cclk_semaphore_ == NULL) {
            return false;
        }
    }

    // 重置状态
    cclk_received_ = false;
    cclk_response_.clear();

    // 注册URC回调 - 拼接所有参数
    auto urc_callback_it = at_uart_->RegisterUrcCallback(
        [](const std::string& command, const std::vector<AtArgumentValue>& arguments) {
            if (command == "CCLK" && arguments.size() > 0) {
                // 拼接所有参数
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

    // 发送命令
    if (!at_uart_->SendCommand("AT+CCLK?", 3000)) {
        at_uart_->UnregisterUrcCallback(urc_callback_it);
        return false;
    }
    
    // 等待响应
    if (xSemaphoreTake(cclk_semaphore_, pdMS_TO_TICKS(3000)) != pdTRUE) {
        at_uart_->UnregisterUrcCallback(urc_callback_it);
        return false;
    }

    at_uart_->UnregisterUrcCallback(urc_callback_it);

    if (!cclk_received_ || cclk_response_.empty()) {
        return false;
    }

    // 智能修复: 如果缺少逗号,补上
    // "26/03/008:53:13+32" -> "26/03/09,08:53:13+32"
    std::string fixed = cclk_response_;
    
    if (fixed.find(',') == std::string::npos) {
        // 没有逗号,尝试补全
        // 找到最后一个 '/' 后的位置
        size_t last_slash = fixed.rfind('/');
        if (last_slash != std::string::npos && last_slash + 2 < fixed.length()) {
            // 在日期后2位插入逗号
            fixed.insert(last_slash + 2, ",");
        }
    }
    
    // 返回标准格式: +CCLK: "26/03/09,08:53:13+32"
    snprintf(time_str, size, "+CCLK: \"%s\"", fixed.c_str());
    
    return true;
}

/**
 * @brief 触发 ML307 NTP 时间同步
 */
extern "C" bool ml307_sync_ntp_time(void)
{
    if (at_uart_ == nullptr) {
        return false;
    }

    if (!at_uart_->SendCommand("AT+MNTP=\"ntp.aliyun.com\",123,0", 10000)) {
        return false;
    }
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    return true;
}
