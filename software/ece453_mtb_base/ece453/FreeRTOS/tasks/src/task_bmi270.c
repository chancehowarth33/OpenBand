/**
 * @file task_bmi270.c
 * @brief BMI270 accelerometer and gyroscope sensor task
 *
 * Reads accelerometer and gyroscope data every 100ms.
 * Prints values over UART for verification.
 *
 * Accel range:  +/- 2g  (default)
 * Gyro range:   +/- 2000 deg/s (default)
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

/* Write a single byte to a BMI270 register */
static cy_rslt_t bmi270_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    cy_rslt_t result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, buf, 2, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* Read one or more bytes starting at reg */
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

/* Load the 8192-byte firmware config blob required before any sensor data flows */
static cy_rslt_t bmi270_load_config(void)
{
    cy_rslt_t result;

    /* Hold the bus for the entire upload so MAX30102 task can't interleave.
     * bmi270_write_reg/read_reg also take this recursive mutex, so they work
     * normally while we hold it here. */
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);

    /* Disable advanced power save before touching config */
    result = bmi270_write_reg(BMI270_REG_PWR_CONF, 0x00);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }
    vTaskDelay(pdMS_TO_TICKS(1)); /* spec: 450 µs min */

    /* Signal start of config load */
    result = bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x00);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

    /* Write config in 256-byte chunks */
    static uint8_t write_buf[257]; /* reg + 256 data bytes */
    uint16_t config_size = BMI270_CONFIG_SIZE;

    for (uint16_t i = 0; i < config_size; i += 256)
    {
        /* Set the word-address for this chunk in INIT_ADDR_0/1 */
        uint16_t word_addr = i / 2;
        uint8_t addr_buf[3] = {
            BMI270_REG_INIT_ADDR_0,
            (uint8_t)(word_addr & 0x0F),
            (uint8_t)(word_addr >> 4)
        };
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, addr_buf, 3, 100, true);
        if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

        /* Write up to 256 bytes of config data to INIT_DATA */
        uint16_t chunk = (i + 256 <= config_size) ? 256 : (config_size - i);
        write_buf[0] = BMI270_REG_INIT_DATA;
        memcpy(&write_buf[1], &bmi270_config_file[i], chunk);
        result = cyhal_i2c_master_write(&i2c_bus, BMI270_ADDR, write_buf, chunk + 1, 200, true);
        if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }
    }

    /* Trigger initialization */
    result = bmi270_write_reg(BMI270_REG_INIT_CTRL, 0x01);
    if (result != CY_RSLT_SUCCESS) { xSemaphoreGiveRecursive(i2c_mutex); return result; }

    xSemaphoreGiveRecursive(i2c_mutex);

    /* Wait for internal processor to finish (~20 ms) — release bus while waiting */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Verify init completed: INTERNAL_STATUS[3:0] must be 0x01 */
    uint8_t status = 0;
    result = bmi270_read_reg(BMI270_REG_INTERNAL_STATUS, &status, 1);
    if (result != CY_RSLT_SUCCESS) return result;

    if ((status & 0x0F) != 0x01)
    {
        printf("[BMI270] Config load failed, INTERNAL_STATUS=0x%02X\n", status);
        return CY_RSLT_TYPE_ERROR;
    }

    printf("[BMI270] Config loaded OK\n");
    return CY_RSLT_SUCCESS;
}

static cy_rslt_t bmi270_init(void)
{
    cy_rslt_t result;
    uint8_t chip_id = 0;

    /* Verify chip ID */
    result = bmi270_read_reg(BMI270_REG_CHIP_ID, &chip_id, 1);
    if (result != CY_RSLT_SUCCESS || chip_id != 0x24)
    {
        printf("[BMI270] ERROR: bad chip ID 0x%02X (expected 0x24)\n", chip_id);
        return CY_RSLT_TYPE_ERROR;
    }
    printf("[BMI270] Found, chip ID: 0x%02X\n", chip_id);

    /* Load firmware config blob — required before sensors produce data */
    result = bmi270_load_config();
    if (result != CY_RSLT_SUCCESS) return result;

    /* Enable accelerometer and gyroscope */
    bmi270_write_reg(BMI270_REG_PWR_CTRL, 0x0E);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Accel: 100Hz ODR, normal avg */
    bmi270_write_reg(BMI270_REG_ACC_CONF, 0x28);

    /* Accel range: +/- 2g */
    bmi270_write_reg(BMI270_REG_ACC_RANGE, 0x00);

    /* Gyro: 100Hz ODR, normal mode */
    bmi270_write_reg(BMI270_REG_GYR_CONF, 0x28);

    /* Gyro range: +/- 2000 deg/s */
    bmi270_write_reg(BMI270_REG_GYR_RANGE, 0x00);

    return CY_RSLT_SUCCESS;
}

void task_bmi270(void *pvParameters)
{
    if (bmi270_init() != CY_RSLT_SUCCESS)
    {
        printf("[BMI270] Init failed, task exiting\n");
        vTaskDelete(NULL);
    }

    uint8_t buf[12];

    while (1)
    {
        /* Read all 6 axes in one burst (12 bytes from 0x0C) */
        cy_rslt_t result = bmi270_read_reg(BMI270_REG_ACC_X_LSB, buf, 12);

        if (result == CY_RSLT_SUCCESS)
        {
            bmi270_acc_x = (int16_t)((buf[1]  << 8) | buf[0]);
            bmi270_acc_y = (int16_t)((buf[3]  << 8) | buf[2]);
            bmi270_acc_z = (int16_t)((buf[5]  << 8) | buf[4]);
            bmi270_gyr_x = (int16_t)((buf[7]  << 8) | buf[6]);
            bmi270_gyr_y = (int16_t)((buf[9]  << 8) | buf[8]);
            bmi270_gyr_z = (int16_t)((buf[11] << 8) | buf[10]);

            health_algo_update_imu(bmi270_acc_x, bmi270_acc_y, bmi270_acc_z);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
