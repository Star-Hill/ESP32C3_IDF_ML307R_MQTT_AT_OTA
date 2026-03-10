/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @FilePath: \BeeHive_Vscode_4G_WIFI\main\beehive_system_config.h
 * @Description: BeeHive 系统统一配置文件 —— 所有可调参数集中在此管理
 *
 * 使用方法：
 *   在 beehive_system.c / wifi_sntp_time.c 顶部 include 本文件即可
 *   需要调整参数时只修改这一个文件
 */

#ifndef BEEHIVE_SYSTEM_CONFIG_H
#define BEEHIVE_SYSTEM_CONFIG_H

/* ============================================================
 *  WiFi 连接管理
 * ============================================================ */

/** WiFi 连接超时时间（秒）：超时后自动切换 4G */
#define WIFI_TIMEOUT_SECONDS            60

/** WiFi 连接状态检查间隔（秒） */
#define WIFI_CHECK_INTERVAL_SECONDS     5

/* ============================================================
 *  MQTT 分时上传策略
 *  每日以 HIGH_FREQ 时段和 LOW_FREQ 时段两段循环
 * ============================================================ */

/** 高频上传开始时刻（小时，24 小时制，含） */
#define WIFI_MQTT_HIGH_FREQ_START_HOUR  7

/** 高频上传结束时刻（小时，24 小时制，不含） */
#define WIFI_MQTT_HIGH_FREQ_END_HOUR    19

/** 高频上传间隔（毫秒）：7:00–19:00，每 60 秒上传一次 */
#define WIFI_MQTT_HIGH_FREQ_INTERVAL_MS  (60UL * 1000UL)

/** 低频上传间隔（毫秒）：19:00–次日 7:00，每 1 小时上传一次 */
#define WIFI_MQTT_LOW_FREQ_INTERVAL_MS   (3600UL * 1000UL)

/* ============================================================
 *  时间显示任务
 * ============================================================ */

/** 日志显示时间的间隔（秒） */
#define TIME_DISPLAY_INTERVAL_SECONDS   10

/* ============================================================
 *  MQTT 发布任务栈大小 / 优先级
 * ============================================================ */
#define MQTT_PUBLISH_TASK_STACK_SIZE    4096
#define MQTT_PUBLISH_TASK_PRIORITY      5

/* ============================================================
 *  WiFi 超时任务栈大小 / 优先级
 * ============================================================ */
#define WIFI_TIMEOUT_TASK_STACK_SIZE    3072
#define WIFI_TIMEOUT_TASK_PRIORITY      5

#endif /* BEEHIVE_SYSTEM_CONFIG_H */
