/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-05 17:26:28
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 01:17:33
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\DHT11\bsp_dht11.c
 * @Description: DHT11温湿度传感器驱动，提供读取温湿度的接口，并定时输出日志
 */

#include "bsp_dht11.h"
#include <stdio.h>
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include <stdio.h>
#include "driver/uart.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

float temperature = 0;
float humidity = 0;

// FreeRTOS任务句柄
static TaskHandle_t dht11_task_handle = NULL;

// 任务控制标志
static volatile bool task_running = false;

void Delay_ms(uint16 ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

// 温湿度定义
uchar ucharFLAG, uchartemp;
float Humi, Temp;
uchar ucharT_data_H, ucharT_data_L, ucharRH_data_H, ucharRH_data_L, ucharcheckdata;
uchar ucharT_data_H_temp, ucharT_data_L_temp, ucharRH_data_H_temp, ucharRH_data_L_temp, ucharcheckdata_temp;
uchar ucharcomdata;

uchar Humi_small;
uchar Temp_small;

static void InputInitial(void) // 设置端口为输入
{
    esp_rom_gpio_pad_select_gpio(GPIO_DHT11);
    gpio_set_direction(GPIO_DHT11, GPIO_MODE_INPUT);
}

static void OutputHigh(void) // 输出1
{
    esp_rom_gpio_pad_select_gpio(GPIO_DHT11);
    gpio_set_direction(GPIO_DHT11, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DHT11, 1);
}

static void OutputLow(void) // 输出0
{
    esp_rom_gpio_pad_select_gpio(GPIO_DHT11);
    gpio_set_direction(GPIO_DHT11, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_DHT11, 0);
}

static uint8 getData() // 读取状态
{
    return gpio_get_level(GPIO_DHT11);
}

// 读取一个字节数据
static void COM(void)
{
    uchar i;
    for (i = 0; i < 8; i++)
    {
        ucharFLAG = 2;
        // 等待IO口变低，变低后，通过延时去判断是0还是1
        while ((getData() == 0) && ucharFLAG++)
            ets_delay_us(10);
        ets_delay_us(35); // 延时35us
        uchartemp = 0;

        // 如果这个位是1，35us后，还是1，否则为0
        if (getData() == 1)
            uchartemp = 1;
        ucharFLAG = 2;

        // 等待IO口变高，变高后，表示可以读取下一位
        while ((getData() == 1) && ucharFLAG++)
            ets_delay_us(10);
        if (ucharFLAG == 1)
            break;
        ucharcomdata <<= 1;
        ucharcomdata |= uchartemp;
    }
}

void DHT11(void) // 温湿传感启动
{
    OutputLow();
    Delay_ms(19); //>18MS
    OutputHigh();
    InputInitial(); // 输入
    ets_delay_us(30);
    if (!getData()) // 表示传感器拉低总线
    {
        ucharFLAG = 2;
        // 等待总线被传感器拉高
        while ((!getData()) && ucharFLAG++)
            ets_delay_us(10);
        // 等待总线被传感器拉低
        while ((getData()) && ucharFLAG++)
            ets_delay_us(10);
        COM(); // 读取第1字节，
        ucharRH_data_H_temp = ucharcomdata;
        COM(); // 读取第2字节，
        ucharRH_data_L_temp = ucharcomdata;
        COM(); // 读取第3字节，
        ucharT_data_H_temp = ucharcomdata;
        COM(); // 读取第4字节，
        ucharT_data_L_temp = ucharcomdata;
        COM(); // 读取第5字节，
        ucharcheckdata_temp = ucharcomdata;
        OutputHigh();
        // 判断校验和是否一致
        uchartemp = (ucharT_data_H_temp + ucharT_data_L_temp + ucharRH_data_H_temp + ucharRH_data_L_temp);
        if (uchartemp == ucharcheckdata_temp)
        {
            // 校验和一致，
            ucharRH_data_H = ucharRH_data_H_temp; // 湿度高8
            ucharRH_data_L = ucharRH_data_L_temp; // 湿度低8
            ucharT_data_H = ucharT_data_H_temp;   // 温度高8
            ucharT_data_L = ucharT_data_L_temp;   // 温度低8
            ucharcheckdata = ucharcheckdata_temp;

            // 保存温度和湿度
            Humi = ucharRH_data_H;
            Humi_small = ucharRH_data_L * 0.1;
            Humi = Humi + Humi_small;

            Temp = ucharT_data_H;
            Temp_small = ucharT_data_L * 0.1;
            Temp = Temp + Temp_small;
        }
        else
        {
            // 这里没有上限值
            // Humi = 100;
            // Temp = 100;
        }
    }
    else // 没用成功读取，返回0 也不做处理
    {
        // Humi = 0,
        // Temp = 0;
    }

    OutputHigh(); // 输出
}

// DHT11任务函数
static void dht11_task(void *pvParameters)
{
    printf("[DHT11] Task started\n");

    while (task_running)
    {
        // 读取温湿度
        DHT11();

        // 显示读取后的温度数据
        printf("[DHT11] Temperature = %.2f °C\n", Temp);
        // 显示读取后的湿度数据
        printf("[DHT11] Humidity = %.2f %%\n", Humi);

        // 延时
        Delay_ms(DHT11_READ_INTERVAL_MS);
    }

    printf("[DHT11] Task stopped\n");
    dht11_task_handle = NULL;
    vTaskDelete(NULL);
}

// 启动DHT11任务
void dht11_task_start(void)
{
    printf("========================================\n");
    printf("DHT11 Temperature & Humidity Monitor\n");
    printf("========================================\n");

    if (dht11_task_handle != NULL)
    {
        printf("[DHT11] Task already running!\n");
        return;
    }

    task_running = true;
    BaseType_t ret = xTaskCreate(
        dht11_task,
        "dht11_task",
        DHT11_TASK_STACK_SIZE,
        NULL,
        DHT11_TASK_PRIORITY,
        &dht11_task_handle);

    if (ret == pdPASS)
    {
        printf("[DHT11] Task created successfully\n");
    }
    else
    {
        printf("[DHT11] Failed to create task\n");
        task_running = false;
    }
}

// 停止DHT11任务
void dht11_task_stop(void)
{
    if (dht11_task_handle == NULL)
    {
        printf("[DHT11] Task is not running\n");
        return;
    }

    task_running = false;
    printf("[DHT11] Stopping task...\n");
    // 等待任务自行退出
    vTaskDelay(pdMS_TO_TICKS(100));
}

// 暂停DHT11任务
void dht11_task_suspend(void)
{
    if (dht11_task_handle == NULL)
    {
        printf("[DHT11] Task is not running\n");
        return;
    }

    vTaskSuspend(dht11_task_handle);
    printf("[DHT11] Task suspended\n");
}

// 恢复DHT11任务
void dht11_task_resume(void)
{
    if (dht11_task_handle == NULL)
    {
        printf("[DHT11] Task is not running\n");
        return;
    }

    vTaskResume(dht11_task_handle);
    printf("[DHT11] Task resumed\n");
}

// 获取温度数据
float dht11_get_temperature(void)
{
    return Temp;
}

// 获取湿度数据
float dht11_get_humidity(void)
{
    return Humi;
}