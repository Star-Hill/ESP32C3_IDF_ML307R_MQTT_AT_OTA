/*
 * @Author: StarHill cishaxiatian@163.com
 * @Date: 2026-03-07 13:35:17
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 12:00:00
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\ML307_MQTT\ml307_mqtt_config.h
 * @Description: 修改这个文件来配置 MQTT 连接参数
 */

#ifndef ML307_MQTT_CONFIG_H
#define ML307_MQTT_CONFIG_H

#include "driver/gpio.h"

// ==================== MQTT 服务器配置 ====================
#define MQTT_BROKER_HOST "175.178.117.18"   // MQTT 服务器地址
#define MQTT_BROKER_PORT 1883               // MQTT 服务器端口
#define MQTT_CLIENT_ID "BeeHive_ML307_Test" // MQTT 客户端 ID（每个设备必须唯一）
#define MQTT_USERNAME "ML307"               // MQTT 用户名
#define MQTT_PASSWORD "ML307DEMO"           // MQTT 密码

// ==================== MQTT 主题配置 ====================
// 发布主题（ESP32 发送数据到服务器）
#define MQTT_PUBLISH_TOPIC "esp32/data"
// 订阅主题（ESP32 接收服务器的控制命令）
#define MQTT_SUBSCRIBE_TOPIC "esp32/control"

// ==================== 分时上传调度配置 ====================
//
// 高频时段: MQTT_HIGH_FREQ_START_HOUR ~ MQTT_HIGH_FREQ_END_HOUR (24小时制，含首不含尾)
// 低频时段: 其余时段
//
// 示例: 7:00–19:00 高频60秒, 其余低频3600秒
//
// 高频时段开始小时 (0–23)
#define MQTT_HIGH_FREQ_START_HOUR 7
// 高频时段结束小时 (0–23, 不含, 即到该小时的 00:00 为止)
#define MQTT_HIGH_FREQ_END_HOUR 12
// 高频时段上传间隔 (毫秒)  60000 = 60秒
#define MQTT_HIGH_FREQ_INTERVAL_MS 60000UL
// 低频时段上传间隔 (毫秒)  3600000 = 1小时
#define MQTT_LOW_FREQ_INTERVAL_MS 3600000UL

// ==================== GPIO 配置 ====================

// ML307 串口 TX 引脚（ESP32 发送数据到 ML307）
#define ML307_UART_TX_PIN GPIO_NUM_18
// ML307 串口 RX 引脚（ESP32 接收 ML307 数据）
#define ML307_UART_RX_PIN GPIO_NUM_19
// ML307 DTR 引脚（如果不使用设置为 GPIO_NUM_NC）
#define ML307_UART_DTR_PIN GPIO_NUM_NC

// ==================== 高级配置 ====================

// MQTT 连接 ID（ML307 支持 0-5）
#define MQTT_CONNECTION_ID 0

// MQTT 任务栈大小（字节）
#define MQTT_TASK_STACK_SIZE 8192

// MQTT 任务优先级
#define MQTT_TASK_PRIORITY 5

// 最大 JSON 消息长度（字节）
#define MQTT_MAX_MESSAGE_SIZE 512

#endif // ML307_MQTT_CONFIG_H
