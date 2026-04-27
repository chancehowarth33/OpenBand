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

#define MOTOR_M0_PIN_0  P10_0
#define MOTOR_M0_PIN_1  P10_1
#define MOTOR_M1_PIN_0  P10_2
#define MOTOR_M1_PIN_1  P10_3

void system_control_initialize(void) {
    // Initialize any necessary resources for the system control task
    // This could include creating queues, semaphores, or other synchronization primitives

    /* Start retarget-io to enable printf */
    cy_retarget_io_init(P5_1, P5_0, 115200);

    // This example program will configure 4 GPIO pins for motor control as
    // outputs and set them to a default state (e.g., LOW)
    cyhal_gpio_init(MOTOR_M0_PIN_0, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
    cyhal_gpio_init(MOTOR_M0_PIN_1, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
    cyhal_gpio_init(MOTOR_M1_PIN_0, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);
    cyhal_gpio_init(MOTOR_M1_PIN_1, CYHAL_GPIO_DIR_OUTPUT, CYHAL_GPIO_DRIVE_STRONG, 0);

    /* Create the system control task */
    xTaskCreate(task_system_control, "SystemControl", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);


}


void task_system_control(void *pvParameters) {
    // Main loop for the system control task

    /* Set Motor States */
    cyhal_gpio_write(MOTOR_M0_PIN_0, 1); // Set M0 Pin 0 HIGH
    cyhal_gpio_write(MOTOR_M0_PIN_1, 0); // Set M0 Pin 1 LOW
    cyhal_gpio_write(MOTOR_M1_PIN_0, 1); // Set M1 Pin 0 HIGH
    cyhal_gpio_write(MOTOR_M1_PIN_1, 0); // Set M1 Pin 1 LOW

    while (1) {
        // Perform system control operations here
        // This could include monitoring system status, managing resources, etc.
        printf("System Control Task is running...\n");
        // For demonstration purposes, we can add a delay to simulate work
        vTaskDelay(pdMS_TO_TICKS(1000)); // Delay for 1000 ms
    }
}