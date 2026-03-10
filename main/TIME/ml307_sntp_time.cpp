/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-09 13:44:22
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 01:19:50
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\ml307_sntp_time.cpp
 * @Description: ML307 网络时间同步模块 - 实现文件 (简化时间显示版本)
 * 时间显示格式改为只显示: HH:MM:SS (例如: 16:53:23)
 */

#include "ml307_sntp_time.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"

// 引入ML307组件
#include "at_uart.h"
#include "at_modem.h"

static const char *TAG = "ML307_Time";

// ==================== 全局变量 ====================

// 任务句柄
static TaskHandle_t time_task_handle_ = NULL;

// 运行状态
static bool is_running_ = false;

// NTP同步状态
static ntp_sync_status_t ntp_sync_status_ = NTP_SYNC_IDLE;
static uint32_t last_sync_tick_ = 0;

// NTP服务器配置 (运行时可修改)
static char ntp_server_[64] = NTP_SERVER_ADDRESS;
static uint16_t ntp_port_ = NTP_SERVER_PORT;

// 时间回调函数
static ml307_time_callback_t time_callback_ = NULL;

// 事件组
static EventGroupHandle_t time_event_group_ = NULL;
#define TIME_MQTT_READY_BIT  BIT0

// AT命令互斥锁 (全局共享,避免与MQTT冲突)
static SemaphoreHandle_t at_command_mutex_ = NULL;

// ==================== 外部C++函数声明 ====================

/**
 * @brief C++桥接: 获取网络时间
 */
extern "C" bool ml307_get_network_time(char *time_str, size_t size);

/**
 * @brief C++桥接: 触发NTP同步
 */
extern "C" bool ml307_sync_ntp_time(void);

// ==================== 辅助函数 ====================

/**
 * @brief 解析 AT+CCLK? 的响应
 * 
 * 响应格式: +CCLK: "21/12/27,06:56:20+32"
 * 或者: +CCLK: "26/03/09,06:56:20+32"  (完整年份)
 */
static bool parse_cclk_response(const char *response, ml307_time_t *time)
{
    if (response == NULL || time == NULL) {
        return false;
    }

    // 初始化
    memset(time, 0, sizeof(ml307_time_t));
    time->valid = false;

    // 查找 +CCLK: 
    const char *start = strstr(response, "+CCLK:");
    if (start == NULL) {
        ESP_LOGD(TAG, "未找到 +CCLK: 标记");
        return false;
    }

    // 查找引号内的时间字符串
    const char *quote1 = strchr(start, '"');
    if (quote1 == NULL) {
        ESP_LOGD(TAG, "未找到起始引号");
        return false;
    }
    quote1++; // 跳过引号

    const char *quote2 = strchr(quote1, '"');
    if (quote2 == NULL) {
        ESP_LOGD(TAG, "未找到结束引号");
        return false;
    }

    // 复制时间字符串
    char time_str[32];
    size_t len = quote2 - quote1;
    if (len >= sizeof(time_str)) {
        ESP_LOGE(TAG, "时间字符串过长");
        return false;
    }
    strncpy(time_str, quote1, len);
    time_str[len] = '\0';

    // 解析: "yy/MM/dd,hh:mm:ss±zz"
    // 示例: "26/03/09,14:56:20+32"
    int yy, MM, dd, hh, mm, ss, zz;
    char tz_sign;

    int matched = sscanf(time_str, "%2d/%2d/%2d,%2d:%2d:%2d%c%2d",
                         &yy, &MM, &dd, &hh, &mm, &ss, &tz_sign, &zz);

    if (matched < 6) {
        ESP_LOGE(TAG, "时间格式解析失败: %s (匹配了%d个字段)", time_str, matched);
        return false;
    }

    // 填充结构体
    time->year = 2000 + yy;
    time->month = MM;
    time->day = dd;
    time->hour = hh;
    time->minute = mm;
    time->second = ss;

    // 时区处理 (±zz 表示相对UTC的1/4小时数)
    if (matched >= 8) {
        time->timezone = (tz_sign == '-') ? -zz : zz;
    } else {
        time->timezone = 0;
    }

    time->valid = true;

    ESP_LOGD(TAG, "✅ 解析成功: %04d-%02d-%02d %02d:%02d:%02d (TZ:%+d)",
             time->year, time->month, time->day,
             time->hour, time->minute, time->second,
             time->timezone);

    return true;
}

