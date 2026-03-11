/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 16:35:31
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-11 15:10:51
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\wifi_sntp_time.h
 * @Description: 时间显示模块头文件，连接上WIFI后直接调用这个即可日志显示时间
 */

#ifndef TIME_DISPLAY_H
#define TIME_DISPLAY_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动时间显示任务
     *
     * 功能：
     * - 自动连接 NTP 服务器同步时间（使用上海时区）
     * - 每 10 秒在日志中显示当前时间
     * - 格式："当前时间为 : 2026年3月8日  16:30"
     *
     * @return
     *      - ESP_OK: 启动成功
     *      - ESP_FAIL: 启动失败
     *
     * @note
     *      - 需要 WiFi 已连接才能同步时间
     *      - 如果 WiFi 未连接，会先显示默认时间，等待 WiFi 连接后自动同步
     *      - 时区设置为 CST-8（中国标准时间/上海时间）
     *
     * @example
     *      void app_main(void) {
     *          beehive_system_start();  // 启动 WiFi + MQTT
     *          time_display_start();     // 启动时间显示（一行代码）
     *      }
     */
    esp_err_t wifi_time_display_start(void);

    /**
     * @brief 停止时间显示任务
     *
     * @return
     *      - ESP_OK: 停止成功
     *      - ESP_FAIL: 停止失败
     */
    esp_err_t wifi_time_display_stop(void);

    /**
     * @brief 获取当前时间字符串
     *
     * @param buffer 字符串缓冲区
     * @param buffer_size 缓冲区大小
     *
     * @return
     *      - ESP_OK: 获取成功
     *      - ESP_FAIL: 获取失败
     *
     * @example
     *      char time_str[64];
     *      wifi_time_display_get_time_string(time_str, sizeof(time_str));
     *      ESP_LOGI(TAG, "%s", time_str);
     *      // 输出: "当前时间为 : 2026年3月8日  16:30"
     */
    esp_err_t wifi_time_display_get_time_string(char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif /* TIME_DISPLAY_H */
