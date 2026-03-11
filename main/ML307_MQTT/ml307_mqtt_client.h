/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-07 13:35:31
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 12:00:00
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\ML307_MQTT\ml307_mqtt_client.h
 * @Description: ML307 MQTT 客户端模块 - 头文件
 * 使用方法：
 * 1. 在 ml307_mqtt_config.h 中配置 MQTT 参数及分时调度参数
 * 2. 在 ml307_mqtt_client.cpp 中实现 mqtt_build_message() 和 mqtt_on_message()
 * 3. 在 main.cpp 中调用 mqtt_client_start() 启动
 */

#ifndef ML307_MQTT_CLIENT_H
#define ML307_MQTT_CLIENT_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "DHT11/bsp_dht11.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "xl9555_ir_counter.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // ==================== 用户必须实现的回调函数 ====================

    /**
     * @brief 构建要发送的 JSON 消息（用户必须实现）
     *
     * @param buffer       消息缓冲区
     * @param buffer_size  缓冲区大小
     * @param message_count 当前消息计数（从1开始递增）
     * @return 构建的消息长度；返回 0 则跳过本次发送
     */
    extern int mqtt_build_message(char *buffer, size_t buffer_size, uint32_t message_count);

    /**
     * @brief 处理接收到的 MQTT 消息（用户必须实现）
     *
     * @param topic       消息主题
     * @param payload     消息内容
     * @param payload_len 消息长度
     */
    extern void mqtt_on_message(const char *topic, const char *payload, size_t payload_len);

    // ==================== 公共 API ====================

    /**
     * @brief 启动 MQTT 客户端
     *
     * 后台任务自动完成：
     * 1. 检测 ML307 模组
     * 2. 等待网络注册
     * 3. 连接 MQTT 服务器 / 订阅主题
     * 4. 根据当前 RTC 时间自动切换高频/低频上传间隔
     *    (间隔由 ml307_mqtt_config.h 中的四个宏控制)
     *
     * @return true 启动成功
     */
    bool mqtt_client_start(void);

    /**
     * @brief 停止 MQTT 客户端
     */
    void mqtt_client_stop(void);

    /**
     * @brief 手动发送消息到发布主题
     *
     * @param message 消息内容
     * @return true 发送成功
     */
    bool mqtt_client_publish(const char *message);

    /**
     * @brief 获取 MQTT 连接状态
     */
    bool mqtt_client_is_connected(void);

    /**
     * @brief 获取已发送消息数量
     */
    uint32_t mqtt_client_get_message_count(void);

    /**
     * @brief 获取运行时生成的 MQTT Client ID 字符串
     *        格式: "BeeHive_AABBCCDDEEFF"
     *        需在 mqtt_client_start() 调用后才有效
     */
    const char *mqtt_client_get_client_id(void);

#ifdef __cplusplus
}
#endif

#endif // ML307_MQTT_CLIENT_H
