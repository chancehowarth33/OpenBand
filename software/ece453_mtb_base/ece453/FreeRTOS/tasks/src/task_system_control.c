/**
 * @file task_system_control.c
 * @author Joe Krachey (jkrachey@wisc.edu)
 * @brief
 * @version 0.1
 * @date 2026-04-15
 *
 * @copyright Copyright (c) 2026
 *
 */
#include "task_system_control.h"
#include "task_max30102.h"
#include "task_bmi270.h"
#include "task_ble.h"
#include "health_algo.h"
#include "sw_step_counter.h"   /* software pedometer (only built/spawned with SW_STEP_COUNTER) */
#include "ww_rtc.h"            /* wall-clock RTC (WCO-backed) */
#include "cyhal.h"
#include <stdlib.h>

/* Battery ADC — P10_0 reads the midpoint of the voltage divider
 *   R_top = RD2 = 220 kΩ  (between VBAT and P10_0)
 *   R_bot = RD1 = 100 kΩ  (between P10_0 and GND)
 *   V_bat = V_adc × (220 + 100) / 100  =  V_adc × 3.2
 *   LiPo range: 3.0 V (0%) … 4.25 V (100%)
 */
#define BAT_ADC_PIN         P10_0
#define BAT_R_TOP_KOHM      220
#define BAT_R_BOT_KOHM      100
#define BAT_VBAT_MIN_MV     3000
#define BAT_VBAT_MAX_MV     4250

static cyhal_adc_t           bat_adc;
static cyhal_adc_channel_t   bat_adc_ch;

volatile uint8_t health_battery = 0;

/* Shared I2C bus - SCB3: P6_5 SDA, P6_4 SCL                */
/* NOTE: BMI270 pull-up jumper must be cut on SparkFun board */
#define I2C_SDA_PIN     P6_5
#define I2C_SCL_PIN     P6_4
#define I2C_FREQ_HZ     400000UL    /* 400 kHz fast mode */

cyhal_i2c_t i2c_bus;
SemaphoreHandle_t i2c_mutex;
SemaphoreHandle_t print_mutex;

void system_control_initialize(void) {
    /* Start retarget-io to enable printf */
    cy_retarget_io_init(P5_1, P5_0, 115200);

    print_mutex = xSemaphoreCreateMutex();
    CY_ASSERT(print_mutex != NULL);

    PRINTF("\n\n=== WatchWise Boot ===\n");

#ifdef DEBUG_NO_SLEEP
    /* Keep the high-frequency clocks up so the debug UART stays clean. Deep
     * sleep scrambles UART output (clock transitions). Use this for firmware
     * debugging: `make program DEFINES=DEBUG_NO_SLEEP`. Normal builds deep-sleep
     * fully for battery life. */
    cyhal_syspm_lock_deepsleep();
    PRINTF("[PWR] deep sleep LOCKED (DEBUG_NO_SLEEP) — clean UART, higher power\n");
#endif

    ww_rtc_init();   /* wall clock (set later by the app over BLE) */

    cyhal_i2c_cfg_t i2c_cfg = {
        .is_slave        = false,
        .address         = 0,
        .frequencyhal_hz = I2C_FREQ_HZ
    };

    PRINTF("Initializing I2C bus...\n");
    cy_rslt_t result = cyhal_i2c_init(&i2c_bus, I2C_SDA_PIN, I2C_SCL_PIN, NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        PRINTF("I2C bus init FAILED: 0x%08lX\n", (unsigned long)result);
        return;
    }
    cyhal_i2c_configure(&i2c_bus, &i2c_cfg);
    PRINTF("I2C bus OK\n");

    i2c_mutex = xSemaphoreCreateRecursiveMutex();
    CY_ASSERT(i2c_mutex != NULL);

    /* Battery ADC init on P10_0 */
    cyhal_adc_init(&bat_adc, BAT_ADC_PIN, NULL);
    const cyhal_adc_channel_config_t adc_ch_cfg = {
        .enable_averaging   = true,
        .min_acquisition_ns = 1000,
    };
    cyhal_adc_channel_init_diff(&bat_adc_ch, &bat_adc, BAT_ADC_PIN,
                                 CYHAL_ADC_VNEG, &adc_ch_cfg);
    PRINTF("Battery ADC on P10_0 OK\n");

    PRINTF("Starting tasks...\n");
    xTaskCreate(task_system_control, "SystemControl", configMINIMAL_STACK_SIZE,     NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(task_max30102,       "MAX30102",       configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(task_bmi270,         "BMI270",         configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
#ifdef SW_STEP_COUNTER
    /* Workaround board: BMI270 feature engine is dead, so run a software
     * pedometer that overrides the (always-0) hardware step count.
     * Enabled with: make program DEFINES=SW_STEP_COUNTER */
    sw_step_counter_initialize();
#endif
    ble_task_initialize();
}


/* FreeRTOS hook — fires when pvPortMalloc() fails (heap exhausted) */
void vApplicationMallocFailedHook(void)
{
    PRINTF("[HEAP] pvPortMalloc FAILED — heap exhausted!\n");
    taskDISABLE_INTERRUPTS();
    CY_HALT();
}

void task_system_control(void *pvParameters) {
    while (1) {
        /* Read battery voltage from voltage divider on P10_0.
         * cyhal_adc_read_uv() returns the pin voltage in microvolts (VREF=VDDA=3.3V).
         * V_bat = V_adc * (R_top + R_bot) / R_bot = V_adc * 3.2
         */
        int32_t v_adc_uv = cyhal_adc_read_uv(&bat_adc_ch);
        if (v_adc_uv > 0) {
            int32_t v_bat_mv = (int32_t)((int64_t)v_adc_uv
                               * (BAT_R_TOP_KOHM + BAT_R_BOT_KOHM)
                               / BAT_R_BOT_KOHM / 1000);
            int32_t pct = (v_bat_mv - BAT_VBAT_MIN_MV) * 100
                          / (BAT_VBAT_MAX_MV - BAT_VBAT_MIN_MV);
            if (pct < 0)   pct = 0;
            if (pct > 100) pct = 100;
            health_battery = (uint8_t)pct;
        }

        if (health_hr > 0) {
            PRINTF("=== Health === HR: %u BPM | SpO2: %u%% | Steps: %u | Bat: %u%%\n",
                   health_hr, health_spo2, (unsigned)health_steps, health_battery);
        } else {
            PRINTF("=== Health === No finger detected | Steps: %u | Bat: %u%%\n",
                   (unsigned)health_steps, health_battery);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
