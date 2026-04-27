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

/* Declaration of system control functions for tasks */
void system_control_initialize(void);
void task_system_control(void *pvParameters); 

#endif