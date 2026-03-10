/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-09 13:44:20
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-10 01:20:27
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\TIME\ml307_sntp_time.h
 * @Description: ML307 网络时间同步模块 - 头文件
 * 功能说明:
 * 1. 自动从ML307模组获取基站时间(AT+CCLK?)
 * 2. 支持NTP时间同步(AT+MNTP)
 * 3. 后台任务定时打印时间日志
 * 4. 提供时间格式化和解析功能
 * 
 * 使用方法:
 * 1. 在main.cpp中调用 ml307_sntp_time_start() 启动时间任务
 * 2. 任务会自动每隔 TIME_LOG_INTERVAL_MS 打印时间
 * 3. 可选调用 ml307_sntp_sync_ntp() 手动触发NTP同步
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
 * 默认5秒打印一次时间
 * 可修改为: 1000=1秒, 10000=10秒, 60000=1分钟
 */
#define TIME_LOG_INTERVAL_MS        5000

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
 * @brief NTP同步超时时间 (毫秒)
 */
#define NTP_SYNC_TIMEOUT_MS         10000

/**
 * @brief 启动后延迟多久进行首次NTP同步 (毫秒)
 * 
 * 等待网络稳定后再同步NTP
 */
#define NTP_INITIAL_SYNC_DELAY_MS   5000

/**
 * @brief NTP自动同步间隔 (毫秒)
 * 
 * 默认每小时同步一次
 * 设置为0则禁用自动同步
 */
#define NTP_AUTO_SYNC_INTERVAL_MS   (60 * 60 * 1000)

// ==================== 数据结构 ====================

/**
 * @brief 时间结构体 (从ML307 AT+CCLK?解析)
 */
typedef struct {
    uint16_t year;      ///< 年份 (2000-2099)
    uint8_t  month;     ///< 月份 (1-12)
    uint8_t  day;       ///< 日期 (1-31)
    uint8_t  hour;      ///< 小时 (0-23)
    uint8_t  minute;    ///< 分钟 (0-59)
    uint8_t  second;    ///< 秒 (0-59)
    int8_t   timezone;  ///< 时区 (相对UTC的1/4小时数, 如+8时区=32)
    bool     valid;     ///< 时间是否有效
} ml307_time_t;

/**
 * @brief NTP同步状态
 */
typedef enum {
    NTP_SYNC_IDLE = 0,          ///< 未同步
    NTP_SYNC_IN_PROGRESS,       ///< 同步中
    NTP_SYNC_SUCCESS,           ///< 同步成功
    NTP_SYNC_FAILED,            ///< 同步失败
    NTP_SYNC_TIMEOUT,           ///< 同步超时
    NTP_SYNC_NETWORK_ERROR      ///< 网络错误
} ntp_sync_status_t;

// ==================== 公共API ====================

/**
 * @brief 启动时间同步任务
 * 
 * 创建后台任务,定时打印时间日志
 * 任务会自动:
 * 1. 等待MQTT客户端初始化完成(获取at_uart实例)
 * 2. 延迟后执行首次NTP同步(可选)
 * 3. 每隔 TIME_LOG_INTERVAL_MS 读取并打印时间
 * 4. 每隔 NTP_AUTO_SYNC_INTERVAL_MS 自动重新同步NTP(可选)
 * 
 * @return true 启动成功, false 启动失败
 * 
 * @note 必须在 mqtt_client_start() 之后调用,或至少等待网络就绪
 */
bool ml307_sntp_time_start(void);

/**
 * @brief 停止时间同步任务
 */
void ml307_sntp_time_stop(void);

/**
 * @brief 手动触发NTP时间同步
 * 
 * 发送 AT+MNTP 命令到ML307模组进行NTP同步
 * 
 * @return true 同步命令发送成功, false 发送失败
 * 
 * @note 这是异步操作,实际同步可能需要几秒
 *       可通过 ml307_sntp_get_sync_status() 查询同步状态
 */
bool ml307_sntp_sync_ntp(void);

/**
 * @brief 获取当前网络时间
 * 
 * 从ML307模组读取时间 (AT+CCLK?)
 * 
 * @param time 输出时间结构体
 * @return true 获取成功, false 获取失败
 * 
 * @example
 * ml307_time_t current_time;
 * if (ml307_sntp_get_time(&current_time)) {
 *     printf("%04d-%02d-%02d %02d:%02d:%02d\n",
 *            current_time.year, current_time.month, current_time.day,
 *            current_time.hour, current_time.minute, current_time.second);
 * }
 */
bool ml307_sntp_get_time(ml307_time_t *time);

/**
 * @brief 获取格式化的时间字符串
 * 
 * 返回格式: "YYYY-MM-DD HH:MM:SS"
 * 
 * @param buffer 输出缓冲区
 * @param size 缓冲区大小 (建议至少32字节)
 * @return true 成功, false 失败
 * 
 * @example
 * char time_str[32];
 * if (ml307_sntp_get_time_string(time_str, sizeof(time_str))) {
 *     printf("当前时间: %s\n", time_str);
 * }
 */
bool ml307_sntp_get_time_string(char *buffer, size_t size);

/**
 * @brief 获取NTP同步状态
 * 
 * @return ntp_sync_status_t 同步状态
 */
ntp_sync_status_t ml307_sntp_get_sync_status(void);

/**
 * @brief 获取上次NTP同步的时间戳
 * 
 * @return uint32_t 上次同步的系统tick数 (毫秒), 0表示从未同步
 */
uint32_t ml307_sntp_get_last_sync_tick(void);

/**
 * @brief 检查时间任务是否正在运行
 * 
 * @return true 运行中, false 未运行
 */
bool ml307_sntp_is_running(void);

/**
 * @brief 设置NTP服务器 (运行时修改)
 * 
 * @param server NTP服务器地址
 * @param port NTP服务器端口
 */
void ml307_sntp_set_ntp_server(const char *server, uint16_t port);

/**
 * @brief 时间变化回调函数类型
 * 
 * 当获取到新的时间时调用
 * 
 * @param time 时间结构体指针
 */
typedef void (*ml307_time_callback_t)(const ml307_time_t *time);

/**
 * @brief 注册时间变化回调 (可选)
 * 
 * 每次读取时间后都会调用此回调
 * 
 * @param callback 回调函数
 */
void ml307_sntp_register_callback(ml307_time_callback_t callback);

#ifdef __cplusplus
}
#endif

#endif // ML307_SNTP_TIME_H