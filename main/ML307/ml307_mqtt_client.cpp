/**
 * @file ml307_mqtt_client.cpp
 * @brief ML307 MQTT 客户端模块 - 实现文件
 */

#include "ml307_mqtt_client.h"
#include "ml307_mqtt_config.h"  // 包含配置文件

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "at_uart.h"
#include "at_modem.h"
#include "ml307_mqtt.h"

static const char *TAG = "MqttClient";

// 全局变量
static TaskHandle_t mqtt_task_handle_ = NULL;
static mqtt_message_callback_t message_callback_ = NULL;

// MQTT 客户端对象
static std::shared_ptr<AtUart> at_uart_;
static std::unique_ptr<AtModem> modem_;
static std::unique_ptr<Ml307Mqtt> mqtt_;

// 状态变量
static bool is_connected_ = false;
static bool is_running_ = false;
static uint32_t message_count_ = 0;

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

    mqtt_->OnMessage([](const std::string &topic, const std::string &payload)
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📩 ========== 收到消息 ==========");
        ESP_LOGI(TAG, "   主题: %s", topic.c_str());
        ESP_LOGI(TAG, "   内容: %s", payload.c_str());
        ESP_LOGI(TAG, "================================");
        ESP_LOGI(TAG, "");

        // 调用用户回调
        if (message_callback_)
        {
            message_callback_(topic.c_str(), payload.c_str(), payload.length());
        }
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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  初始化完成");
    ESP_LOGI(TAG, "  发送间隔: %lu ms", MQTT_PUBLISH_INTERVAL_MS);
    ESP_LOGI(TAG, "  发布主题: %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "  订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "========================================");

    // 主循环：周期性发送消息
    while (is_running_)
    {
        if (is_connected_)
        {
            // 构建消息
            message_count_++;
            
            char message[256];
            int len = 0;
            
            // 构建 JSON 消息
            len += snprintf(message + len, sizeof(message) - len, "{");
            
#if MQTT_INCLUDE_DEVICE_NAME
            len += snprintf(message + len, sizeof(message) - len, 
                           "\"device\":\"%s\",", MQTT_DEVICE_NAME);
#endif

#if MQTT_INCLUDE_MESSAGE_COUNT
            len += snprintf(message + len, sizeof(message) - len, 
                           "\"count\":%lu,", message_count_);
#endif

#if MQTT_INCLUDE_UPTIME
            len += snprintf(message + len, sizeof(message) - len, 
                           "\"uptime\":%lu,", esp_log_timestamp() / 1000);
#endif

            len += snprintf(message + len, sizeof(message) - len, 
                           "\"message\":\"Hello from ESP32!\"}");

            // 发送消息
            if (mqtt_->Publish(MQTT_PUBLISH_TOPIC, message, 0))
            {
                ESP_LOGI(TAG, "📤 [%lu] %s", message_count_, message);
            }
            else
            {
                ESP_LOGE(TAG, "❌ [%lu] 发送失败", message_count_);
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

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 客户端配置");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "服务器: %s:%d", MQTT_BROKER_HOST, MQTT_BROKER_PORT);
    ESP_LOGI(TAG, "客户端ID: %s", MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "发布主题: %s", MQTT_PUBLISH_TOPIC);
    ESP_LOGI(TAG, "订阅主题: %s", MQTT_SUBSCRIBE_TOPIC);
    ESP_LOGI(TAG, "发送间隔: %lu ms", MQTT_PUBLISH_INTERVAL_MS);
    ESP_LOGI(TAG, "========================================");

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

void mqtt_client_set_message_callback(mqtt_message_callback_t callback)
{
    message_callback_ = callback;
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
