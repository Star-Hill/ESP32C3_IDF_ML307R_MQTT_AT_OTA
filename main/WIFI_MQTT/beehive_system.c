/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 10:26:37
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 01:21:10
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\WIFI_MQTT\beehive_system.c
 * @Description: BeeHive 系统核心模块 - 实现文件
 */

#include "beehive_system.h"
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

#include "ml307_mqtt_client.h"
#include "DHT11/bsp_dht11.h"
/* ==================== 内部变量 ==================== */

static const char *TAG = "beehive_system";

// MQTT 相关
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static TaskHandle_t s_mqtt_publish_task = NULL;
static bool s_mqtt_connected = false;
static int s_publish_interval = MQTT_PUBLISH_INTERVAL_SECONDS;

// WiFi 超时相关
static TaskHandle_t s_wifi_timeout_task = NULL;
static bool s_wifi_connected = false;
static bool s_wifi_timeout_triggered = false;

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
                    int new_interval = time_item->valueint;
                    ESP_LOGI(TAG, "🔧 更新上报间隔: %d -> %d 秒", s_publish_interval, new_interval);
                    s_publish_interval = new_interval;
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

    ESP_LOGI(TAG, "📡 MQTT 数据发布任务已启动");

    while (1)
    {
        if (s_mqtt_client != NULL && s_mqtt_connected)
        {
            token_id++;
            // 读取 DHT11 温湿度
            float temp = dht11_get_temperature();
            float humid = dht11_get_humidity();

            // 构建 JSON
            snprintf(mqtt_publish_data, sizeof(mqtt_publish_data),
                     "{\"device\":\"BeeHive_WIFI\",\"temp\":%.1f,\"humid\":%.1f,\"token\":%d,\"interval\":%d}",
                     temp, humid, token_id, s_publish_interval);

            esp_mqtt_client_publish(s_mqtt_client, MQTT_PUBLISH_TOPIC,
                                    mqtt_publish_data, strlen(mqtt_publish_data), 0, 0);

            ESP_LOGI(TAG, "📤 已发布 [%d]: %s", token_id, mqtt_publish_data);
        }
        vTaskDelay(pdMS_TO_TICKS(s_publish_interval * 1000));
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
    mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
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
                                      3072, NULL, 5, &s_wifi_timeout_task);
    if (task_ret != pdPASS)
    {
        ESP_LOGE(TAG, "❌ WiFi 超时检测任务创建失败");
        return ESP_FAIL;
    }

    // 3. 启动 MQTT 数据发布任务
    task_ret = xTaskCreate(mqtt_publish_task, "mqtt_publish",
                           4096, NULL, 5, &s_mqtt_publish_task);
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

    return ESP_OK;
}
