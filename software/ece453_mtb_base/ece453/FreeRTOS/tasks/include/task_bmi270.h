/**
 * @file task_bmi270.h
 * @brief BMI270 accelerometer and gyroscope sensor task
 *
 * Sensor is on its own I2C bus: P10_0 SDA, P10_1 SCL (SCB1)
 * I2C address: 0x68 (ADR pin pulled to GND by default)
 */
#ifndef TASK_BMI270_H
#define TASK_BMI270_H

#include "Infineon_Api.h"
#include "FreeRTOS_Api.h"

/* BMI270 Register Map */
#define BMI270_REG_CHIP_ID      0x00    /* Should read 0x24 */
#define BMI270_REG_ERR_REG      0x02
#define BMI270_REG_STATUS       0x03
#define BMI270_REG_ACC_X_LSB    0x0C
#define BMI270_REG_ACC_X_MSB    0x0D
#define BMI270_REG_ACC_Y_LSB    0x0E
#define BMI270_REG_ACC_Y_MSB    0x0F
#define BMI270_REG_ACC_Z_LSB    0x10
#define BMI270_REG_ACC_Z_MSB    0x11
#define BMI270_REG_GYR_X_LSB    0x12
#define BMI270_REG_GYR_X_MSB   0x13
#define BMI270_REG_GYR_Y_LSB    0x14
#define BMI270_REG_GYR_Y_MSB   0x15
#define BMI270_REG_GYR_Z_LSB    0x16
#define BMI270_REG_GYR_Z_MSB   0x17
#define BMI270_REG_ACC_CONF     0x40
#define BMI270_REG_ACC_RANGE    0x41
#define BMI270_REG_GYR_CONF     0x42
#define BMI270_REG_GYR_RANGE    0x43
#define BMI270_REG_INTERNAL_STATUS  0x21
#define BMI270_REG_INIT_CTRL    0x59
#define BMI270_REG_INIT_ADDR_0  0x5B
#define BMI270_REG_INIT_ADDR_1  0x5C
#define BMI270_REG_INIT_DATA    0x5E
#define BMI270_REG_PWR_CONF     0x7C
#define BMI270_REG_PWR_CTRL     0x7D
#define BMI270_REG_CMD          0x7E

/* Latest readings — read from anywhere via these globals */
extern volatile int16_t bmi270_acc_x;
extern volatile int16_t bmi270_acc_y;
extern volatile int16_t bmi270_acc_z;
extern volatile int16_t bmi270_gyr_x;
extern volatile int16_t bmi270_gyr_y;
extern volatile int16_t bmi270_gyr_z;

void task_bmi270(void *pvParameters);

#endif
