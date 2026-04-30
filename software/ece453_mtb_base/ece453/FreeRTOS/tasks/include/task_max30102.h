/**
 * @file task_max30102.h
 * @brief MAX30102 heart rate and SpO2 sensor task
 *
 * Sensor is on its own I2C bus: P6_5 SDA, P6_4 SCL (SCB3)
 * I2C address: 0x57
 */
#ifndef TASK_MAX30102_H
#define TASK_MAX30102_H

#include "Infineon_Api.h"
#include "FreeRTOS_Api.h"

/* MAX30102 Register Map */
#define MAX30102_REG_INTR_STATUS_1  0x00
#define MAX30102_REG_INTR_STATUS_2  0x01
#define MAX30102_REG_INTR_ENABLE_1  0x02
#define MAX30102_REG_INTR_ENABLE_2  0x03
#define MAX30102_REG_FIFO_WR_PTR    0x04
#define MAX30102_REG_OVF_COUNTER    0x05
#define MAX30102_REG_FIFO_RD_PTR    0x06
#define MAX30102_REG_FIFO_DATA      0x07
#define MAX30102_REG_FIFO_CONFIG    0x08
#define MAX30102_REG_MODE_CONFIG    0x09
#define MAX30102_REG_SPO2_CONFIG    0x0A
#define MAX30102_REG_LED1_PA        0x0C    /* Red LED pulse amplitude */
#define MAX30102_REG_LED2_PA        0x0D    /* IR LED pulse amplitude */
#define MAX30102_REG_TEMP_INTR      0x1F
#define MAX30102_REG_TEMP_FRAC      0x20
#define MAX30102_REG_TEMP_CONFIG    0x21
#define MAX30102_REG_PART_ID        0xFF    /* Should read 0x15 */

/* Mode config values */
#define MAX30102_MODE_SPO2          0x03    /* Red + IR */
#define MAX30102_MODE_HR            0x02    /* IR only */
#define MAX30102_RESET              0x40

/* Latest readings — read from anywhere via these globals */
extern volatile uint32_t max30102_red;
extern volatile uint32_t max30102_ir;

void task_max30102(void *pvParameters);

#endif
