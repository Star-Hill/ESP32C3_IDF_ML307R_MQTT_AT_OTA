/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 16:34:55
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 15:27:22
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\wifi_sntp_time.c
 * @Description: 时间显示模块实现
 */
#include "wifi_sntp_time.h"
#include "beehive_system_config.h" // 统一配置：TIME_DISPLAY_INTERVAL_SECONDS
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include <string.h>

/* ==================== 配置宏定义 ==================== */
// TIME_DISPLAY_INTERVAL_SECONDS 已由 beehive_system_config.h 统一定义

// NTP 服务器（可选多个，自动选择最快的）
#define NTP_SERVER_1 "ntp.aliyun.com"  // 阿里云 NTP
#define NTP_SERVER_2 "ntp.tencent.com" // 腾讯 NTP
#define NTP_SERVER_3 "pool.ntp.org"    // 国际 NTP 池

// 时区设置（中国标准时间/上海时间）
#define TIMEZONE "CST-8"

/* ==================== 内部变量 ==================== */
static const char *TAG = "time_display";
static TaskHandle_t s_time_display_task = NULL;
static bool s_time_synced = false;

/* ==================== NTP 时间同步回调 ==================== */

static void time_sync_notification_cb(struct timeval *tv)
{
    s_time_synced = true;
    ESP_LOGI(TAG, "✅ 时间同步成功");
}

/* ==================== 时间同步初始化 ==================== */

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "🕐 正在初始化 NTP 时间同步...");

    // 设置时区为上海时间（CST-8）
    setenv("TZ", TIMEZONE, 1);
    tzset();

    // 配置 SNTP
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NTP_SERVER_1);
    esp_sntp_setservername(1, NTP_SERVER_2);
    esp_sntp_setservername(2, NTP_SERVER_3);

    // 设置时间同步回调
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);

    // 启动 SNTP
    esp_sntp_init();

    ESP_LOGI(TAG, "✅ NTP 客户端已启动");
    ESP_LOGI(TAG, "   服务器1: %s", NTP_SERVER_1);
    ESP_LOGI(TAG, "   服务器2: %s", NTP_SERVER_2);
    ESP_LOGI(TAG, "   服务器3: %s", NTP_SERVER_3);
    ESP_LOGI(TAG, "   时区: %s(上海时间)", TIMEZONE);
}

/* ==================== 等待时间同步 ==================== */

static bool wait_for_time_sync(void)
{
    int retry = 0;
    const int max_retry = 50; // 最多等待 50 秒

    ESP_LOGI(TAG, "⏳ 等待时间同步...");

    while (!s_time_synced && retry < max_retry)
    {
        ESP_LOGI(TAG, "   等待中... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (s_time_synced)
    {
        ESP_LOGI(TAG, "✅ 时间同步完成");
        return true;
    }
    else
    {
        ESP_LOGE(TAG, "❌ 时间同步超时，退出时间显示任务");
        return false;
    }
}

/* ==================== 获取时间字符串 ==================== */

esp_err_t wifi_time_display_get_time_string(char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size < 64)
    {
        return ESP_FAIL;
    }

    time_t now;
    struct tm timeinfo;

    time(&now);
    localtime_r(&now, &timeinfo);

    // 格式：2026年3月8日  16:30
    snprintf(buffer, buffer_size,
             "当前时间为 : %d年%d月%d日  %02d:%02d",
             timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1,
             timeinfo.tm_mday,
             timeinfo.tm_hour,
             timeinfo.tm_min);

    return ESP_OK;
}

/* ==================== 时间显示任务 ==================== */

static void time_display_task(void *pvParameters)
{
    char time_str[128];

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  时间显示任务已启动");
    ESP_LOGI(TAG, "  显示间隔: %d 秒", TIME_DISPLAY_INTERVAL_SECONDS);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 初始化 NTP
    initialize_sntp();

    // 等待时间同步，失败则退出任务
    if (!wait_for_time_sync())
    {
        ESP_LOGE(TAG, "时间显示任务退出");
        s_time_display_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  开始定时显示时间");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 主循环：每 10 秒显示一次时间
    while (1)
    {
        // 获取时间字符串
        if (wifi_time_display_get_time_string(time_str, sizeof(time_str)) == ESP_OK)
        {
            // 使用 INFO 级别日志显示
            ESP_LOGI(TAG, "🕐 %s", time_str);
        }
        else
        {
            ESP_LOGE(TAG, "❌ 获取时间失败");
        }

        // 等待指定间隔
        vTaskDelay(pdMS_TO_TICKS(TIME_DISPLAY_INTERVAL_SECONDS * 1000));
    }
}

/* ==================== 公共 API 实现 ==================== */

esp_err_t wifi_time_display_start(void)
{
    if (s_time_display_task != NULL)
    {
        ESP_LOGW(TAG, "⚠️  时间显示任务已在运行");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "🚀 启动时间显示模块...");

    // 创建任务
    BaseType_t ret = xTaskCreate(
        time_display_task,
        "time_display",
        4096,
        NULL,
        5,
        &s_time_display_task);

    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "❌ 时间显示任务创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✅ 时间显示任务已启动");
    return ESP_OK;
}

esp_err_t wifi_time_display_stop(void)
{
    if (s_time_display_task == NULL)
    {
        ESP_LOGW(TAG, "⚠️  时间显示任务未运行");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "⏹️  正在停止时间显示任务...");

    vTaskDelete(s_time_display_task);
    s_time_display_task = NULL;

    ESP_LOGI(TAG, "✅ 时间显示任务已停止");
    return ESP_OK;
}