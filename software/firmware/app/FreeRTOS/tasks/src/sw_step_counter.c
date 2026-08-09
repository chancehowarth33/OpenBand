/**
 * @file sw_step_counter.c
 * @brief Software pedometer — bypass for a BMI270 with a dead feature engine.
 *
 * Diagnosed case (PCB #1): accelerometer, gyro, I2C and config-load all work,
 * but the on-die feature engine never produces output (step-activity frozen at
 * 0). That block can't be fixed in firmware, so instead of the hardware step
 * counter we read raw accel and detect steps ourselves on the CM4.
 *
 * Entirely self-contained and build-flag gated (SW_STEP_COUNTER). When the flag
 * is set, task_system_control spawns this task; it reads accel over I2C at
 * ~50 Hz, runs a magnitude / DC-removal / peak-detect pedometer, and writes the
 * cumulative count into health_steps — overriding the (always-0) hardware read
 * in task_bmi270. task_bmi270.c is left untouched.
 */

#ifdef SW_STEP_COUNTER

#include "sw_step_counter.h"
#include "task_system_control.h"   /* i2c_bus, i2c_mutex, PRINTF */
#include "task_bmi270.h"           /* BMI270_REG_ACC_X_LSB */
#include "health_algo.h"           /* health_steps */
#include <math.h>
#include <stdio.h>

/* ── Tuning (50 Hz sample rate) ──────────────────────────────────────── */
#define SWS_ADDR          0x68      /* BMI270 I2C address */
#define SWS_SAMPLE_MS     20        /* 20 ms -> 50 Hz */
#define SWS_DC_ALPHA      0.05f     /* gravity / DC tracking (slow)        */
#define SWS_AC_ALPHA      0.40f     /* AC smoothing (light)                */
#define SWS_TH_HIGH       3000.0f   /* rising threshold (~0.18 g, ±2g=16384/g) */
#define SWS_TH_LOW        1200.0f   /* hysteresis reset                    */
#define SWS_MIN_INTERVAL  12        /* >=240 ms between steps (max ~4/s)   */
#define SWS_MAX_INTERVAL  75        /* <=1.5 s keeps the walk "chain" alive */
#define SWS_WARMUP        4         /* require N rhythmic steps before counting */

/* ── 50 Hz accel read (own I2C transaction; shares the bus mutex) ───────── */
static cy_rslt_t sws_read_accel(int16_t *ax, int16_t *ay, int16_t *az)
{
    uint8_t b[6] = {0};
    uint8_t reg  = BMI270_REG_ACC_X_LSB;

    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    cy_rslt_t r = cyhal_i2c_master_write(&i2c_bus, SWS_ADDR, &reg, 1, 100, false);
    if (r == CY_RSLT_SUCCESS)
        r = cyhal_i2c_master_read(&i2c_bus, SWS_ADDR, b, 6, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);

    if (r != CY_RSLT_SUCCESS) return r;
    *ax = (int16_t)((b[1] << 8) | b[0]);
    *ay = (int16_t)((b[3] << 8) | b[2]);
    *az = (int16_t)((b[5] << 8) | b[4]);
    return CY_RSLT_SUCCESS;
}

/* ── Pedometer task ─────────────────────────────────────────────────────── */
static void sw_steps_task(void *arg)
{
    (void)arg;
    /* Let task_bmi270 finish bringing the sensor up (config load + accel on). */
    vTaskDelay(pdMS_TO_TICKS(2500));
    PRINTF("[SWSTEP] software pedometer active (BMI270 feature-engine bypass)\n");

    float    dc        = 16384.0f;  /* seed at ~1 g */
    float    ac_s      = 0.0f;      /* smoothed AC */
    bool     above     = false;     /* peak hysteresis state */
    uint32_t now       = 0;         /* sample counter (time in 20 ms ticks) */
    uint32_t last_peak = 0;
    uint32_t consec    = 0;         /* consecutive in-window peaks */
    uint32_t steps     = 0;         /* cumulative committed steps */

    for (;;) {
        int16_t ax, ay, az;
        if (sws_read_accel(&ax, &ay, &az) == CY_RSLT_SUCCESS) {
            float m  = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az);
            dc      += SWS_DC_ALPHA * (m - dc);        /* track gravity */
            float ac = m - dc;                          /* dynamic component */
            ac_s    += SWS_AC_ALPHA * (ac - ac_s);      /* smooth */

            now++;
            if (!above && ac_s > SWS_TH_HIGH) {
                above = true;                           /* rising edge = peak */
                uint32_t dt = now - last_peak;
                if (dt >= SWS_MIN_INTERVAL) {           /* not a bounce */
                    last_peak = now;
                    consec = (dt <= SWS_MAX_INTERVAL) ? (consec + 1) : 1;
                    if (consec >= SWS_WARMUP) {
                        /* commit the warm-up batch once, then one per step */
                        steps += (consec == SWS_WARMUP) ? SWS_WARMUP : 1;
                    }
                }
            } else if (above && ac_s < SWS_TH_LOW) {
                above = false;                          /* reset for next peak */
            }
        }

        /* Re-assert every loop so the hardware-counter's periodic 0 write in
         * task_bmi270 (always 0 on the broken chip) can't stick. */
        health_steps = steps;

        vTaskDelay(pdMS_TO_TICKS(SWS_SAMPLE_MS));
    }
}

void sw_step_counter_initialize(void)
{
    xTaskCreate(sw_steps_task, "SWStep",
                configMINIMAL_STACK_SIZE * 2, NULL,
                tskIDLE_PRIORITY + 1, NULL);
}

#endif /* SW_STEP_COUNTER */
