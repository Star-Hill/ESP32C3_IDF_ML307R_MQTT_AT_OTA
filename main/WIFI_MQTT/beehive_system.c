/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 10:26:37
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 15:28:52
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\WIFI_MQTT\beehive_system.c
 * @Description: BeeHive 系统核心模块 - 实现文件
 */

#include "beehive_system.h"
#include "beehive_system_config.h" // 统一配置文件
#include "xn_wifi_manage.h"
#include "ml307_mqtt_config.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <time.h> // localtime_r

#include "ml307_mqtt_client.h"
#include "DHT11/bsp_dht11.h"
#include "XL9555/xl9555_ir_counter.h"
#include "wifi_sntp_time.h"

/* ==================== 内部变量 ==================== */

static const char *TAG = "beehive_system";

// MQTT 相关
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static TaskHandle_t s_mqtt_publish_task = NULL;
static bool s_mqtt_connected = false;

// WiFi 超时相关
static TaskHandle_t s_wifi_timeout_task = NULL;
static bool s_wifi_connected = false;
static bool s_wifi_timeout_triggered = false;

// 动态间隔覆盖：0 表示不覆盖，使用分时策略；>0 表示云端下发的自定义间隔（毫秒）
static uint32_t s_publish_interval_override_ms = 0;

/* ==================== 分时间隔辅助函数 ==================== */

/**
 * @brief 根据当前本地时间返回应使用的 MQTT 上传间隔（毫秒）
 *        高频时段 [HIGH_START, HIGH_END)  → WIFI_MQTT_HIGH_FREQ_INTERVAL_MS
 *        低频时段 其余                    → WIFI_MQTT_LOW_FREQ_INTERVAL_MS
 *        若系统时间尚未同步（year<2020），保守使用高频间隔
 */
static uint32_t get_wifi_publish_interval_ms(void)
{
    // 云端动态下发了自定义间隔，优先使用
    if (s_publish_interval_override_ms > 0)
    {
        return s_publish_interval_override_ms;
    }

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    // 年份合理性校验（未同步时 tm_year+1900 < 2020）
    if (ti.tm_year + 1900 < 2020)
    {
        return (uint32_t)WIFI_MQTT_HIGH_FREQ_INTERVAL_MS;
    }

    int h = ti.tm_hour;
    bool high = (h >= WIFI_MQTT_HIGH_FREQ_START_HOUR &&
                 h < WIFI_MQTT_HIGH_FREQ_END_HOUR);

    return high ? (uint32_t)WIFI_MQTT_HIGH_FREQ_INTERVAL_MS
                : (uint32_t)WIFI_MQTT_LOW_FREQ_INTERVAL_MS;
}

/* ==================== MQTT 功能 ==================== */

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "✅ MQTT 已连接到服务器");
        s_mqtt_connected = true;
        msg_id = esp_mqtt_client_subscribe(client, MQTT_SUBSCRIBE_TOPIC, 0);
        ESP_LOGI(TAG, "📥 已订阅主题: %s (msg_id=%d)", MQTT_SUBSCRIBE_TOPIC, msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "❌ MQTT 已断开连接");
        s_mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "✅ 订阅成功, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
    {
        ESP_LOGI(TAG, "📨 收到 MQTT 消息");
        ESP_LOGI(TAG, "  主题: %.*s", event->topic_len, event->topic);
        ESP_LOGI(TAG, "  数据: %.*s", event->data_len, event->data);

        cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
        if (!root)
        {
            ESP_LOGW(TAG, "⚠️  收到的数据不是有效的 JSON");
            break;
        }

        cJSON *method = cJSON_GetObjectItem(root, "method");
        if (cJSON_IsString(method) && strcmp(method->valuestring, "control") == 0)
        {
            cJSON *params = cJSON_GetObjectItem(root, "params");
            if (cJSON_IsObject(params))
            {
                cJSON *time_item = cJSON_GetObjectItem(params, "time");
                if (cJSON_IsNumber(time_item))
                {
                    cJSON *time_item = cJSON_GetObjectItem(params, "time");
                    if (cJSON_IsNumber(time_item))
                    {
                        int new_interval_sec = time_item->valueint;
                        uint32_t new_interval_ms = (uint32_t)(new_interval_sec * 1000);
                        ESP_LOGI(TAG, "🔧 更新上报间隔: %lu ms -> %d s (%d ms)",
                                 s_publish_interval_override_ms, new_interval_sec, new_interval_sec * 1000);
                        s_publish_interval_override_ms = new_interval_ms;
                    }
                }
            }
        }
        cJSON_Delete(root);
    }
    break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "❌ MQTT 错误");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("socket errno", event->error_handle->esp_transport_sock_errno);
        }
        break;

    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "🔄 正在连接 MQTT 服务器...");
        break;

    default:
        break;
    }
}

