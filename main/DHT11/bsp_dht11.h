#ifndef _BSP_DHT11_H_
#define _BSP_DHT11_H_

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#define uchar unsigned char
#define uint8 unsigned char
#define uint16 unsigned short

/**************引脚修改此处****************/

#define GPIO_DHT11 5

/**************任务配置****************/
#define DHT11_TASK_STACK_SIZE 2048
#define DHT11_TASK_PRIORITY 5
#define DHT11_READ_INTERVAL_MS 5000 // 读取间隔，单位ms

void Delay_ms(uint16 ms);
void DHT11(void); // 温湿传感启动

// 任务控制接口
void dht11_task_start(void);   // 启动DHT11任务
void dht11_task_stop(void);    // 停止DHT11任务
void dht11_task_suspend(void); // 暂停DHT11任务
void dht11_task_resume(void);  // 恢复DHT11任务

// 获取温湿度数据接口（用于其他模块获取数据）
float dht11_get_temperature(void);
float dht11_get_humidity(void);

#endif