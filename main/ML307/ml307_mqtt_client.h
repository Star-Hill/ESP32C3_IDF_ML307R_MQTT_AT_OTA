/**
 * @file ml307_mqtt_client.h
 * @brief ML307 MQTT 客户端模块 - 头文件
 * 
 * 使用方法：
 * 1. 在 ml307_mqtt_config.h 中配置 MQTT 参数
 * 2. 在 main.cpp 中实现 mqtt_build_message() 和 mqtt_on_message()
 * 3. 在 main.cpp 中调用 mqtt_client_start() 启动
 */

#ifndef ML307_MQTT_CLIENT_H
#define ML307_MQTT_CLIENT_H

#include <stddef.h>   // 添加这个，定义 size_t
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT 消息接收回调函数类型
 * 
 * @param topic 消息主题
 * @param payload 消息内容
 * @param payload_len 消息长度
 */
typedef void (*mqtt_message_callback_t)(const char *topic, const char *payload, size_t payload_len);

/**
 * @brief 构建要发送的 JSON 消息（用户必须实现）
 * 
 * 这个函数会被定时调用，用于构建要发送的消息内容
 * 你可以在这里读取传感器数据、系统状态等，然后拼接成 JSON
 * 
 * @param buffer 消息缓冲区
 * @param buffer_size 缓冲区大小
 * @param message_count 当前消息计数（从1开始递增）
 * 
 * @return 构建的消息长度，如果返回 0 则跳过本次发送
 * 
 * @example
 * int mqtt_build_message(char *buffer, size_t buffer_size, uint32_t message_count)
 * {
 *     float temperature = read_temperature();
 *     float humidity = read_humidity();
 *     
 *     return snprintf(buffer, buffer_size,
 *                     "{\"temp\":%.1f,\"humid\":%.1f,\"count\":%lu}",
 *                     temperature, humidity, message_count);
 * }
 */
extern int mqtt_build_message(char *buffer, size_t buffer_size, uint32_t message_count);

/**
 * @brief 处理接收到的 MQTT 消息（用户必须实现）
 * 
 * 当从服务器收到消息时，这个函数会被调用
 * 你可以在这里解析 JSON、控制设备等
 * 
 * @param topic 消息主题
 * @param payload 消息内容
 * @param payload_len 消息长度
 * 
 * @example
 * void mqtt_on_message(const char *topic, const char *payload, size_t payload_len)
 * {
 *     if (strncmp(payload, "led_on", 6) == 0) {
 *         gpio_set_level(LED_GPIO, 1);
 *     }
 * }
 */
extern void mqtt_on_message(const char *topic, const char *payload, size_t payload_len);

/**
 * @brief 启动 MQTT 客户端
 * 
 * 启动一个后台任务来运行 MQTT 客户端
 * 任务会自动完成：
 * 1. 检测 ML307 模组
 * 2. 等待网络注册
 * 3. 连接 MQTT 服务器
 * 4. 订阅主题
 * 5. 定时调用 mqtt_build_message() 发送消息
 * 6. 接收消息并调用 mqtt_on_message() 处理
 * 
 * @return true 启动成功, false 启动失败
 */
bool mqtt_client_start(void);

/**
 * @brief 停止 MQTT 客户端
 */
void mqtt_client_stop(void);

/**
 * @brief 手动发送消息
 * 
 * 除了定时发送，你也可以主动调用这个函数发送消息
 * 
 * @param message 要发送的消息内容
 * @return true 发送成功, false 发送失败
 */
bool mqtt_client_publish(const char *message);

/**
 * @brief 获取 MQTT 连接状态
 * 
 * @return true 已连接, false 未连接
 */
bool mqtt_client_is_connected(void);

/**
 * @brief 获取已发送的消息数量
 * 
 * @return 消息计数
 */
uint32_t mqtt_client_get_message_count(void);

#ifdef __cplusplus
}
#endif

#endif // ML307_MQTT_CLIENT_H