static void mqtt_publish_task(void *pvParameters)
{
    char mqtt_publish_data[1024];
    int token_id = 0;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  MQTT 数据发布任务已启动");
    ESP_LOGI(TAG, "  高频时段 : %02d:00 ~ %02d:00 (间隔 %lu s)",
             WIFI_MQTT_HIGH_FREQ_START_HOUR, WIFI_MQTT_HIGH_FREQ_END_HOUR,
             WIFI_MQTT_HIGH_FREQ_INTERVAL_MS / 1000UL);
    ESP_LOGI(TAG, "  低频时段 : 其余      (间隔 %lu s)",
             WIFI_MQTT_LOW_FREQ_INTERVAL_MS / 1000UL);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    while (1)
    {
        uint32_t interval_ms = get_wifi_publish_interval_ms();

        if (s_mqtt_client != NULL && s_mqtt_connected)
        {
            token_id++;

            // 读取 DHT11 温湿度
            float temp = dht11_get_temperature();
            float humid = dht11_get_humidity();

            // 读取 16 路红外计数
            uint32_t ch[XL9555_IR_CHANNEL_COUNT] = {0};
            xl9555_ir_counter_get_all(ch);

            uint32_t total_count = 0;
            for (int i = 0; i < XL9555_IR_CHANNEL_COUNT; i++)
            {
                total_count += ch[i];
            }

            // 构建 JSON
            snprintf(mqtt_publish_data, sizeof(mqtt_publish_data),
                     "{"
                     "\"method\":\"report\","
                     "\"clientToken\":%d,"
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
                     token_id,
                     temp, humid,
                     ch[0], ch[1], ch[2], ch[3],
                     ch[4], ch[5], ch[6], ch[7],
                     ch[8], ch[9], ch[10], ch[11],
                     ch[12], ch[13], ch[14], ch[15],
                     total_count);

            // 发布并判断结果
            int msg_id = esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLISH_TOPIC,
                                                 mqtt_publish_data,
                                                 strlen(mqtt_publish_data), 0, 0);
            if (msg_id >= 0)
            {
                // 打印发布日志，附上当前时段信息
                time_t now;
                struct tm ti;
                time(&now);
                localtime_r(&now, &ti);
                bool high = (ti.tm_hour >= WIFI_MQTT_HIGH_FREQ_START_HOUR &&
                             ti.tm_hour < WIFI_MQTT_HIGH_FREQ_END_HOUR);
                ESP_LOGI(TAG, "📤 [%d] %s  (%s %lu s)",
                         token_id, mqtt_publish_data,
                         high ? "高频" : "低频",
                         interval_ms / 1000UL);

                // ✅ 发布成功后重置红外计数
                xl9555_ir_counter_reset(0xFF);
            }
            else
            {
                ESP_LOGE(TAG, "❌ [%d] 发布失败 (msg_id=%d)", token_id, msg_id);
            }
        }

        // 将长间隔切成 1 秒小片，保持任务响应性
        uint32_t elapsed = 0;
        while (elapsed < interval_ms)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            elapsed += 1000;
        }
    }
}

static esp_err_t mqtt_start(void)
{
    if (s_mqtt_client != NULL)
    {
        ESP_LOGW(TAG, "⚠️  MQTT 客户端已启动");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🚀 正在启动 MQTT 客户端...");

    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.hostname = MQTT_BROKER_HOST;
    mqtt_cfg.broker.address.port = MQTT_BROKER_PORT;
    mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
    mqtt_cfg.credentials.client_id = mqtt_client_get_client_id();
    mqtt_cfg.credentials.username = MQTT_USERNAME;
    mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL)
    {
        ESP_LOGE(TAG, "❌ MQTT 客户端初始化失败");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);

    ESP_LOGI(TAG, "✅ MQTT 客户端已启动");
    return ESP_OK;
}

/* ==================== WiFi 超时检测功能 ==================== */

