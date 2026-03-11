/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-09 13:44:22
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-11 15:10:31
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\ml307_sntp_time.cpp
 * @Description: ML307 网络时间同步模块 - 实现文件
 *
 * 时间管理策略:
 * - 首次: NTP同步 → 解析 AT+CCLK? 响应 → settimeofday 写入 RTC
 * - 运行中: 每 TIME_LOG_INTERVAL_MS 用 gettimeofday 读 RTC 打印 HH:MM:SS
 * - 每日凌晨: 在 00:00 ~ 00:(TIME_MIDNIGHT_SYNC_WINDOW_MIN) 触发一次重新同步
 *             同一窗口内只同步一次，窗口结束后重置标志
 */

#include "ml307_sntp_time.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"

#include "at_uart.h"
#include "at_modem.h"

static const char *TAG = "ML307_Time";

// ==================== 全局变量 ====================

static TaskHandle_t    time_task_handle_   = NULL;
static bool            is_running_         = false;
static bool            rtc_synced_         = false;   ///< RTC 是否已被 NTP 写入过
static ntp_sync_status_t ntp_sync_status_  = NTP_SYNC_IDLE;

// 每日凌晨同步控制
static bool midnight_sync_done_            = false;   ///< 当日窗口是否已同步过

// 时间回调
static ml307_time_callback_t time_callback_ = NULL;

// AT 互斥锁 (与 MQTT 模块共享，避免同时发 AT 命令)
static SemaphoreHandle_t at_command_mutex_ = NULL;

// ==================== 外部 C++ 桥接声明 ====================

/**
 * @brief 从 ML307 获取网络时间字符串 (在 ml307_mqtt_client.cpp 中实现)
 *
 * 输出格式: +CCLK: "yy/MM/dd,hh:mm:ss+tz"
 */
extern "C" bool ml307_get_network_time(char *time_str, size_t size);

/**
 * @brief 触发 ML307 NTP 同步 (在 ml307_mqtt_client.cpp 中实现)
 */
extern "C" bool ml307_sync_ntp_time(void);

// ==================== 内部辅助函数 ====================

/**
 * @brief 解析 AT+CCLK? 响应，填充 ml307_time_t
 *
 * 响应格式: +CCLK: "yy/MM/dd,hh:mm:ss±tz"
 * 其中 tz 是相对 UTC 的 1/4 小时数 (如 +8 时区 = +32)
 */
static bool parse_cclk_response(const char *response, ml307_time_t *time)
{
    if (response == NULL || time == NULL) return false;

    memset(time, 0, sizeof(ml307_time_t));
    time->valid = false;

    const char *start = strstr(response, "+CCLK:");
    if (start == NULL) return false;

    const char *q1 = strchr(start, '"');
    if (q1 == NULL) return false;
    q1++;

    const char *q2 = strchr(q1, '"');
    if (q2 == NULL) return false;

    char ts[32];
    size_t len = (size_t)(q2 - q1);
    if (len >= sizeof(ts)) return false;
    strncpy(ts, q1, len);
    ts[len] = '\0';

    int yy, MM, dd, hh, mm, ss, tz = 0;
    char tz_sign = '+';
    int matched = sscanf(ts, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                         &yy, &MM, &dd, &hh, &mm, &ss, &tz_sign, &tz);
    if (matched < 6) {
        ESP_LOGE(TAG, "CCLK 解析失败: %s (matched=%d)", ts, matched);
        return false;
    }

    // 将 UTC + 时区偏移 转换为本地时间
    // tz 单位是 1/4 小时
    int tz_total_min = (tz_sign == '-') ? -(tz * 15) : (tz * 15);
    int total_min    = hh * 60 + mm + tz_total_min;

    // 处理跨日
    int day_offset = 0;
    while (total_min < 0)      { total_min += 1440; day_offset--; }
    while (total_min >= 1440)  { total_min -= 1440; day_offset++; }

    time->year   = 2000 + yy;
    time->month  = (uint8_t)MM;
    time->day    = (uint8_t)(dd + day_offset);   // 粗略处理，跨月不修正
    time->hour   = (uint8_t)(total_min / 60);
    time->minute = (uint8_t)(total_min % 60);
    time->second = (uint8_t)ss;
    time->valid  = true;

    ESP_LOGI(TAG, "✅ CCLK 解析: %04d-%02d-%02d %02d:%02d:%02d",
             time->year, time->month, time->day,
             time->hour, time->minute, time->second);
    return true;
}

