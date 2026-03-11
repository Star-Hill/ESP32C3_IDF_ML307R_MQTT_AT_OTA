/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-11 16:12:35
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-11 16:16:49
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\OTA\ota_task.h
 * @Description: OTA 任务头文件 - 声明 OTA 相关的任务函数和配置宏
 */

#pragma once

#ifdef __cplusplus
extern "C"
{
#endif

#include "esp_err.h"

/* ================================================================
 * OTA 配置宏（按需修改）
 * ================================================================ */

/** OTA version.json 地址 */
#define OTA_VERSION_URL "http://175.178.117.18:88/firmware/version.json"

/** OTA 初始化任务栈大小 */
#define OTA_TASK_STACK_SIZE (1024 * 8)

/** OTA 初始化任务优先级 */
#define OTA_TASK_PRIORITY (tskIDLE_PRIORITY + 2)

    /* ================================================================
     * 公开接口
     * ================================================================ */

    /**
     * @brief 启动 OTA 任务（可在 MQTT Start_OTA 回调中调用）
     *
     * 内部会创建一个独立 FreeRTOS 任务完成 OTA 初始化与检查，
     * 防止在事件回调/MQTT 回调中直接运行导致栈溢出。
     *
     * @return ESP_OK        任务创建成功
     * @return ESP_FAIL      任务创建失败（栈不足等）
     */
    esp_err_t ota_task_start(void);

#ifdef __cplusplus
}
#endif