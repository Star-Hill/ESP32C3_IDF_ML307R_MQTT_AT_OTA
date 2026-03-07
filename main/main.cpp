// #include <stdio.h>
// #include <inttypes.h>
// #include "sdkconfig.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_chip_info.h"
// #include "esp_flash.h"
// #include "esp_timer.h"
// #include "freertos/FreeRTOSConfig.h"
// #include "esp_task_wdt.h"
// #include "esp_log.h"

// #include "DHT/bsp_dht11.h"
// #include "esp_io_expander_tca95xx_16bit.h"
// #include "xl9555_ir_counter.h"

// static const char *TAG = "MAIN";

// void app_main(void)
// {
//     // 启动DHT11任务，自动定时读取并输出
//     dht11_task_start();

//     // 1. 初始化硬件（I2C + XL9555 + INT GPIO）
//     ESP_ERROR_CHECK(xl9555_ir_counter_init());
//     // 2. 启动后台计数任务（每 5 秒自动打印日志）
//     ESP_ERROR_CHECK(xl9555_ir_counter_start());

//     ESP_LOGI(TAG, "System ready.");
//     // 3. 主循环：按需读取计数（示例：每 5 秒读一次 CH0）
//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(5000));

//         // uint32_t count = 0;
//         // xl9555_ir_counter_get(0, &count);
//         // ESP_LOGI(TAG, "CH00 count = %" PRIu32, count);

//         // 如需清零某通道：xl9555_ir_counter_reset(0);
//     }

//     /*
//      * 其他使用示例：
//      *
//      * // 暂停任务
//      * dht11_task_suspend();
//      * vTaskDelay(pdMS_TO_TICKS(5000));
//      *
//      * // 恢复任务
//      * dht11_task_resume();
//      * vTaskDelay(pdMS_TO_TICKS(5000));
//      *
//      * // 获取当前温湿度（不输出日志）
//      * float temp = dht11_get_temperature();
//      * float humi = dht11_get_humidity();
//      * printf("Current: T=%.2f, H=%.2f\n", temp, humi);
//      *
//      * // 停止任务
//      * dht11_task_stop();
//      */
// }

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

#include "at_uart.h"
#include "at_modem.h"
#include "ml307_mqtt.h"

static const char *TAG = "ML307_EMQX";

// ===== EMQX 服务器配置 =====
#define MQTT_BROKER     "175.178.117.18"      // EMQX 服务器 IP
#define MQTT_PORT       1883                   // EMQX 端口
#define CLIENT_ID       "BeeHive_ML307_Test"   // 客户端ID
#define USERNAME        "ML307"                // 用户名
#define PASSWORD        "ML307DEMO"            // 密码

// ===== MQTT 主题配置 =====
// 发布主题：ESP32 -> EMQX -> MQTTX 订阅这个主题
#define PUBLISH_TOPIC   "esp32/data"

// 订阅主题：MQTTX 发布到这个主题 -> ESP32 接收
#define SUBSCRIBE_TOPIC "esp32/control"

// GPIO 配置
#define ML307_TX_PIN    GPIO_NUM_18
#define ML307_RX_PIN    GPIO_NUM_19
#define ML307_DTR_PIN   GPIO_NUM_NC

// MQTT ID
#define MQTT_ID         0

class EmqxMqttTest
{
private:
    std::shared_ptr<AtUart> at_uart_;
    std::unique_ptr<AtModem> modem_;
    std::unique_ptr<Ml307Mqtt> mqtt_;
    bool is_connected_ = false;
    uint32_t message_count_ = 0;  // 消息计数器

public:
    EmqxMqttTest() {}

    // 步骤1: 初始化并检测 ML307 模组
    bool InitModem()
    {
        ESP_LOGI(TAG, "=== 步骤1: 检测 ML307 模组 ===");

        modem_ = AtModem::Detect(ML307_TX_PIN, ML307_RX_PIN, ML307_DTR_PIN, 921600);

        if (modem_ == nullptr)
        {
            ESP_LOGE(TAG, "❌ 未检测到 ML307 模组");
            return false;
        }

        ESP_LOGI(TAG, "✓ ML307 模组检测成功");
        at_uart_ = modem_->GetAtUart();

        // 打印模组信息
        std::string revision = modem_->GetModuleRevision();
        std::string imei = modem_->GetImei();
        std::string iccid = modem_->GetIccid();

        ESP_LOGI(TAG, "模组版本: %s", revision.c_str());
        ESP_LOGI(TAG, "IMEI: %s", imei.c_str());
        ESP_LOGI(TAG, "ICCID: %s", iccid.c_str());

        return true;
    }

