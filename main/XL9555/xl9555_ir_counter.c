/*
 * xl9555_ir_counter.c
 *
 * XL9555 16路红外计数模块 —— INT中断驱动实现
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "xl9555_ir_counter.h"

/* ==================== 内部宏 ==================== */
#define ALL_INPUT_PINS  (IO_EXPANDER_PIN_NUM_0  | IO_EXPANDER_PIN_NUM_1  | \
                         IO_EXPANDER_PIN_NUM_2  | IO_EXPANDER_PIN_NUM_3  | \
                         IO_EXPANDER_PIN_NUM_4  | IO_EXPANDER_PIN_NUM_5  | \
                         IO_EXPANDER_PIN_NUM_6  | IO_EXPANDER_PIN_NUM_7  | \
                         IO_EXPANDER_PIN_NUM_8  | IO_EXPANDER_PIN_NUM_9  | \
                         IO_EXPANDER_PIN_NUM_10 | IO_EXPANDER_PIN_NUM_11 | \
                         IO_EXPANDER_PIN_NUM_12 | IO_EXPANDER_PIN_NUM_13 | \
                         IO_EXPANDER_PIN_NUM_14 | IO_EXPANDER_PIN_NUM_15)

static const char *TAG = "XL9555_IR";

/* ==================== 内部状态 ==================== */
static esp_io_expander_handle_t s_io_expander  = NULL;
static i2c_master_bus_handle_t  s_i2c_handle   = NULL;
static SemaphoreHandle_t        s_int_semaphore = NULL;

static uint32_t s_ir_count[XL9555_IR_CHANNEL_COUNT] = {0};
static uint32_t s_last_level = 0;

/* ==================== ISR ==================== */
static void IRAM_ATTR xl9555_int_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_int_semaphore, &woken);
    portYIELD_FROM_ISR(woken);
}

/* ==================== 后台任务 ==================== */
static void ir_counter_task(void *arg)
{
    uint32_t current_level  = 0;
    uint32_t rising_edges   = 0;
    TickType_t last_log     = xTaskGetTickCount();

    ESP_LOGI(TAG, "Task started, log every %d ms", XL9555_IR_LOG_INTERVAL_MS);

    while (1) {
        /* 等待 INT 信号，超时后进入打印逻辑 */
        BaseType_t triggered = xSemaphoreTake(s_int_semaphore, pdMS_TO_TICKS(XL9555_IR_LOG_INTERVAL_MS));

        if (triggered == pdTRUE) {
            /* 读寄存器同时自动清除 XL9555 的 INT 输出 */
            esp_err_t ret = esp_io_expander_get_level(s_io_expander, ALL_INPUT_PINS, &current_level);
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "get_level failed: %s", esp_err_to_name(ret));
                continue;
            }

            /* 上升沿：上次为 0，本次为 1 */
            rising_edges = (~s_last_level & current_level) & 0xFFFF;
            if (rising_edges) {
                for (int ch = 0; ch < XL9555_IR_CHANNEL_COUNT; ch++) {
                    if (rising_edges & (1UL << ch)) {
                        s_ir_count[ch]++;
                        ESP_LOGD(TAG, "CH%02d triggered, total=%" PRIu32, ch, s_ir_count[ch]);
                    }
                }
            }
            s_last_level = current_level;
        }

        /* 定期打印汇总日志 */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_log) >= pdMS_TO_TICKS(XL9555_IR_LOG_INTERVAL_MS)) {
            last_log = now;
            ESP_LOGI(TAG, "===== IR Count Summary =====");
            for (int ch = 0; ch < XL9555_IR_CHANNEL_COUNT; ch++) {
                if (ch % 4 == 0) printf("  ");
                printf("CH%02d:%4" PRIu32 "  ", ch, s_ir_count[ch]);
                if (ch % 4 == 3) printf("\n");
            }
            printf("  Level: 0x%04" PRIX32 "\n", s_last_level);
            xl9555_ir_counter_reset(0xFF); // 重置全部通道计数
        }
    }
}

/* ==================== 公开 API 实现 ==================== */

esp_err_t xl9555_ir_counter_init(void)
{
    esp_err_t ret;

    /* 创建信号量 */
    s_int_semaphore = xSemaphoreCreateBinary();
    if (s_int_semaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create semaphore");
        return ESP_ERR_NO_MEM;
    }

    /* I2C 总线 */
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port    = XL9555_IR_I2C_PORT,
        .sda_io_num  = XL9555_IR_SDA_IO,
        .scl_io_num  = XL9555_IR_SCL_IO,
        .clk_source  = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    ret = i2c_new_master_bus(&bus_cfg, &s_i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C init OK (SCL=GPIO%d, SDA=GPIO%d)", XL9555_IR_SCL_IO, XL9555_IR_SDA_IO);

    /* XL9555 */
    ret = esp_io_expander_new_i2c_tca95xx_16bit(s_i2c_handle, XL9555_IR_I2C_ADDRESS, &s_io_expander);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "expander create failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_io_expander_set_dir(s_io_expander, ALL_INPUT_PINS, IO_EXPANDER_INPUT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_dir failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "XL9555 init OK, all 16 pins -> INPUT");

    /* 读一次清除上电 INT，记录初始电平 */
    ret = esp_io_expander_get_level(s_io_expander, ALL_INPUT_PINS, &s_last_level);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initial get_level failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "Initial level: 0x%04" PRIX32, s_last_level);

    /* INT GPIO（在 XL9555 读取完后再装载，避免误触发） */
    const gpio_config_t int_cfg = {
        .pin_bit_mask = (1ULL << XL9555_IR_INT_IO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&int_cfg));
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(XL9555_IR_INT_IO, xl9555_int_isr, NULL));
    ESP_LOGI(TAG, "INT GPIO%d registered (NEGEDGE)", XL9555_IR_INT_IO);

    return ESP_OK;
}

esp_err_t xl9555_ir_counter_start(void)
{
    if (s_io_expander == NULL || s_int_semaphore == NULL) {
        ESP_LOGE(TAG, "Not initialized, call xl9555_ir_counter_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t res = xTaskCreatePinnedToCore(
        ir_counter_task,
        "ir_counter",
        XL9555_IR_TASK_STACK_SIZE,
        NULL,
        XL9555_IR_TASK_PRIORITY,
        NULL,
        XL9555_IR_TASK_CORE
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Task create failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "IR counter task started");
    return ESP_OK;
}

esp_err_t xl9555_ir_counter_get(uint8_t channel, uint32_t *count)
{
    if (channel >= XL9555_IR_CHANNEL_COUNT || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *count = s_ir_count[channel];
    return ESP_OK;
}

esp_err_t xl9555_ir_counter_get_all(uint32_t *buf)
{
    if (buf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(buf, s_ir_count, sizeof(s_ir_count));
    return ESP_OK;
}

esp_err_t xl9555_ir_counter_reset(uint8_t channel)
{
    if (channel == 0xFF) {
        memset(s_ir_count, 0, sizeof(s_ir_count));
        ESP_LOGI(TAG, "All channels reset");
    } else if (channel < XL9555_IR_CHANNEL_COUNT) {
        s_ir_count[channel] = 0;
        ESP_LOGI(TAG, "CH%02d reset", channel);
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}
