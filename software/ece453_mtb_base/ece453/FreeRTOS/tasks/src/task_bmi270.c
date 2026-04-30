/**
 * @file task_bmi270.c
 * @brief BMI270 accelerometer, gyroscope, and hardware step counter task
 *
 * Step counting uses the BMI270's internal feature engine:
 *   - Feature page 6, byte 3: enable bits (0x08 = step detector, 0x10 = step counter)
 *   - Feature page 0, bytes 0-1: 16-bit step count output (polling)
 */
#include "task_bmi270.h"
#include "task_system_control.h"
#include "bmi270_config.h"
#include "health_algo.h"
#include <string.h>

#define BMI270_ADDR     0x68

volatile int16_t bmi270_acc_x = 0;
volatile int16_t bmi270_acc_y = 0;
volatile int16_t bmi270_acc_z = 0;
volatile int16_t bmi270_gyr_x = 0;
volatile int16_t bmi270_gyr_y = 0;
volatile int16_t bmi270_gyr_z = 0;

/* ── Low-level I2C helpers ─────────────────────────────────────────── */

static cy_rslt_t bmi270_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    cy_rslt_t result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, buf, 2, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

static cy_rslt_t bmi270_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    cy_rslt_t result;
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, &reg, 1, 100, false);
    if (result == CY_RSLT_SUCCESS)
        result = cyhal_i2c_master_read(&i2c_bus, BMI270_ADDR, data, len, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* Read `len` bytes from a feature page atomically (FEAT_PAGE + FEATURES) */
static cy_rslt_t bmi270_read_feat_page(uint8_t page, uint8_t *data, uint8_t len)
{
    cy_rslt_t result;
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);

    uint8_t pg_buf[2] = {BMI270_REG_FEAT_PAGE, page};
    result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, pg_buf, 2, 100, true);
    if (result == CY_RSLT_SUCCESS) {
        uint8_t reg = BMI270_REG_FEATURES;
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, &reg, 1, 100, false);
        if (result == CY_RSLT_SUCCESS)
            result = cyhal_i2c_master_read(&i2c_bus, BMI270_ADDR, data, len, 100, true);
    }

    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* Write 16 bytes to a feature page atomically */
static cy_rslt_t bmi270_write_feat_page(uint8_t page, const uint8_t *data)
{
    cy_rslt_t result;
    static uint8_t write_buf[17];   /* reg + 16 bytes — static, not stack */

    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);

    uint8_t pg_buf[2] = {BMI270_REG_FEAT_PAGE, page};
    result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, pg_buf, 2, 100, true);
    if (result == CY_RSLT_SUCCESS) {
        write_buf[0] = BMI270_REG_FEATURES;
        memcpy(&write_buf[1], data, 16);
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, write_buf, 17, 100, true);
    }

    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* ── Feature engine: step counter ──────────────────────────────────── */

/* Enable the hardware step detector + counter (page 6, byte 3, bits 3-4) */
static cy_rslt_t bmi270_enable_step_counter(void)
{
    uint8_t feat[16] = {0};

    cy_rslt_t result = bmi270_read_feat_page(6, feat, 16);
    if (result != CY_RSLT_SUCCESS) return result;

    feat[3] |= 0x18;   /* bit3 = step detector enable, bit4 = step counter enable */

    result = bmi270_write_feat_page(6, feat);
    if (result != CY_RSLT_SUCCESS) return result;

    /* Restore page to 0 so regular feature output reads work */
    bmi270_write_reg(BMI270_REG_FEAT_PAGE, 0x00);

    printf("[BMI270] Hardware step counter enabled\n");
    return CY_RSLT_SUCCESS;
}

/* Read 16-bit step count from feature page 0, bytes 0-1 */
static uint16_t bmi270_read_step_count(void)
{
    uint8_t data[2] = {0};
    bmi270_read_feat_page(0, data, 2);
    return (uint16_t)((uint16_t)data[1] << 8 | data[0]);
}

/* ── Config load ───────────────────────────────────────────────────── */

