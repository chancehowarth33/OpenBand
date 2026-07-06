/**
 * @file task_system_control.h
 * @author Joe Krachey (jkrachey@wisc.edu)
 * @brief 
 * @version 0.1
 * @date 2026-04-15
 * 
 * @copyright Copyright (c) 2026
 * 
 */
#ifndef TASK_SYSTEM_CONTROL_H
#define TASK_SYSTEM_CONTROL_H

#include "Infineon_Api.h"
#include "FreeRTOS_Api.h"

/* Shared I2C bus - SCB3: P6_5 SDA, P6_4 SCL */
extern cyhal_i2c_t i2c_bus;

/* Recursive mutex — must be held for the duration of any I2C transaction */
extern SemaphoreHandle_t i2c_mutex;

/* Mutex protecting printf/UART — take before any printf, give after */
extern SemaphoreHandle_t print_mutex;

/* Set true by task_bmi270 after sustained no motion ("off-wrist"/idle), cleared
 * on any motion. Other tasks slow their poll loops when true so the CPU can
 * deep-sleep in longer chunks. */
extern volatile bool ww_off_wrist;

#define PRINTF(...) do { \
    xSemaphoreTake(print_mutex, portMAX_DELAY); \
    printf(__VA_ARGS__); \
    xSemaphoreGive(print_mutex); \
} while(0)

void system_control_initialize(void);
void task_system_control(void *pvParameters);

#endif