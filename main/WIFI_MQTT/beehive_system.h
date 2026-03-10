/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-08 10:26:37
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 13:54:06
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\WIFI_MQTT\beehive_system.h
 * @Description: BeeHive 系统管理模块（整合 WiFi + MQTT + 超时检测）
 */
#ifndef BEEHIVE_SYSTEM_H
#define BEEHIVE_SYSTEM_H

#include "esp_err.h"
#include "beehive_system_config.h" // 统一配置（WiFi超时、MQTT间隔等）

#ifdef __cplusplus
extern "C"
{
#endif

    /* ==================== 函数声明 ==================== */

    /**
     * @brief 一键启动整个系统
     *
     * 功能：
     * - 初始化 WiFi 配网模块
     * - 启动 WiFi 超时检测（超时自动切换 4G ML307）
     * - 启动 MQTT 数据发布任务（分时高低频策略）
     * - WiFi 连接成功后自动启动 ESP MQTT 客户端
     *
     * @return ESP_OK 成功 / ESP_FAIL 失败
     */
    esp_err_t beehive_system_start(void);

#ifdef __cplusplus
}
#endif

#endif /* BEEHIVE_SYSTEM_H */