/**
 * @brief 格式化时间为字符串 (简化版 - 只显示时分秒)
 */
static int format_time_string(const ml307_time_t *time, char *buffer, size_t size)
{
    if (!time->valid) {
        return snprintf(buffer, size, "无效时间");
    }

    // 计算时区偏移 (timezone 是1/4小时单位, 如+32表示+8小时)
    int tz_hours = time->timezone / 4;
    int tz_minutes = (time->timezone % 4) * 15;
    
    // 将UTC时间转换为本地时间
    int local_hour = time->hour + tz_hours;
    int local_minute = time->minute + tz_minutes;
    
    // 处理分钟溢出
    if (local_minute >= 60) {
        local_minute -= 60;
        local_hour += 1;
    } else if (local_minute < 0) {
        local_minute += 60;
        local_hour -= 1;
    }
    
    // 处理小时溢出
    if (local_hour >= 24) {
        local_hour -= 24;
    } else if (local_hour < 0) {
        local_hour += 24;
    }

    // 只显示时分秒: HH:MM:SS
    return snprintf(buffer, size,
                    "%02d:%02d:%02d",
                    local_hour, local_minute, time->second);
}

// ==================== 内部实现函数 ====================

/**
 * @brief 从ML307读取时间 (带互斥锁保护)
 * 
 * @param time 输出时间结构体
 * @return true 成功, false 失败
 */