/**
 * @brief 将 ml307_time_t 写入 ESP32 RTC (settimeofday)
 */
static bool set_rtc_from_time(const ml307_time_t *t)
{
    if (t == NULL || !t->valid) return false;

    struct tm tm_info = {};
    tm_info.tm_year = t->year - 1900;
    tm_info.tm_mon  = t->month - 1;
    tm_info.tm_mday = t->day;
    tm_info.tm_hour = t->hour;
    tm_info.tm_min  = t->minute;
    tm_info.tm_sec  = t->second;
    tm_info.tm_isdst = -1;

    time_t epoch = mktime(&tm_info);
    if (epoch == (time_t)-1) {
        ESP_LOGE(TAG, "mktime 失败");
        return false;
    }

    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    if (settimeofday(&tv, NULL) != 0) {
        ESP_LOGE(TAG, "settimeofday 失败");
        return false;
    }

    ESP_LOGI(TAG, "✅ RTC 已更新: %02d:%02d:%02d", t->hour, t->minute, t->second);
    return true;
}

/**
 * @brief 从 ESP32 RTC 读取当前时间
 */
static bool read_rtc(ml307_time_t *out)
{
    if (!rtc_synced_) {
        out->valid = false;
        return false;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm_info;
    localtime_r(&tv.tv_sec, &tm_info);

    out->year   = (uint16_t)(tm_info.tm_year + 1900);
    out->month  = (uint8_t)(tm_info.tm_mon + 1);
    out->day    = (uint8_t)tm_info.tm_mday;
    out->hour   = (uint8_t)tm_info.tm_hour;
    out->minute = (uint8_t)tm_info.tm_min;
    out->second = (uint8_t)tm_info.tm_sec;
    out->valid  = true;
    return true;
}

/**
 * @brief 执行一次完整的云端时间同步并写入 RTC
 *
 * 步骤: AT+MNTP → 等待 → AT+CCLK? → settimeofday
 * 需要持有 at_command_mutex_
 *
 * @return true 同步且写入成功
 */
static bool do_cloud_sync(void)
{
    ntp_sync_status_ = NTP_SYNC_IN_PROGRESS;
    bool ok = false;

    // 获取 AT 互斥锁 (超时10秒)
    if (at_command_mutex_ != NULL) {
        if (xSemaphoreTake(at_command_mutex_, pdMS_TO_TICKS(10000)) != pdTRUE) {
            ESP_LOGW(TAG, "⏳ AT 忙，跳过本次同步");
            ntp_sync_status_ = NTP_SYNC_FAILED;
            return false;
        }
    }

    // 1. NTP 同步
    ESP_LOGI(TAG, "🔄 触发 NTP 同步: %s:%d", NTP_SERVER_ADDRESS, NTP_SERVER_PORT);
    if (!ml307_sync_ntp_time()) {
        ESP_LOGW(TAG, "⚠️ NTP 命令发送失败");
        ntp_sync_status_ = NTP_SYNC_FAILED;
        goto release;
    }
    // 等待模组完成 NTP 校时
    vTaskDelay(pdMS_TO_TICKS(3000));

    // 2. 读取时间
    {
        char raw[128];
        if (!ml307_get_network_time(raw, sizeof(raw))) {
            ESP_LOGW(TAG, "⚠️ AT+CCLK? 读取失败");
            ntp_sync_status_ = NTP_SYNC_FAILED;
            goto release;
        }

        ml307_time_t t;
        if (!parse_cclk_response(raw, &t)) {
            ntp_sync_status_ = NTP_SYNC_FAILED;
            goto release;
        }

        // 3. 写入 RTC
        if (set_rtc_from_time(&t)) {
            rtc_synced_     = true;
            ntp_sync_status_ = NTP_SYNC_SUCCESS;
            ok = true;
        } else {
            ntp_sync_status_ = NTP_SYNC_FAILED;
        }
    }

release:
    if (at_command_mutex_ != NULL) {
        xSemaphoreGive(at_command_mutex_);
    }
    return ok;
}

// ==================== 时间同步任务 ====================

static void time_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  时间同步任务启动 (RTC模式)");
    ESP_LOGI(TAG, "  日志间隔  : %lu ms", TIME_LOG_INTERVAL_MS);
    ESP_LOGI(TAG, "  NTP服务器 : %s:%d", NTP_SERVER_ADDRESS, NTP_SERVER_PORT);
#if TIME_MIDNIGHT_SYNC_WINDOW_MIN > 0
    ESP_LOGI(TAG, "  凌晨重同步: 00:00 ~ 00:%02d 窗口内", TIME_MIDNIGHT_SYNC_WINDOW_MIN);
#else
    ESP_LOGI(TAG, "  凌晨重同步: 禁用");
#endif
    ESP_LOGI(TAG, "========================================");

    // 等待 MQTT 模块初始化完成
    ESP_LOGI(TAG, "⏳ 等待 MQTT 模块初始化 (%lu ms)...", NTP_INITIAL_SYNC_DELAY_MS + 8000UL);
    vTaskDelay(pdMS_TO_TICKS(8000));   // 与 MQTT 任务同步，确保 at_uart_ 可用

    // ── 首次 NTP 同步 ──────────────────────────────────────
    ESP_LOGI(TAG, "⏳ 延迟 %lu ms 后进行首次 NTP 同步...", NTP_INITIAL_SYNC_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(NTP_INITIAL_SYNC_DELAY_MS));

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "🔄 首次云端时间同步...");
    if (do_cloud_sync()) {
        ESP_LOGI(TAG, "✅ 首次同步成功，RTC 已接管计时");
    } else {
        ESP_LOGW(TAG, "⚠️ 首次同步失败，等待下一次重试");
        // 首次失败时，每隔 10 秒重试，直到成功
        while (is_running_ && !rtc_synced_) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            ESP_LOGI(TAG, "🔄 重试首次时间同步...");
            do_cloud_sync();
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⏰ 开始定时读取 RTC 时间...");
    ESP_LOGI(TAG, "");

    // ── 主循环 ──────────────────────────────────────────────
    while (is_running_)
    {
        ml307_time_t now;
        if (read_rtc(&now))
        {
            // 打印时间 (仅 HH:MM:SS)
            ESP_LOGI(TAG, "%02d:%02d:%02d", now.hour, now.minute, now.second);

            // 调用用户回调
            if (time_callback_ != NULL) {
                time_callback_(&now);
            }

#if TIME_MIDNIGHT_SYNC_WINDOW_MIN > 0
            // ── 每日凌晨同步逻辑 ──────────────────────────
            // 进入窗口: hour==0 && minute < WINDOW  && 还没同步过
            // 离开窗口: minute >= WINDOW → 重置标志，下一天重来
            if (now.hour == 0 && now.minute < TIME_MIDNIGHT_SYNC_WINDOW_MIN) {
                if (!midnight_sync_done_) {
                    ESP_LOGI(TAG, "");
                    ESP_LOGI(TAG, "🌙 凌晨重同步 (00:%02d 窗口)...", now.minute);
                    if (do_cloud_sync()) {
                        ESP_LOGI(TAG, "✅ 凌晨同步成功");
                    } else {
                        ESP_LOGW(TAG, "⚠️ 凌晨同步失败，下次窗口重试");
                    }
                    midnight_sync_done_ = true;   // 无论成功失败，窗口内只尝试一次
                    ESP_LOGI(TAG, "");
                }
            } else {
                // 离开窗口 → 重置，下一天可再触发
                midnight_sync_done_ = false;
            }
#endif
        }
        else
        {
            // RTC 尚未就绪（首次同步未完成）
            ESP_LOGD(TAG, "⏳ RTC 尚未就绪");
        }

        vTaskDelay(pdMS_TO_TICKS(TIME_LOG_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "⏹️ 时间同步任务退出");
    time_task_handle_ = NULL;
    vTaskDelete(NULL);
}

// ==================== 公共 API 实现 ====================

extern "C" {

bool ml307_sntp_time_start(void)
{
    if (time_task_handle_ != NULL) {
        ESP_LOGW(TAG, "⚠️ 时间同步任务已在运行");
        return false;
    }

    // 创建 AT 互斥锁（若尚未创建）
    if (at_command_mutex_ == NULL) {
        at_command_mutex_ = xSemaphoreCreateMutex();
        if (at_command_mutex_ == NULL) {
            ESP_LOGE(TAG, "❌ 创建 AT 互斥锁失败");
            return false;
        }
    }

    is_running_          = true;
    rtc_synced_          = false;
    midnight_sync_done_  = false;
    ntp_sync_status_     = NTP_SYNC_IDLE;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ML307 时间模块配置");
    ESP_LOGI(TAG, "  日志间隔  : %lu ms", TIME_LOG_INTERVAL_MS);
    ESP_LOGI(TAG, "  NTP服务器 : %s:%d", NTP_SERVER_ADDRESS, NTP_SERVER_PORT);
    ESP_LOGI(TAG, "  计时方式  : ESP32 RTC (首次同步后)");
#if TIME_MIDNIGHT_SYNC_WINDOW_MIN > 0
    ESP_LOGI(TAG, "  凌晨重同步: 00:00~00:%02d", TIME_MIDNIGHT_SYNC_WINDOW_MIN);
#else
    ESP_LOGI(TAG, "  凌晨重同步: 禁用");
#endif
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    BaseType_t ret = xTaskCreate(
        time_sync_task,
        "ml307_time",
        TIME_TASK_STACK_SIZE,
        NULL,
        TIME_TASK_PRIORITY,
        &time_task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "❌ 创建时间任务失败");
        is_running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "✅ 时间同步任务已启动");
    return true;
}

void ml307_sntp_time_stop(void)
{
    if (time_task_handle_ == NULL) {
        ESP_LOGW(TAG, "⚠️ 时间任务未运行");
        return;
    }

    ESP_LOGI(TAG, "⏹️ 正在停止时间同步任务...");
    is_running_ = false;
    while (time_task_handle_ != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "✅ 时间同步任务已停止");
}

bool ml307_sntp_get_rtc_time(ml307_time_t *time)
{
    if (time == NULL) return false;
    return read_rtc(time);
}

bool ml307_sntp_get_time_string(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) return false;

    ml307_time_t now;
    if (!read_rtc(&now)) {
        snprintf(buffer, size, "--:--:--");
        return false;
    }

    snprintf(buffer, size, "%02d:%02d:%02d", now.hour, now.minute, now.second);
    return true;
}

ntp_sync_status_t ml307_sntp_get_sync_status(void)
{
    return ntp_sync_status_;
}

bool ml307_sntp_is_running(void)
{
    return (time_task_handle_ != NULL && is_running_);
}

bool ml307_sntp_is_rtc_synced(void)
{
    return rtc_synced_;
}

void ml307_sntp_register_callback(ml307_time_callback_t callback)
{
    time_callback_ = callback;
    if (callback != NULL) {
        ESP_LOGI(TAG, "✅ 时间回调已注册");
    } else {
        ESP_LOGI(TAG, "时间回调已取消");
    }
}

} // extern "C"
