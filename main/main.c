#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "freertos/FreeRTOSConfig.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "DHT/bsp_dht11.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "xl9555_ir_counter.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    // 启动DHT11任务，自动定时读取并输出
    dht11_task_start();

    // 1. 初始化硬件（I2C + XL9555 + INT GPIO）
    ESP_ERROR_CHECK(xl9555_ir_counter_init());
    // 2. 启动后台计数任务（每 5 秒自动打印日志）
    ESP_ERROR_CHECK(xl9555_ir_counter_start());

    ESP_LOGI(TAG, "System ready.");
    // 3. 主循环：按需读取计数（示例：每 5 秒读一次 CH0）
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // uint32_t count = 0;
        // xl9555_ir_counter_get(0, &count);
        // ESP_LOGI(TAG, "CH00 count = %" PRIu32, count);

        // 如需清零某通道：xl9555_ir_counter_reset(0);
    }

    /*
     * 其他使用示例：
     *
     * // 暂停任务
     * dht11_task_suspend();
     * vTaskDelay(pdMS_TO_TICKS(5000));
     *
     * // 恢复任务
     * dht11_task_resume();
     * vTaskDelay(pdMS_TO_TICKS(5000));
     *
     * // 获取当前温湿度（不输出日志）
     * float temp = dht11_get_temperature();
     * float humi = dht11_get_humidity();
     * printf("Current: T=%.2f, H=%.2f\n", temp, humi);
     *
     * // 停止任务
     * dht11_task_stop();
     */
}