static bool internal_get_time(ml307_time_t *time)
{
    bool result = false;
    
    // 获取互斥锁 (超时2秒)
    if (at_command_mutex_ != NULL) {
        if (xSemaphoreTake(at_command_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE) {
            ESP_LOGW(TAG, "⏳ AT命令忙,跳过本次时间读取");
            return false;
        }
    }

    // 调用C桥接函数获取时间
    char response[128];
    if (ml307_get_network_time(response, sizeof(response))) {
        // 解析响应
        if (parse_cclk_response(response, time)) {
            result = true;
        } else {
            ESP_LOGD(TAG, "解析时间响应失败");
        }
    } else {
        ESP_LOGD(TAG, "获取网络时间失败");
    }

    // 释放互斥锁
    if (at_command_mutex_ != NULL) {
        xSemaphoreGive(at_command_mutex_);
    }

    return result;
}

/**
 * @brief 执行NTP同步 (带互斥锁保护)
 */
static bool internal_sync_ntp(void)
{
    bool result = false;

    ESP_LOGI(TAG, "🔄 开始NTP同步: %s:%d", ntp_server_, ntp_port_);

    ntp_sync_status_ = NTP_SYNC_IN_PROGRESS;

    // 获取互斥锁 (超时10秒)
    if (at_command_mutex_ != NULL) {
        if (xSemaphoreTake(at_command_mutex_, pdMS_TO_TICKS(10000)) != pdTRUE) {
            ESP_LOGW(TAG, "⏳ AT命令忙,NTP同步延后");
            ntp_sync_status_ = NTP_SYNC_FAILED;
            return false;
        }
    }

    // 调用C桥接函数
    if (ml307_sync_ntp_time()) {
        ntp_sync_status_ = NTP_SYNC_SUCCESS;
        last_sync_tick_ = xTaskGetTickCount() * portTICK_PERIOD_MS;
        ESP_LOGI(TAG, "✅ NTP同步成功");
        result = true;
    } else {
        ntp_sync_status_ = NTP_SYNC_FAILED;
        ESP_LOGW(TAG, "⚠️ NTP同步失败");
    }

    // 释放互斥锁
    if (at_command_mutex_ != NULL) {
        xSemaphoreGive(at_command_mutex_);
    }

    return result;
}

/**
 * @brief 时间同步任务主函数
 */
static void time_sync_task(void *pvParameters)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  时间同步任务启动");
    ESP_LOGI(TAG, "  打印间隔: %lu ms", TIME_LOG_INTERVAL_MS);
    ESP_LOGI(TAG, "  NTP服务器: %s:%d", ntp_server_, ntp_port_);
    ESP_LOGI(TAG, "========================================");

    // 等待MQTT模块初始化完成
    ESP_LOGI(TAG, "⏳ 等待 MQTT 模块初始化...");
    vTaskDelay(pdMS_TO_TICKS(8000));  // 延长到8秒,确保MQTT完全就绪

    // 首次NTP同步 (可选)
    #if NTP_AUTO_SYNC_INTERVAL_MS > 0
    ESP_LOGI(TAG, "⏳ 延迟 %lu ms 后进行首次NTP同步...", NTP_INITIAL_SYNC_DELAY_MS);
    vTaskDelay(pdMS_TO_TICKS(NTP_INITIAL_SYNC_DELAY_MS));
    
    internal_sync_ntp();
    vTaskDelay(pdMS_TO_TICKS(3000)); // 等待同步完成
    #endif

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "⏰ 开始定时打印时间日志...");
    ESP_LOGI(TAG, "");

    uint32_t last_ntp_sync_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
    uint32_t loop_count = 0;
    uint32_t success_count = 0;
    uint32_t fail_count = 0;

    // 主循环
    while (is_running_)
    {
        loop_count++;

        // 获取当前时间
        ml307_time_t current_time;
        if (internal_get_time(&current_time))
        {
            success_count++;
            
            // 格式化时间字符串
            char time_str[64];
            format_time_string(&current_time, time_str, sizeof(time_str));

            // 打印日志 (简化格式)
            ESP_LOGI(TAG, "%s", time_str);

            // 调用用户回调 (如果注册了)
            if (time_callback_ != NULL) {
                time_callback_(&current_time);
            }
            
            // 重置失败计数
            fail_count = 0;
        }
        else
        {
            fail_count++;
            
            // 连续失败3次才打印警告
            if (fail_count <= 3) {
                ESP_LOGD(TAG, "⚠️ [%lu] 获取时间失败 (第%lu次)", loop_count, fail_count);
            } else {
                ESP_LOGW(TAG, "⚠️ [%lu] 获取时间失败 (连续%lu次)", loop_count, fail_count);
            }
        }

        // 检查是否需要自动NTP同步
        #if NTP_AUTO_SYNC_INTERVAL_MS > 0
        uint32_t current_tick = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_tick - last_ntp_sync_tick >= NTP_AUTO_SYNC_INTERVAL_MS)
        {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "🔄 自动NTP同步 (间隔: %lu ms)", NTP_AUTO_SYNC_INTERVAL_MS);
            internal_sync_ntp();
            last_ntp_sync_tick = current_tick;
            ESP_LOGI(TAG, "");
            
            vTaskDelay(pdMS_TO_TICKS(3000)); // 等待同步完成
        }
        #endif

        // 等待下一次打印
        vTaskDelay(pdMS_TO_TICKS(TIME_LOG_INTERVAL_MS));
    }

    ESP_LOGI(TAG, "⏹️ 时间同步任务退出 (成功:%lu, 失败:%lu)", success_count, fail_count);
    time_task_handle_ = NULL;
    vTaskDelete(NULL);
}

// ==================== C接口实现 (extern "C") ====================