    // 步骤2: 等待网络注册
    bool WaitForNetwork()
    {
        ESP_LOGI(TAG, "=== 步骤2: 等待网络注册 ===");

        modem_->OnNetworkStateChanged([this](bool network_ready)
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

        while (retry_count < max_retries)
        {
            auto result = modem_->WaitForNetworkReady();

            if (result == NetworkStatus::Ready)
            {
                ESP_LOGI(TAG, "✓ 网络注册成功");

                std::string carrier = modem_->GetCarrierName();
                int csq = modem_->GetCsq();

                ESP_LOGI(TAG, "运营商: %s", carrier.c_str());
                ESP_LOGI(TAG, "信号强度: %d/31", csq);

                return true;
            }
            else if (result == NetworkStatus::ErrorInsertPin)
            {
                ESP_LOGE(TAG, "❌ SIM卡需要PIN码");
                return false;
            }
            else if (result == NetworkStatus::ErrorRegistrationDenied)
            {
                ESP_LOGE(TAG, "❌ 网络注册被拒绝");
                return false;
            }

            ESP_LOGI(TAG, "等待网络注册... (%d/%d)", retry_count + 1, max_retries);
            vTaskDelay(pdMS_TO_TICKS(1000));
            retry_count++;
        }

        ESP_LOGE(TAG, "❌ 网络注册超时");
        return false;
    }

    // 步骤3: 连接 EMQX MQTT 服务器
    bool ConnectToEmqx()
    {
        ESP_LOGI(TAG, "=== 步骤3: 连接 EMQX MQTT 服务器 ===");

        // 创建 MQTT 客户端
        mqtt_ = std::make_unique<Ml307Mqtt>(at_uart_, MQTT_ID);

        // 设置回调函数
        mqtt_->OnConnected([this]()
        {
            ESP_LOGI(TAG, "✅ MQTT 已连接到 EMQX");
            is_connected_ = true;
        });

        mqtt_->OnDisconnected([this]()
        {
            ESP_LOGW(TAG, "⚠️ MQTT 已断开连接");
            is_connected_ = false;
        });

        mqtt_->OnError([](const std::string &error)
        {
            ESP_LOGE(TAG, "❌ MQTT 错误: %s", error.c_str());
        });

        mqtt_->OnMessage([](const std::string &topic, const std::string &payload)
        {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "📩 ========== 收到 MQTTX 消息 ==========");
            ESP_LOGI(TAG, "   主题: %s", topic.c_str());
            ESP_LOGI(TAG, "   内容: %s", payload.c_str());
            ESP_LOGI(TAG, "   长度: %d 字节", payload.length());
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "");
        });

        // 连接到 EMQX
        ESP_LOGI(TAG, "正在连接到 EMQX ...");
        ESP_LOGI(TAG, "  服务器: %s:%d", MQTT_BROKER, MQTT_PORT);
        ESP_LOGI(TAG, "  客户端ID: %s", CLIENT_ID);
        ESP_LOGI(TAG, "  用户名: %s", USERNAME);

        bool connected = mqtt_->Connect(
            MQTT_BROKER,
            MQTT_PORT,
            CLIENT_ID,
            USERNAME,
            PASSWORD);

        if (!connected)
        {
            int error_code = mqtt_->GetLastError();
            ESP_LOGE(TAG, "❌ MQTT 连接失败 (错误码: %d)", error_code);
            return false;
        }

