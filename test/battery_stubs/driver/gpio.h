/* Host-test stub: driver/gpio.h (only what battery.c uses). */
#pragma once

typedef int gpio_num_t;

typedef enum {
    GPIO_MODE_DISABLE = 0,
    GPIO_MODE_INPUT,
    GPIO_MODE_OUTPUT,
} gpio_mode_t;

int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode);
int gpio_set_level(gpio_num_t gpio_num, unsigned int level);
