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

/* Shared I2C bus - SCB3: P6_5 SDA, P6_4 SCL                */
/* NOTE: BMI270 pull-up jumper must be cut on SparkFun board */
#define I2C_SDA_PIN     P6_5
#define I2C_SCL_PIN     P6_4
#define I2C_FREQ_HZ     400000UL    /* 400 kHz fast mode */

cyhal_i2c_t i2c_bus;
SemaphoreHandle_t i2c_mutex;

void system_control_initialize(void) {
    /* Start retarget-io to enable printf */
    cy_retarget_io_init(P5_1, P5_0, 115200);
    printf("\n\n=== WatchWise Boot ===\n");

    cyhal_i2c_cfg_t i2c_cfg = {
        .is_slave        = false,
        .address         = 0,
        .frequencyhal_hz = I2C_FREQ_HZ
    };

    printf("Initializing I2C bus...\n");
    cy_rslt_t result = cyhal_i2c_init(&i2c_bus, I2C_SDA_PIN, I2C_SCL_PIN, NULL);
    if (result != CY_RSLT_SUCCESS)
    {
        printf("I2C bus init FAILED: 0x%08lX\n", result);
        return;
    }
    cyhal_i2c_configure(&i2c_bus, &i2c_cfg);
    printf("I2C bus OK\n");

    i2c_mutex = xSemaphoreCreateRecursiveMutex();
    CY_ASSERT(i2c_mutex != NULL);

    printf("Starting tasks...\n");
    xTaskCreate(task_system_control, "SystemControl", configMINIMAL_STACK_SIZE,     NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(task_max30102,       "MAX30102",       configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(task_bmi270,         "BMI270",         configMINIMAL_STACK_SIZE * 2, NULL, tskIDLE_PRIORITY + 1, NULL);
    ble_task_initialize();
}


void task_system_control(void *pvParameters) {
    while (1) {
        if (health_hr > 0) {
            printf("=== Health === HR: %u BPM | SpO2: %u%% | Steps: %lu\n",
                   health_hr, health_spo2, health_steps);
        } else {
            printf("=== Health === No finger detected | Steps: %lu\n",
                   health_steps);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