extern "C" {

bool ml307_sntp_time_start(void)
{
    if (time_task_handle_ != NULL) {
        ESP_LOGW(TAG, "⚠️ 时间同步任务已经在运行");
        return false;
    }

    // 创建互斥锁 (如果还没创建)
    if (at_command_mutex_ == NULL) {
        at_command_mutex_ = xSemaphoreCreateMutex();
        if (at_command_mutex_ == NULL) {
            ESP_LOGE(TAG, "❌ 创建互斥锁失败");
            return false;
        }
        ESP_LOGD(TAG, "✅ AT命令互斥锁已创建");
    }

    // 创建事件组
    if (time_event_group_ == NULL) {
        time_event_group_ = xEventGroupCreate();
    }

    // 重置状态
    is_running_ = true;
    ntp_sync_status_ = NTP_SYNC_IDLE;
    last_sync_tick_ = 0;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  ML307 时间同步模块配置");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "打印间隔: %lu ms", TIME_LOG_INTERVAL_MS);
    ESP_LOGI(TAG, "NTP服务器: %s:%d", ntp_server_, ntp_port_);
    #if NTP_AUTO_SYNC_INTERVAL_MS > 0
    ESP_LOGI(TAG, "自动同步间隔: %lu ms", NTP_AUTO_SYNC_INTERVAL_MS);
    #else
    ESP_LOGI(TAG, "自动同步: 禁用");
    #endif
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    // 创建任务
    BaseType_t ret = xTaskCreate(
        time_sync_task,
        "ml307_time",
        TIME_TASK_STACK_SIZE,
        NULL,
        TIME_TASK_PRIORITY,
        &time_task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "❌ 创建时间同步任务失败");
        is_running_ = false;
        return false;
    }

    ESP_LOGI(TAG, "✅ 时间同步任务已启动");
    return true;
}

void ml307_sntp_time_stop(void)
{
    if (time_task_handle_ == NULL) {
        ESP_LOGW(TAG, "⚠️ 时间同步任务未运行");
        return;
    }

    ESP_LOGI(TAG, "⏹️ 正在停止时间同步任务...");
    is_running_ = false;

    // 等待任务退出
    while (time_task_handle_ != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "✅ 时间同步任务已停止");
}

bool ml307_sntp_sync_ntp(void)
{
    if (!is_running_) {
        ESP_LOGW(TAG, "⚠️ 时间任务未运行");
        return false;
    }

    return internal_sync_ntp();
}

bool ml307_sntp_get_time(ml307_time_t *time)
{
    if (time == NULL) {
        return false;
    }

    return internal_get_time(time);
}

bool ml307_sntp_get_time_string(char *buffer, size_t size)
{
    if (buffer == NULL || size == 0) {
        return false;
    }

    ml307_time_t current_time;
    if (!internal_get_time(&current_time)) {
        return false;
    }

    format_time_string(&current_time, buffer, size);
    return true;
}

ntp_sync_status_t ml307_sntp_get_sync_status(void)
{
    return ntp_sync_status_;
}

uint32_t ml307_sntp_get_last_sync_tick(void)
{
    return last_sync_tick_;
}

bool ml307_sntp_is_running(void)
{
    return (time_task_handle_ != NULL && is_running_);
}

void ml307_sntp_set_ntp_server(const char *server, uint16_t port)
{
    if (server != NULL) {
        strncpy(ntp_server_, server, sizeof(ntp_server_) - 1);
        ntp_server_[sizeof(ntp_server_) - 1] = '\0';
    }
    
    if (port > 0) {
        ntp_port_ = port;
    }

    ESP_LOGI(TAG, "✅ NTP服务器已更新: %s:%d", ntp_server_, ntp_port_);
}

void ml307_sntp_register_callback(ml307_time_callback_t callback)
{
    time_callback_ = callback;
    
    if (callback != NULL) {
        ESP_LOGI(TAG, "✅ 时间回调已注册");
    } else {
        ESP_LOGI(TAG, "❌ 时间回调已取消");
    }
}

} // extern "C"

