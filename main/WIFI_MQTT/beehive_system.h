/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 10:26:37
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-08 13:34:34
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\WIFI_MQTT\beehive_system.h
 * @Description: BeeHive 系统管理模块（整合 WiFi + MQTT + 超时检测）
 */
#ifndef BEEHIVE_SYSTEM_H
#define BEEHIVE_SYSTEM_H

#include "esp_err.h"


#ifdef __cplusplus
extern "C"
{
#endif

/* ==================== 配置宏定义 ==================== */
// WiFi 连接超时时间（秒），默认：60 秒
#ifndef WIFI_TIMEOUT_SECONDS
#define WIFI_TIMEOUT_SECONDS 10
#endif

// WiFi 连接状态检查间隔（秒），默认：5 秒
#ifndef WIFI_CHECK_INTERVAL_SECONDS
#define WIFI_CHECK_INTERVAL_SECONDS 5
#endif

// MQTT 数据发布间隔（秒），默认：5 秒
#ifndef MQTT_PUBLISH_INTERVAL_SECONDS
#define MQTT_PUBLISH_INTERVAL_SECONDS 5
#endif

    /* ==================== 函数声明 ==================== */

    /**
     * @brief 一键启动整个系统
     *
     * 功能：
     * - 初始化 WiFi 配网模块
     * - 启动 WiFi 超时检测
     * - 启动 MQTT 数据发布任务
     * - WiFi 连接成功后自动启动 MQTT
     *
     * @return ESP_OK 成功 / ESP_FAIL 失败
     */
    esp_err_t beehive_system_start(void);

#ifdef __cplusplus
}
#endif

#endif /* BEEHIVE_SYSTEM_H */
