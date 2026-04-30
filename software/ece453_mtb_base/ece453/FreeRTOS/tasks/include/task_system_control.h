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

void system_control_initialize(void);
void task_system_control(void *pvParameters);

#endif