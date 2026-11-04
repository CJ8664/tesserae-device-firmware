/* Host-test stub: freertos/FreeRTOS.h (only what battery.c uses). */
#pragma once

#include <stddef.h>

typedef unsigned int TickType_t;

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
