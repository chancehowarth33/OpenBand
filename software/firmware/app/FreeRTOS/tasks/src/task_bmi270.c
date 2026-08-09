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

/* "Off-wrist"/idle: set after sustained no motion, cleared on any motion.
 * Shared so other tasks can back off their poll loops (see task_system_control.h). */
volatile bool ww_off_wrist = false;

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

/* Enable the hardware step detector + counter.
 * SC_1 register = page 6 bytes[2..3] (little-endian), per BMI270 spec:
 *   bits 9:0  watermark_level   bit 11  en_detector   bit 12  en_counter
 * Setting byte 3 |= 0x18 turns on both the detector and the counter. The
 * live count is read back from the feature-engine output on page 0. */
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

    PRINTF("[BMI270] Hardware step counter enabled\n");
    return CY_RSLT_SUCCESS;
}

/* Read 16-bit step count from the feature-engine output on feature page 0,
 * bytes 0-1. This is where THIS config blob publishes the live count — NOT
 * register 0x1E (that read was a regression that always returned 0). */
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
        PRINTF("[BMI270] Config load failed, INTERNAL_STATUS=0x%02X\n", status);
        return CY_RSLT_TYPE_ERROR;
    }

    PRINTF("[BMI270] Config loaded OK\n");
    return CY_RSLT_SUCCESS;
}

/* ── Init ──────────────────────────────────────────────────────────── */

static cy_rslt_t bmi270_init(void)
{
    cy_rslt_t result;
    uint8_t chip_id = 0;

    result = bmi270_read_reg(BMI270_REG_CHIP_ID, &chip_id, 1);
    if (result != CY_RSLT_SUCCESS || chip_id != 0x24) {
        PRINTF("[BMI270] ERROR: bad chip ID 0x%02X (expected 0x24)\n", chip_id);
        return CY_RSLT_TYPE_ERROR;
    }
    PRINTF("[BMI270] Found, chip ID: 0x%02X\n", chip_id);

    /* The 8KB config blob load over I2C is flaky (it's the most demanding
     * transfer on the bus); a single bad load reports a junk INTERNAL_STATUS
     * and would otherwise kill the whole IMU task. Retry with a soft reset
     * between attempts so boot is reliable. */
    result = CY_RSLT_TYPE_ERROR;
    for (int attempt = 1; attempt <= 5; attempt++) {
        result = bmi270_load_config();
        if (result == CY_RSLT_SUCCESS) {
            if (attempt > 1)
                PRINTF("[BMI270] Config load OK on attempt %d\n", attempt);
            break;
        }
        PRINTF("[BMI270] Config load attempt %d failed — soft reset & retry\n",
               attempt);
        bmi270_write_reg(BMI270_REG_CMD, 0xB6);   /* soft reset */
        vTaskDelay(pdMS_TO_TICKS(5));             /* >2ms post-reset settle */
    }
    if (result != CY_RSLT_SUCCESS) {
        PRINTF("[BMI270] Config load failed after 5 attempts\n");
        return result;
    }

#ifdef LOW_POWER
    /* Gyro off — unused, and it draws ~0.9 mA. The step counter runs on the
     * accelerometer's feature engine, so accel-only is fully functional. */
    bmi270_write_reg(BMI270_REG_PWR_CTRL, 0x04);   /* acc only */
    vTaskDelay(pdMS_TO_TICKS(50));
    bmi270_write_reg(BMI270_REG_ACC_CONF,  0x28);   /* 100 Hz, normal avg */
    bmi270_write_reg(BMI270_REG_ACC_RANGE, 0x00);   /* ±2 g */
#else
    bmi270_write_reg(BMI270_REG_PWR_CTRL, 0x0E);   /* acc + gyr + temp */
    vTaskDelay(pdMS_TO_TICKS(50));

    bmi270_write_reg(BMI270_REG_ACC_CONF,  0x28);   /* 100 Hz, normal avg (known-working breadboard value) */
    bmi270_write_reg(BMI270_REG_ACC_RANGE, 0x00);   /* ±2 g */
    bmi270_write_reg(BMI270_REG_GYR_CONF,  0x28);   /* 100 Hz, normal mode */
    bmi270_write_reg(BMI270_REG_GYR_RANGE, 0x00);   /* ±2000 °/s */
#endif

    result = bmi270_enable_step_counter();
    if (result != CY_RSLT_SUCCESS)
        PRINTF("[BMI270] Step counter enable failed (non-fatal)\n");

    return CY_RSLT_SUCCESS;
}

/* ── Task ──────────────────────────────────────────────────────────── */

void task_bmi270(void *pvParameters)
{
    if (bmi270_init() != CY_RSLT_SUCCESS) {
        PRINTF("[BMI270] Init failed, task exiting\n");
        vTaskDelete(NULL);
    }

    uint8_t  buf[12];

    /* Off-wrist / idle detection: if no axis moves by more than MOVE_DELTA for
     * IDLE_AFTER_S seconds, declare "off-wrist" (also covers a worn-but-still
     * wrist) — other tasks then back off so the CPU deep-sleeps longer. Any
     * motion immediately returns to the active poll rate. */
    #define MOVE_DELTA    1000   /* per-axis change (~0.06 g) that counts as motion */
    #define IDLE_AFTER_S  30     /* seconds of no motion before going idle          */
    int16_t  prev_x = 0, prev_y = 0, prev_z = 0;
    uint32_t still_secs = 0;

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

        /* Motion test: any axis change above the threshold since last read. */
        int dx = bmi270_acc_x - prev_x; if (dx < 0) dx = -dx;
        int dy = bmi270_acc_y - prev_y; if (dy < 0) dy = -dy;
        int dz = bmi270_acc_z - prev_z; if (dz < 0) dz = -dz;
        prev_x = bmi270_acc_x; prev_y = bmi270_acc_y; prev_z = bmi270_acc_z;
        bool moving = (dx > MOVE_DELTA || dy > MOVE_DELTA || dz > MOVE_DELTA);

        uint32_t poll_ms = ww_off_wrist ? 4000u : 1000u;   /* current poll interval */
        if (moving) {
            still_secs = 0;
            if (ww_off_wrist) { ww_off_wrist = false; PRINTF("[BMI270] motion — active\n"); }
        } else {
            still_secs += poll_ms / 1000u;
            if (!ww_off_wrist && still_secs >= IDLE_AFTER_S) {
                ww_off_wrist = true; PRINTF("[BMI270] no motion — idle/off-wrist\n");
            }
        }

        /* Hardware step counter accumulates on-chip, so a slow read is fine. */
        health_steps = bmi270_read_step_count();

        /* Slow down when idle so the CPU can deep-sleep in longer chunks. */
        vTaskDelay(pdMS_TO_TICKS(ww_off_wrist ? 4000u : 1000u));
    }
}
