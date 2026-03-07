/**
 * @file ml307_mqtt_client.h
 * @brief ML307 MQTT 客户端模块 - 头文件
 * 
 * 使用方法：
 * 1. 在 ml307_mqtt_config.h 中配置 MQTT 参数
 * 2. 在 main.cpp 中调用 mqtt_client_start() 启动
 * 3. 可选：注册回调函数接收消息
 */

#ifndef ML307_MQTT_CLIENT_H
#define ML307_MQTT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
 * @brief 启动 MQTT 客户端（使用配置文件中的参数）
 * 
 * 这个函数会创建一个独立的后台任务来运行 MQTT 客户端
 * 所有配置参数从 ml307_mqtt_config.h 中读取
 * 
 * 任务会自动完成：
 * 1. 检测 ML307 模组
 * 2. 等待网络注册
 * 3. 连接 MQTT 服务器
 * 4. 订阅主题
 * 5. 周期性发送消息
 * 
 * @return true 启动成功, false 启动失败
 * 
 * @example
 * void app_main(void) {
 *     // 一行代码启动 MQTT 客户端
 *     mqtt_client_start();
 *     
 *     // 你的主程序代码...
 * }
 */
bool mqtt_client_start(void);

/**
 * @brief 停止 MQTT 客户端任务
 */
void mqtt_client_stop(void);

/**
 * @brief 注册消息接收回调函数
 * 
 * @param callback 回调函数指针
 * 
 * @example
 * void on_message(const char *topic, const char *payload, size_t len) {
 *     printf("收到: %.*s\n", len, payload);
 * }
 * 
 * void app_main(void) {
 *     mqtt_client_set_message_callback(on_message);
 *     mqtt_client_start();
 * }
 */
void mqtt_client_set_message_callback(mqtt_message_callback_t callback);

/**
 * @brief 发送自定义消息到 MQTT 服务器
 * 
 * @param message 消息内容（字符串）
 * @return true 发送成功, false 发送失败
 * 
 * @example
 * mqtt_client_publish("{\"temp\":25.5}");
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