        ESP_LOGI(TAG, "✅ MQTT 连接成功");
        is_connected_ = true;
        return true;
    }

    // 步骤4: 订阅主题
    bool SubscribeTopics()
    {
        ESP_LOGI(TAG, "=== 步骤4: 订阅主题 ===");
        ESP_LOGI(TAG, "订阅主题: %s", SUBSCRIBE_TOPIC);
        ESP_LOGI(TAG, "提示: 在 MQTTX 中发布消息到此主题，ESP32 会收到");

        if (!mqtt_->Subscribe(SUBSCRIBE_TOPIC, 0))
        {
            ESP_LOGE(TAG, "❌ 订阅失败");
            return false;
        }

        ESP_LOGI(TAG, "✅ 订阅成功");
        vTaskDelay(pdMS_TO_TICKS(500));

        return true;
    }

    // 发送单条消息
    bool PublishMessage(const std::string &message)
    {
        if (!is_connected_)
        {
            ESP_LOGW(TAG, "⚠️ MQTT 未连接，跳过发送");
            return false;
        }

        if (!mqtt_->Publish(PUBLISH_TOPIC, message, 0))
        {
            ESP_LOGE(TAG, "❌ 发布消息失败");
            return false;
        }

        return true;
    }

    // 周期性发送消息任务
    void PeriodicPublishTask()
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  开始周期性发送消息");
        ESP_LOGI(TAG, "  发送间隔: 1 秒");
        ESP_LOGI(TAG, "  发布主题: %s", PUBLISH_TOPIC);
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");

        while (true)
        {
            if (is_connected_)
            {
                // 构建消息内容（包含计数器、时间戳等信息）
                message_count_++;
                
                char message[256];
                snprintf(message, sizeof(message),
                         "{\"device\":\"ESP32-ML307\",\"count\":%lu,\"uptime\":%lu,\"message\":\"Hello from ESP32!\"}",
                         message_count_,
                         esp_log_timestamp() / 1000);  // 运行时间（秒）

                // 发送消息
                if (PublishMessage(message))
                {
                    ESP_LOGI(TAG, "📤 [%lu] 发送: %s", message_count_, message);
                }
                else
                {
                    ESP_LOGE(TAG, "❌ [%lu] 发送失败", message_count_);
                }
            }
            else
            {
                ESP_LOGW(TAG, "⚠️ MQTT 未连接，等待重连...");
                
                // 尝试重连
                if (mqtt_->Connect(MQTT_BROKER, MQTT_PORT, CLIENT_ID, USERNAME, PASSWORD))
                {
                    ESP_LOGI(TAG, "✅ MQTT 重连成功");
                    is_connected_ = true;
                    mqtt_->Subscribe(SUBSCRIBE_TOPIC, 0);
                }
                else
                {
                    ESP_LOGE(TAG, "❌ MQTT 重连失败");
                    vTaskDelay(pdMS_TO_TICKS(5000));  // 重连失败等待5秒
                    continue;
                }
            }

            // 等待 1 秒后发送下一条消息
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // 运行完整测试流程
    void Run()
    {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  ESP32-C3 + ML307 EMQX 测试程序");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");

        // 步骤1: 初始化模组
        if (!InitModem())
        {
            ESP_LOGE(TAG, "初始化失败，程序终止");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 步骤2: 等待网络
        if (!WaitForNetwork())
        {
            ESP_LOGE(TAG, "网络连接失败，程序终止");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));

        // 步骤3: 连接 MQTT
        if (!ConnectToEmqx())
        {
            ESP_LOGE(TAG, "MQTT 连接失败，程序终止");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 步骤4: 订阅主题
        if (!SubscribeTopics())
        {
            ESP_LOGW(TAG, "订阅失败，但继续执行");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  初始化完成！");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "📋 MQTTX 配置指南:");
        ESP_LOGI(TAG, "  1. 连接到 EMQX: %s:%d", MQTT_BROKER, MQTT_PORT);
        ESP_LOGI(TAG, "  2. 订阅主题查看 ESP32 消息: %s", PUBLISH_TOPIC);
        ESP_LOGI(TAG, "  3. 发布消息到主题控制 ESP32: %s", SUBSCRIBE_TOPIC);
        ESP_LOGI(TAG, "");

        // 开始周期性发送消息
        PeriodicPublishTask();
    }
};

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ESP32-C3 + ML307 启动中...");
    ESP_LOGI(TAG, "IDF 版本: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "");

    // 等待系统稳定
    vTaskDelay(pdMS_TO_TICKS(2000));

    // 创建测试实例并运行
    EmqxMqttTest test;
    test.Run();
}