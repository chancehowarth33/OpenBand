/**
 * @file task_max30102.c
 * @brief MAX30102 heart rate and SpO2 sensor task
 *
 * Reads raw Red and IR values from the FIFO every 100ms.
 * Prints values over UART for verification.
 */
#include "task_max30102.h"
#include "task_system_control.h"
#include "health_algo.h"
#include <math.h>
#include "task_bmi270.h"   /* bmi270_acc_x/y/z — motion gating (LOW_POWER scheduler) */
#include "task_ble.h"      /* ble_connected — "app open" trigger (LOW_POWER scheduler) */

#define MAX30102_ADDR   0x57

volatile uint32_t max30102_red = 0;
volatile uint32_t max30102_ir  = 0;

/* Write a single byte to a MAX30102 register */
static cy_rslt_t max30102_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    cy_rslt_t result = cyhal_i2c_master_write(&i2c_bus, MAX30102_ADDR, buf, 2, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* Read one or more bytes starting at reg */
static cy_rslt_t max30102_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    cy_rslt_t result;
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    result = cyhal_i2c_master_write(&i2c_bus, MAX30102_ADDR, &reg, 1, 100, false);
    if (result == CY_RSLT_SUCCESS)
        result = cyhal_i2c_master_read(&i2c_bus, MAX30102_ADDR, data, len, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

static cy_rslt_t max30102_init(void)
{
    cy_rslt_t result;
    uint8_t part_id = 0;

    /* Verify chip ID */
    result = max30102_read_reg(MAX30102_REG_PART_ID, &part_id, 1);
    if (result != CY_RSLT_SUCCESS || part_id != 0x15)
    {
        PRINTF("[MAX30102] ERROR: bad part ID 0x%02X (expected 0x15)\n", part_id);
        return CY_RSLT_TYPE_ERROR;
    }
    PRINTF("[MAX30102] Found, part ID: 0x%02X\n", part_id);

    /* Reset */
    max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* FIFO: 4 sample average, FIFO rollover enabled, almost full at 17 */
    max30102_write_reg(MAX30102_REG_FIFO_CONFIG, 0x4F);

    /* SpO2 mode (Red + IR). Kept even in LOW_POWER (HR-only) so the IR slot is
     * captured for the existing IR-based health_algo; the Red LED is just held
     * off. (True one-channel HR mode would change the FIFO layout.) */
    max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2);

#ifdef LOW_POWER
    /* Shorter pulse (118us / 16-bit) for less LED energy, but KEEP 100 sps so the
     * effective rate after 4x FIFO averaging stays 25 Hz — health_algo's HR math
     * assumes 25 Hz, so dropping the sample rate here would halve the reported HR.
     * The tiered scheduler manages mode/LEDs/SHDN at runtime; these are defaults. */
    max30102_write_reg(MAX30102_REG_SPO2_CONFIG, 0x25);  /* 100 sps, 118 us, 4096 */
    max30102_write_reg(MAX30102_REG_LED1_PA, 0x00);      /* Red off (scheduler enables for SpO2) */
    max30102_write_reg(MAX30102_REG_LED2_PA, 0x18);      /* IR ~4.8 mA default */
#else
    /* SpO2 config: 100 samples/sec, 411us pulse width, 4096 ADC range */
    max30102_write_reg(MAX30102_REG_SPO2_CONFIG, 0x27);

    /* LED pulse amplitudes (~7mA each) */
    max30102_write_reg(MAX30102_REG_LED1_PA, 0x24);
    max30102_write_reg(MAX30102_REG_LED2_PA, 0x24);
#endif

    /* Enable FIFO almost full interrupt */
    max30102_write_reg(MAX30102_REG_INTR_ENABLE_1, 0xC0);
    max30102_write_reg(MAX30102_REG_INTR_ENABLE_2, 0x00);

    return CY_RSLT_SUCCESS;
}

/* Drain all available FIFO samples into the globals + health_algo. */
static void max30102_drain_fifo(void)
{
    uint8_t buf[6], wr_ptr, rd_ptr, intr1;
    max30102_read_reg(MAX30102_REG_INTR_STATUS_1, &intr1, 1);
    max30102_read_reg(MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1);
    max30102_read_reg(MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1);

    uint8_t num_samples = (wr_ptr - rd_ptr) & 0x1F;
    for (uint8_t i = 0; i < num_samples; i++) {
        max30102_read_reg(MAX30102_REG_FIFO_DATA, buf, 6);
        max30102_red = ((uint32_t)(buf[0] & 0x03) << 16) | ((uint32_t)buf[1] << 8) | buf[2];
        max30102_ir  = ((uint32_t)(buf[3] & 0x03) << 16) | ((uint32_t)buf[4] << 8) | buf[5];
        health_algo_update_ppg(max30102_red, max30102_ir);
    }
}

#ifdef LOW_POWER
/* ── On-demand health scheduler (measures while the app is open) ─────────
 * HR/SpO2 are only useful when the user is looking at the app, so the sensor is
 * fully shut down (SHDN) while disconnected and only runs while a phone is
 * connected: an immediate HR+SpO2 reading the moment the app opens, then a
 * refresh on a cadence. Windows use IR-only for HR and add the Red LED for
 * SpO2, with motion-gating + adaptive IR current. Steps keep counting in the
 * BMI270 task regardless. All intervals/thresholds are tunable. */
#define SCH_POLL_MS    2000u   /* poll for the BLE connection while idle        */
#define SCH_IR_PA      0x18    /* ~4.8 mA IR drive (adaptive start)             */
#define SCH_IR_PA_HIGH 0x2A    /* ~8.4 mA IR (bump if the pulse is weak)        */
#define SCH_RED_PA     0x24    /* ~7 mA Red (SpO2)                              */
#define SCH_PI_WEAK    0.10f   /* perfusion % below this -> raise IR drive      */

/* HR/SpO2 only matter while the user is looking at the app, so the sensor runs
 * CONTINUOUSLY (live data) while a phone is connected, and is fully shut down
 * (SHDN) while disconnected. Steps keep counting in the BMI270 task either way.
 * Continuous measurement means health_hr/health_spo2 actually change each ~1 s,
 * so the BLE notify carries live values instead of repeating a stale one. */
static void max30102_scheduler(void)
{
    bool measuring = false;
    bool bumped    = false;

    max30102_write_reg(MAX30102_REG_MODE_CONFIG, 0x80);   /* start shut down */
    health_algo_set_spo2_enabled(0);

    for (;;) {
        bool conn = ble_connected;

        if (conn && !measuring) {
            /* App opened — wake for continuous live HR + SpO2 (Red + IR). */
            max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2);
            max30102_write_reg(MAX30102_REG_LED1_PA, SCH_RED_PA);   /* Red on */
            max30102_write_reg(MAX30102_REG_LED2_PA, SCH_IR_PA);    /* IR on  */
            health_algo_set_spo2_enabled(1);
            measuring = true; bumped = false;
            PRINTF("[MAX30102] app open — live HR + SpO2\n");
        } else if (!conn && measuring) {
            /* App closed — shut the sensor down and drop stale readings. */
            max30102_write_reg(MAX30102_REG_MODE_CONFIG, 0x80);     /* SHDN */
            health_algo_set_spo2_enabled(0);
            health_hr   = 0;
            health_spo2 = 0;
            measuring = false;
            PRINTF("[MAX30102] app closed — PPG off\n");
        }

        if (measuring) {
            max30102_drain_fifo();   /* continuous read -> live HR/SpO2 updates */
            /* Adaptive IR: raise the drive once if the pulse comes up weak. */
            float pi = health_algo_perfusion();
            if (!bumped && pi > 0.0f && pi < SCH_PI_WEAK) {
                max30102_write_reg(MAX30102_REG_LED2_PA, SCH_IR_PA_HIGH);
                bumped = true;
            }
            vTaskDelay(pdMS_TO_TICKS(100));   /* ~10 Hz drain */
        } else {
            vTaskDelay(pdMS_TO_TICKS(SCH_POLL_MS));
        }
    }
}
#endif /* LOW_POWER */

void task_max30102(void *pvParameters)
{
    if (max30102_init() != CY_RSLT_SUCCESS)
    {
        PRINTF("[MAX30102] Init failed, task exiting\n");
        vTaskDelete(NULL);
    }

#ifdef LOW_POWER
    PRINTF("[MAX30102] on-demand scheduler active (measures while app connected)\n");
    max30102_scheduler();   /* never returns */
#else
    while (1) {
        max30102_drain_fifo();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
#endif
}
