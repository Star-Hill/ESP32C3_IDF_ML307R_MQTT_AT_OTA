/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-09 13:44:20
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-11 15:10:40
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\ml307_sntp_time.h
 * @Description: ML307 网络时间同步模块 - 头文件
 *
 * 工作流程:
 * 1. 启动后首次通过 AT+MNTP 做 NTP 同步，再用 AT+CCLK? 读取时间
 * 2. 将获取的时间写入 ESP32 内部 RTC (settimeofday)，之后由 RTC 接管计时
 * 3. 每隔 TIME_LOG_INTERVAL_MS 从 RTC 读取时间并用日志打印 HH:MM:SS
 * 4. 每天 00:00（宏 TIME_MIDNIGHT_SYNC_WINDOW_MIN 定义的窗口内）再次
 *    做一次云端 NTP 同步以校正 RTC 漂移，同一窗口内只同步一次
 *
 * 使用方法:
 * 1. 在 main.cpp 中调用 ml307_sntp_time_start() 启动
 * 2. 其他模块可调用 ml307_sntp_get_rtc_time() 获取当前 RTC 时间
 */

#ifndef ML307_SNTP_TIME_H
#define ML307_SNTP_TIME_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// ==================== 配置参数 ====================

/**
 * @brief 时间日志打印间隔 (毫秒)
 *
 * 每隔此时间从 RTC 读取并用日志打印一次 HH:MM:SS
 * 可修改为: 1000=1秒, 5000=5秒, 10000=10秒
 */
#define TIME_LOG_INTERVAL_MS        5000UL

/**
 * @brief 时间同步任务栈大小 (字节)
 */
#define TIME_TASK_STACK_SIZE        4096

/**
 * @brief 时间同步任务优先级
 */
#define TIME_TASK_PRIORITY          4

/**
 * @brief NTP服务器地址
 *
 * 可选: "ntp.aliyun.com", "ntp.tencent.com", "cn.pool.ntp.org"
 */
#define NTP_SERVER_ADDRESS          "ntp.aliyun.com"

/**
 * @brief NTP服务器端口
 */
#define NTP_SERVER_PORT             123

/**
 * @brief 启动后延迟多久进行首次 NTP 同步 (毫秒)
 *
 * 需要等待 MQTT 任务完成模组初始化后 at_uart_ 才可用
 */
#define NTP_INITIAL_SYNC_DELAY_MS   5000UL

/**
 * @brief 每天凌晨再同步的时间窗口 (分钟)
 *
 * 当 RTC 时间为 00:00 ~ 00:MM (MM = 此宏) 时触发日同步
 * 设置为 1 表示只在 00:00~00:01 这一分钟内触发
 * 设置为 0 禁用每日凌晨同步
 */
#define TIME_MIDNIGHT_SYNC_WINDOW_MIN  1

// ==================== 数据结构 ====================

/**
 * @brief 简化时间结构体 (从 RTC 读取)
 */
typedef struct {
    uint16_t year;    ///< 年份 (e.g. 2026)
    uint8_t  month;   ///< 月份 (1-12)
    uint8_t  day;     ///< 日期 (1-31)
    uint8_t  hour;    ///< 小时 (0-23)
    uint8_t  minute;  ///< 分钟 (0-59)
    uint8_t  second;  ///< 秒   (0-59)
    bool     valid;   ///< 时间是否有效 (RTC 已被设置)
} ml307_time_t;

/**
 * @brief NTP 同步状态
 */
typedef enum {
    NTP_SYNC_IDLE = 0,
    NTP_SYNC_IN_PROGRESS,
    NTP_SYNC_SUCCESS,
    NTP_SYNC_FAILED,
    NTP_SYNC_TIMEOUT,
    NTP_SYNC_NETWORK_ERROR
} ntp_sync_status_t;

// ==================== 公共 API ====================

/**
 * @brief 启动时间同步任务
 *
 * 内部流程:
 * 1. 等待 NTP_INITIAL_SYNC_DELAY_MS 后做首次云端同步
 * 2. 同步成功后将时间写入 ESP32 RTC
 * 3. 进入周期打印循环, 每天凌晨在窗口内触发重新同步
 *
 * @return true 启动成功
 * @note 必须在 mqtt_client_start() 之后调用，或至少等待网络就绪
 */
bool ml307_sntp_time_start(void);

/**
 * @brief 停止时间同步任务
 */
void ml307_sntp_time_stop(void);

/**
 * @brief 从 ESP32 RTC 获取当前时间
 *
 * 若 RTC 尚未被 NTP 设置过，valid 字段为 false
 *
 * @param time 输出时间结构体
 * @return true 获取成功（RTC 有效）
 */
bool ml307_sntp_get_rtc_time(ml307_time_t *time);

/**
 * @brief 获取格式化的 RTC 时间字符串
 *
 * 格式: "HH:MM:SS"
 *
 * @param buffer 输出缓冲区 (建议至少 16 字节)
 * @param size   缓冲区大小
 * @return true 成功, false RTC 未就绪
 */
bool ml307_sntp_get_time_string(char *buffer, size_t size);

/**
 * @brief 获取 NTP 同步状态
 */
ntp_sync_status_t ml307_sntp_get_sync_status(void);

/**
 * @brief 检查时间任务是否正在运行
 */
bool ml307_sntp_is_running(void);

/**
 * @brief 检查 RTC 是否已被 NTP 校准
 *
 * 首次 NTP 同步成功并写入 RTC 后返回 true
 */
bool ml307_sntp_is_rtc_synced(void);

/**
 * @brief 时间变化回调函数类型
 *
 * 每次周期打印时调用（可用于其他模块读取时间）
 */
typedef void (*ml307_time_callback_t)(const ml307_time_t *time);

/**
 * @brief 注册时间周期回调 (可选)
 *
 * @param callback 回调函数, 传 NULL 取消注册
 */
void ml307_sntp_register_callback(ml307_time_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // ML307_SNTP_TIME_H
