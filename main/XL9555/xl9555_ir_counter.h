/*
 * @Author: Stathill星丘 && cishaxiatian@gmail.com
 * @Date: 2026-03-06 13:33:32
 * @LastEditors: Stathill星丘 && cishaxiatian@gmail.com
 * @LastEditTime: 2026-03-08 11:27:52
 * @FilePath: \BeeHive_Vscode_4G\main\XL9555\xl9555_ir_counter.h
 * @Description: XL9555 16路红外计数模块 —— INT中断驱动，头文件
 * XL9555 16路红外计数模块 —— INT中断驱动
 *
 * 使用方法（main.c 中）:
 *   1. #include "xl9555_ir_counter.h"
 *   2. xl9555_ir_counter_init();   // 初始化硬件
 *   3. xl9555_ir_counter_start();  // 启动后台任务
 *   4. 需要时调用 xl9555_ir_counter_get() 获取计数
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

/* ==================== 硬件配置（按实际修改） ==================== */
#define XL9555_IR_SCL_IO 7
#define XL9555_IR_SDA_IO 6
#define XL9555_IR_INT_IO 8
#define XL9555_IR_I2C_PORT I2C_NUM_0
#define XL9555_IR_I2C_ADDRESS ESP_IO_EXPANDER_I2C_TCA9555_ADDRESS_000 // A0=A1=GND -> 0x20

/* ==================== 任务配置 ==================== */
#define XL9555_IR_CHANNEL_COUNT 16
#define XL9555_IR_LOG_INTERVAL_MS 5000 // 日志打印间隔，默认 5 秒
#define XL9555_IR_TASK_STACK_SIZE 4096
#define XL9555_IR_TASK_PRIORITY 5
#define XL9555_IR_TASK_CORE 0

    /* ==================== API ==================== */

    /**
     * @brief 初始化 I2C 总线与 XL9555，配置全部引脚为输入，注册 INT 中断
     *
     * @note  若工程中已初始化 I2C 总线，请使用 xl9555_ir_counter_init_with_bus()
     * @return ESP_OK 成功，其他值失败
     */
    esp_err_t xl9555_ir_counter_init(void);

    /**
     * @brief 启动后台计数任务（需先调用 init）
     *
     * @return ESP_OK 成功
     */
    esp_err_t xl9555_ir_counter_start(void);

    /**
     * @brief 获取指定通道的累计计数值
     *
     * @param channel  通道号 0~15，对应 XL9555 P00~P17
     * @param count    输出计数值
     * @return ESP_OK / ESP_ERR_INVALID_ARG
     */
    esp_err_t xl9555_ir_counter_get(uint8_t channel, uint32_t *count);

    /**
     * @brief 获取全部16路计数值（拷贝到 buf，长度须 >= 16）
     *
     * @param buf  uint32_t 数组，长度 >= XL9555_IR_CHANNEL_COUNT
     * @return ESP_OK / ESP_ERR_INVALID_ARG
     */
    esp_err_t xl9555_ir_counter_get_all(uint32_t *buf);

    /**
     * @brief 清零指定通道计数
     *
     * @param channel  通道号 0~15，传入 0xFF 清零全部通道
     * @return ESP_OK / ESP_ERR_INVALID_ARG
     */
    esp_err_t xl9555_ir_counter_reset(uint8_t channel);

#ifdef __cplusplus
}
#endif
