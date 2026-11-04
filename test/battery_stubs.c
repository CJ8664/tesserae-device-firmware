/* Host-test stub implementations for the IDF surface battery.c touches.
 * Nothing here is exercised by the gauge tests -- battery_pct() is pure --
 * these exist only so the real battery.c links on a host compiler. The ADC
 * stubs report failure, so battery_sample_mv() would yield 0 mV ("unknown")
 * if a test ever called it. */
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "freertos/task.h"

int64_t esp_timer_get_time(void) { return 0; }

int gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode)
{
    (void)gpio_num; (void)mode;
    return 0;
}

int gpio_set_level(gpio_num_t gpio_num, unsigned int level)
{
    (void)gpio_num; (void)level;
    return 0;
}

void vTaskDelay(TickType_t ticks) { (void)ticks; }

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg,
                               adc_oneshot_unit_handle_t *ret_unit)
{
    (void)cfg;
    if (ret_unit) *ret_unit = NULL;
    return 1;   /* fail: no ADC on the host */
}

esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t unit)
{
    (void)unit;
    return 0;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t unit,
                                     adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *cfg)
{
    (void)unit; (void)channel; (void)cfg;
    return 1;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t unit,
                           adc_channel_t channel, int *out_raw)
{
    (void)unit; (void)channel; (void)out_raw;
    return 1;
}

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage)
{
    (void)handle; (void)raw; (void)voltage;
    return 1;
}

esp_err_t adc_cali_create_scheme_curve_fitting(
    const adc_cali_curve_fitting_config_t *cfg, adc_cali_handle_t *ret_handle)
{
    (void)cfg;
    if (ret_handle) *ret_handle = NULL;
    return 1;
}

esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle)
{
    (void)handle;
    return 0;
}

esp_err_t adc_cali_create_scheme_line_fitting(
    const adc_cali_line_fitting_config_t *cfg, adc_cali_handle_t *ret_handle)
{
    (void)cfg;
    if (ret_handle) *ret_handle = NULL;
    return 1;
}

esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t handle)
{
    (void)handle;
    return 0;
}