static void wifi_timeout_check_task(void *pvParameters)
{
    const int CHECK_INTERVAL_MS = WIFI_CHECK_INTERVAL_SECONDS * 1000;
    const int TIMEOUT_MS = WIFI_TIMEOUT_SECONDS * 1000;

    int elapsed_time_ms = 0;

    ESP_LOGI(TAG, "⏱️  WiFi 超时检测任务已启动");
    ESP_LOGI(TAG, "    超时时间：%d 秒", WIFI_TIMEOUT_SECONDS);
    ESP_LOGI(TAG, "    检查间隔：%d 秒", WIFI_CHECK_INTERVAL_SECONDS);

    while (elapsed_time_ms < TIMEOUT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(CHECK_INTERVAL_MS));
        elapsed_time_ms += CHECK_INTERVAL_MS;

        if (s_wifi_connected)
        {
            ESP_LOGI(TAG, "✅ WiFi 已连接，超时检测任务正常退出");
            s_wifi_timeout_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        int remaining_sec = (TIMEOUT_MS - elapsed_time_ms) / 1000;
        ESP_LOGI(TAG, "⏳ WiFi 未连接，剩余等待时间：%d 秒", remaining_sec);
    }

    if (!s_wifi_connected)
    {
        ESP_LOGW(TAG, "WIFI配对模式下用户没有选择连接，连接自动切换为4G");

        s_wifi_timeout_triggered = true;

        ESP_LOGI(TAG, "⏹️  正在停止 WiFi 模块...");
        esp_wifi_disconnect();
        esp_err_t ret = esp_wifi_stop();
        if (ret == ESP_OK)
        {
            ESP_LOGI(TAG, "✅ WiFi 驱动已停止");
        }
        else
        {
            ESP_LOGW(TAG, "⚠️  WiFi 停止失败: %s", esp_err_to_name(ret));
        }
        ESP_LOGI(TAG, "✅ WiFi 模块已完全停止");

        // 启动 ML307 4G MQTT 客户端
        ESP_LOGI(TAG, "🚀 启动 ML307 4G MQTT 客户端");

        if (mqtt_client_start()) // ← 调用你的 ML307 客户端
        {
            ESP_LOGI(TAG, "✅ ML307 MQTT 客户端已启动");
        }
    }

    s_wifi_timeout_task = NULL;
    vTaskDelete(NULL);
}

/* ==================== WiFi 事件回调 ==================== */

static void wifi_event_callback(wifi_manage_state_t state)
{
    if (s_wifi_timeout_triggered)
    {
        ESP_LOGD(TAG, "⏹️  WiFi 已超时，忽略事件");
        return;
    }

    switch (state)
    {
    case WIFI_MANAGE_STATE_CONNECTED:
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  ✅ WiFi 已连接");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");

        s_wifi_connected = true;

        // WiFi 连接成功后自动启动 MQTT
        mqtt_start();
        break;

    case WIFI_MANAGE_STATE_DISCONNECTED:
        ESP_LOGW(TAG, "⚠️  WiFi 已断开");
        s_wifi_connected = false;
        break;

    case WIFI_MANAGE_STATE_CONNECT_FAILED:
        ESP_LOGE(TAG, "❌ WiFi 连接失败");
        s_wifi_connected = false;
        break;

    default:
        break;
    }
}

/* ==================== 系统启动函数 ==================== */

esp_err_t beehive_system_start(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  BeeHive ESP32-C3 WiFi + MQTT 系统");
    ESP_LOGI(TAG, "  IDF 版本: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 1. 初始化 WiFi 配网模块
    printf("🌐 启动 WiFi 配网模块\n");

    wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
    wifi_cfg.wifi_event_cb = wifi_event_callback;

    esp_err_t ret = wifi_manage_init(&wifi_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "❌ WiFi 模块初始化失败: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "✅ WiFi 配网模块初始化成功");

    // 2. 启动 WiFi 超时检测
    BaseType_t task_ret = xTaskCreate(wifi_timeout_check_task, "wifi_timeout",
                                      WIFI_TIMEOUT_TASK_STACK_SIZE, NULL,
                                      WIFI_TIMEOUT_TASK_PRIORITY, &s_wifi_timeout_task);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "❌ WiFi 超时检测任务创建失败");
        return ESP_FAIL;
    }

    // 3. 启动 MQTT 数据发布任务
    task_ret = xTaskCreate(mqtt_publish_task, "mqtt_publish",
                           MQTT_PUBLISH_TASK_STACK_SIZE, NULL,
                           MQTT_PUBLISH_TASK_PRIORITY, &s_mqtt_publish_task);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "❌ MQTT 发布任务创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统启动完成");
    ESP_LOGI(TAG, "  等待 WiFi 连接（%d秒超时）...", WIFI_TIMEOUT_SECONDS);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 启动WIFI的时间显示任务
    wifi_time_display_start();

    return ESP_OK;
}