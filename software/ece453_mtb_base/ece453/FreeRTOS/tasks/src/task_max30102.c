/**
 * @file task_max30102.c
 * @brief MAX30102 heart rate and SpO2 sensor task
 *
 * Reads raw Red and IR values from the FIFO every 100ms.
 * Prints values over UART for verification.
 */
#include "task_max30102.h"
#include "task_system_control.h"
#include "health_algo.h"

#define MAX30102_ADDR   0x57

volatile uint32_t max30102_red = 0;
volatile uint32_t max30102_ir  = 0;

/* Write a single byte to a MAX30102 register */
static cy_rslt_t max30102_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    cy_rslt_t result = cyhal_i2c_master_write(&i2c_bus, MAX30102_ADDR, buf, 2, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

/* Read one or more bytes starting at reg */
static cy_rslt_t max30102_read_reg(uint8_t reg, uint8_t *data, size_t len)
{
    cy_rslt_t result;
    xSemaphoreTakeRecursive(i2c_mutex, portMAX_DELAY);
    result = cyhal_i2c_master_write(&i2c_bus, MAX30102_ADDR, &reg, 1, 100, false);
    if (result == CY_RSLT_SUCCESS)
        result = cyhal_i2c_master_read(&i2c_bus, MAX30102_ADDR, data, len, 100, true);
    xSemaphoreGiveRecursive(i2c_mutex);
    return result;
}

static cy_rslt_t max30102_init(void)
{
    cy_rslt_t result;
    uint8_t part_id = 0;

    /* Verify chip ID */
    result = max30102_read_reg(MAX30102_REG_PART_ID, &part_id, 1);
    if (result != CY_RSLT_SUCCESS || part_id != 0x15)
    {
        printf("[MAX30102] ERROR: bad part ID 0x%02X (expected 0x15)\n", part_id);
        return CY_RSLT_TYPE_ERROR;
    }
    printf("[MAX30102] Found, part ID: 0x%02X\n", part_id);

    /* Reset */
    max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_RESET);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* FIFO: 4 sample average, FIFO rollover enabled, almost full at 17 */
    max30102_write_reg(MAX30102_REG_FIFO_CONFIG, 0x4F);

    /* SpO2 mode (Red + IR) */
    max30102_write_reg(MAX30102_REG_MODE_CONFIG, MAX30102_MODE_SPO2);

    /* SpO2 config: 100 samples/sec, 411us pulse width, 4096 ADC range */
    max30102_write_reg(MAX30102_REG_SPO2_CONFIG, 0x27);

    /* LED pulse amplitudes (~7mA each) */
    max30102_write_reg(MAX30102_REG_LED1_PA, 0x24);
    max30102_write_reg(MAX30102_REG_LED2_PA, 0x24);

    /* Enable FIFO almost full interrupt */
    max30102_write_reg(MAX30102_REG_INTR_ENABLE_1, 0xC0);
    max30102_write_reg(MAX30102_REG_INTR_ENABLE_2, 0x00);

    return CY_RSLT_SUCCESS;
}

void task_max30102(void *pvParameters)
{
    if (max30102_init() != CY_RSLT_SUCCESS)
    {
        printf("[MAX30102] Init failed, task exiting\n");
        vTaskDelete(NULL);
    }

    uint8_t buf[6];
    uint8_t wr_ptr, rd_ptr;
    uint8_t intr1;

    while (1)
    {
        /* Clear interrupt flags */
        max30102_read_reg(MAX30102_REG_INTR_STATUS_1, &intr1, 1);

        /* Check how many samples are available */
        max30102_read_reg(MAX30102_REG_FIFO_WR_PTR, &wr_ptr, 1);
        max30102_read_reg(MAX30102_REG_FIFO_RD_PTR, &rd_ptr, 1);

        uint8_t num_samples = (wr_ptr - rd_ptr) & 0x1F;

        for (uint8_t i = 0; i < num_samples; i++)
        {
            /* Each sample = 3 bytes Red + 3 bytes IR */
            max30102_read_reg(MAX30102_REG_FIFO_DATA, buf, 6);

            max30102_red = ((uint32_t)(buf[0] & 0x03) << 16) |
                           ((uint32_t)buf[1] << 8) |
                            (uint32_t)buf[2];

            max30102_ir  = ((uint32_t)(buf[3] & 0x03) << 16) |
                           ((uint32_t)buf[4] << 8) |
                            (uint32_t)buf[5];

            health_algo_update_ppg(max30102_red, max30102_ir);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