static cy_rslt_t bmi270_load_config(void)
{
    cy_rslt_t result;

    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);

    result = bmi270_write_reg(BMI270_REG_PWR_CONF, 0x00);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }
    vTaskDelay(pdMS_TO_TICKS(1));

    result = bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x00);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

    static uint8_t write_buf[257];
    uint16_t config_size = BMI270_CONFIG_SIZE;

    for (uint16_t i = 0; i < config_size; i += 256)
    {
        uint16_t word_addr = i / 2;
        uint8_t addr_buf[3] = {
            BMI270_REG_INIT_ADDR_0,
            (uint8_t)(word_addr & 0x0F),
            (uint8_t)(word_addr >> 4)
        };
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, addr_buf, 3, 100, true);
        if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

        uint16_t chunk = (i + 256 <= config_size) ? 256 : (config_size - i);
        write_buf[0] = BMI270_REG_INIT_DATA;
        memcpy(&write_buf[1], &bmi270_config_file[i], chunk);
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, write_buf, chunk + 1, 200, true);
        if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }
    }

    result = bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x01);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

    xSemaphoreGiveRecursive(i2c_mutex);

    vTaskDelay(pdMS_TO_TICKS(20));

    uint8_t status = 0;
    result = bmi270_read_reg(BMI270_REG_INTERNAL_STATUS, &status, 1);
    if (result != CY_RSLT_SUCCESS) return result;

    if ((status & 0x0F) != 0x01) {
        printf("[BMI270] Config load failed, INTERNAL_STATUS=0x%02X\n", status);
        return CY_RSLT_TYPE_ERROR;
    }

    printf("[BMI270] Config loaded OK\n");
    return CY_RSLT_SUCCESS;
}

/* ── Init ──────────────────────────────────────────────────────────── */

static cy_rslt_t bmi270_init(void)
{
    cy_rslt_t result;
    uint8_t chip_id = 0;

    result = bmi270_read_reg(BMI270_REG_CHIP_ID, &chip_id, 1);
    if (result != CY_RSLT_SUCCESS || chip_id != 0x24) {
        printf("[BMI270] ERROR: bad chip ID 0x%02X (expected 0x24)\n", chip_id);
        return CY_RSLT_TYPE_ERROR;
    }
    printf("[BMI270] Found, chip ID: 0x%02X\n", chip_id);

    result = bmi270_load_config();
    if (result != CY_RSLT_SUCCESS) return result;

    bmi270_write_reg(BMI270_REG_PWR_CTRL, 0x0E);   /* acc + gyr + temp */
    vTaskDelay(pdMS_TO_TICKS(50));

    bmi270_write_reg(BMI270_REG_ACC_CONF,  0x28);   /* 100 Hz, normal avg */
    bmi270_write_reg(BMI270_REG_ACC_RANGE, 0x00);   /* ±2 g */
    bmi270_write_reg(BMI270_REG_GYR_CONF,  0x28);   /* 100 Hz, normal mode */
    bmi270_write_reg(BMI270_REG_GYR_RANGE, 0x00);   /* ±2000 °/s */

    result = bmi270_enable_step_counter();
    if (result != CY_RSLT_SUCCESS)
        printf("[BMI270] Step counter enable failed (non-fatal)\n");

    return CY_RSLT_SUCCESS;
}

/* ── Task ──────────────────────────────────────────────────────────── */

void task_bmi270(void *pvParameters)
{
    if (bmi270_init() != CY_RSLT_SUCCESS) {
        printf("[BMI270] Init failed, task exiting\n");
        vTaskDelete(NULL);
    }

    uint8_t  buf[12];
    uint8_t  step_tick = 0;

    while (1)
    {
        /* Burst-read all 6 IMU axes (12 bytes) */
        if (bmi270_read_reg(BMI270_REG_ACC_X_LSB, buf, 12) == CY_RSLT_SUCCESS)
        {
            bmi270_acc_x = (int16_t)((buf[1]  << 8) | buf[0]);
            bmi270_acc_y = (int16_t)((buf[3]  << 8) | buf[2]);
            bmi270_acc_z = (int16_t)((buf[5]  << 8) | buf[4]);
            bmi270_gyr_x = (int16_t)((buf[7]  << 8) | buf[6]);
            bmi270_gyr_y = (int16_t)((buf[9]  << 8) | buf[8]);
            bmi270_gyr_z = (int16_t)((buf[11] << 8) | buf[10]);
        }

        /* Read hardware step count once per second (every 10 × 100 ms ticks) */
        if (++step_tick >= 10) {
            step_tick    = 0;
            health_steps = bmi270_read_step_count();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
