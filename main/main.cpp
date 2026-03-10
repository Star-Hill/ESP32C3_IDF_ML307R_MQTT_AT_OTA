/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-05 17:21:09
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 15:28:29
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\main.cpp
 * @Description: 主函数，启动整个系统
 */

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

#include "esp_system.h"
#include "cJSON.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "esp_wifi.h"  

//外设 温湿度+IO扩展+红外计数
#include "DHT11/bsp_dht11.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "xl9555_ir_counter.h"

extern "C" {
    #include "xn_wifi_manage.h"
}

#include "beehive_system.h"
#include "ml307_mqtt_client.h"
#include "ml307_mqtt_config.h"
#include "wifi_sntp_time.h"
#include "ml307_sntp_time.h"

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{

    // 启动DHT11任务，自动定时读取并输出
    dht11_task_start();
    // 初始化硬件（I2C + XL9555 + INT GPIO）
    ESP_ERROR_CHECK(xl9555_ir_counter_init());
    // 启动后台计数任务（每 5 秒自动打印日志）
    ESP_ERROR_CHECK(xl9555_ir_counter_start());
    ESP_LOGI(TAG, "System ready.");


    // ✨ 一行代码启动整个系统（WiFi + ML407R + MQTT + 超时检测 + 时间显示）
    beehive_system_start();


    // 主循环：可以在这里添加其他功能
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(5000));

        // 示例：读取传感器数据
        // uint32_t count = 0;
        // xl9555_ir_counter_get(0, &count);
        // ESP_LOGI(TAG, "CH00 count = %" PRIu32, count);

        // 示例：DHT11 温湿度
        // float temp = dht11_get_temperature();
        // float humi = dht11_get_humidity();
        // ESP_LOGI(TAG, "温度=%.2f, 湿度=%.2f", temp, humi);
    }
}