/**
 * @file ml307_mqtt_config.h
 * @brief ML307 MQTT 客户端配置文件
 * 
 * 在这个文件中配置所有 MQTT 相关参数
 */

#ifndef ML307_MQTT_CONFIG_H
#define ML307_MQTT_CONFIG_H

// ==================== MQTT 服务器配置 ====================


#define MQTT_BROKER_HOST    "175.178.117.18"        // MQTT 服务器地址
#define MQTT_BROKER_PORT     1883                   // MQTT 服务器端口
#define MQTT_CLIENT_ID      "BeeHive_ML307_Test"    // MQTT 客户端 ID（每个设备必须唯一）
#define MQTT_USERNAME       "ML307"                 // MQTT 用户名
#define MQTT_PASSWORD       "ML307DEMO"             // MQTT 密码

// ==================== MQTT 主题配置 ====================

// 发布主题(ESP32 发送数据到服务器)
#define MQTT_PUBLISH_TOPIC      "esp32/data"
// 订阅主题(ESP32 接收服务器的控制命令)
#define MQTT_SUBSCRIBE_TOPIC    "esp32/control"

// ==================== 定时发送配置 ====================
// 自动发送消息的间隔时间（毫秒）
// 1000 = 1秒，5000 = 5秒，60000 = 1分钟
#define MQTT_PUBLISH_INTERVAL_MS    1000

// ==================== GPIO 配置 ====================

// ML307 串口 TX 引脚
#define ML307_UART_TX_PIN       GPIO_NUM_18
// ML307 串口 RX 引脚
#define ML307_UART_RX_PIN       GPIO_NUM_19
// ML307 DTR 引脚（如果不使用设置为 GPIO_NUM_NC）
#define ML307_UART_DTR_PIN      GPIO_NUM_NC

// ==================== 高级配置 ====================
// MQTT 连接 ID（ML307 支持 0-5）
#define MQTT_CONNECTION_ID      0
// MQTT 任务栈大小（字节）
#define MQTT_TASK_STACK_SIZE    8192
// MQTT 任务优先级
#define MQTT_TASK_PRIORITY      5

// ==================== 消息内容配置 ====================

// 是否在发送的消息中包含设备名称
#define MQTT_INCLUDE_DEVICE_NAME    1
// 设备名称（如果 MQTT_INCLUDE_DEVICE_NAME = 1）
#define MQTT_DEVICE_NAME            "ESP32-ML307"
// 是否在发送的消息中包含运行时间
#define MQTT_INCLUDE_UPTIME         1

// 是否在发送的消息中包含消息计数
#define MQTT_INCLUDE_MESSAGE_COUNT  1

#endif // ML307_MQTT_CONFIG_H
