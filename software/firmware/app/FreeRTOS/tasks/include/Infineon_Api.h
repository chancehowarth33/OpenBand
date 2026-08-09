/**
 * @file Infineon_Api.h
 * @author Chance Howarth
 * @brief Aggregates the C standard library and Infineon HAL/PDL headers used
 *        across the OpenBand tasks.
 *
 * @copyright Copyright (c) 2026 Chance Howarth. MIT licensed, see LICENSE.
 */
#ifndef INFINEON_API_H
#define INFINEON_API_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "cyhal.h"
#include "cy_pdl.h"
#include "cyhal_gpio.h"
#include "cyhal_uart.h"
#include "cyhal_pwm.h"
#include "cyhal_spi.h"
#include "cyhal_i2c.h"
#include "cyhal_adc.h"
#include "cyhal_timer.h"

#include "cy_retarget_io.h"

#endif
