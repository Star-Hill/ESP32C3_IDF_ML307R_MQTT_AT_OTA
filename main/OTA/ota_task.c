/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-11 16:12:35
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-11 16:48:55
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\OTA\ota_task.c
 * @Description: OTA 任务实现文件 - 定义 OTA 相关的任务函数
 */

#include "ota_task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "http_ota_manager.h"

static const char *TAG = "ota_task";

/* ----------------------------------------------------------------
 * 内部：OTA 初始化任务
 * ---------------------------------------------------------------- */
static void ota_init_task(void *arg)
{
    (void)arg;

    http_ota_manager_config_t cfg = HTTP_OTA_MANAGER_DEFAULT_CONFIG();
    snprintf(cfg.version_url, sizeof(cfg.version_url), OTA_VERSION_URL);

    esp_err_t ret = http_ota_manager_init(&cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "http_ota_manager_init 失败: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }

    ret = http_ota_manager_check_now();
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "http_ota_manager_check_now 失败: %s", esp_err_to_name(ret));
    }

    vTaskDelete(NULL);
}

/* ----------------------------------------------------------------
 * 公开接口实现
 * ---------------------------------------------------------------- */
esp_err_t ota_task_start(void)
{
    ESP_LOGI(TAG, "🚀 收到 OTA 指令，正在创建 OTA 任务...");

    BaseType_t ret = xTaskCreate(ota_init_task,
                                 "ota_init",
                                 OTA_TASK_STACK_SIZE,
                                 NULL,
                                 OTA_TASK_PRIORITY,
                                 NULL);
    if (ret != pdPASS)
    {
        ESP_LOGE(TAG, "❌ OTA 任务创建失败");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "✅ OTA 任务已启动");
    return ESP_OK;
}