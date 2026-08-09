/**
 * @file health_algo.h
 * @brief Health metric algorithms: heart rate, SpO2, step count
 *
 * Heart rate + SpO2: peak detection on a 4-second IR/Red ring buffer (25 Hz).
 * Steps: threshold-based peak detection on accelerometer magnitude.
 */
#ifndef HEALTH_ALGO_H
#define HEALTH_ALGO_H

#include <stdint.h>

/* Results — read from any task */
extern volatile uint8_t  health_hr;      /* heart rate, BPM   (0 = no finger / not ready) */
extern volatile uint8_t  health_spo2;    /* SpO2, %           (0 = no finger / not ready) */
extern volatile uint32_t health_steps;   /* cumulative steps                               */
extern volatile uint8_t  health_battery; /* battery level, %  (0-100)                     */

/* Called from task_max30102 for every FIFO sample */
void health_algo_update_ppg(uint32_t red, uint32_t ir);

/* Perfusion index (AC/DC of IR, in %) from the last compute — a signal-quality
 * metric. Higher = stronger pulse. Used for adaptive LED current and to
 * quality-gate readings (mirrors how real watches reject weak/noisy windows). */
float health_algo_perfusion(void);

/* Enable/disable SpO2 computation. The Red channel is only valid when the Red
 * LED is on, so the scheduler disables SpO2 during IR-only HR windows to avoid
 * publishing garbage. Default = enabled. */
void health_algo_set_spo2_enabled(int enabled);

/* health_steps is written directly by task_bmi270 from the hardware counter */

#endif /* HEALTH_ALGO_H */
