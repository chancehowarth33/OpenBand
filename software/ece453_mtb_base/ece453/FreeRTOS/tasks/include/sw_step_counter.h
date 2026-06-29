/**
 * @file sw_step_counter.h
 * @brief Software pedometer — workaround for a BMI270 whose internal feature
 *        engine (hardware step counter) is dead but whose accelerometer works.
 *
 * Build-flag gated: only compiled/spawned when SW_STEP_COUNTER is defined.
 *   make program DEFINES=SW_STEP_COUNTER   -> program the workaround board
 *   make program                            -> normal hardware-counter build
 *
 * When active it reads the accelerometer directly over I2C at ~50 Hz, runs a
 * peak-detection step algorithm on the CM4, and writes the cumulative count
 * into health_steps (overriding the hardware counter). task_bmi270.c is NOT
 * modified.
 */
#ifndef SW_STEP_COUNTER_H
#define SW_STEP_COUNTER_H

/* Spawn the software pedometer task. Call only when SW_STEP_COUNTER is set;
 * defined as a no-op-safe symbol that simply isn't referenced otherwise. */
void sw_step_counter_initialize(void);

#endif /* SW_STEP_COUNTER_H */
