/* Host-test stub: esp_adc/adc_oneshot.h (only what battery.c uses). */
#pragma once

#include "esp_err.h"

typedef void *adc_oneshot_unit_handle_t;

typedef enum { ADC_UNIT_1 = 0, ADC_UNIT_2 } adc_unit_t;
typedef int adc_channel_t;
typedef enum { ADC_ATTEN_DB_12 = 3 } adc_atten_t;
typedef enum { ADC_BITWIDTH_12 = 12 } adc_bitwidth_t;

#define ADC_CHANNEL_0 0

typedef struct {
    adc_unit_t unit_id;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_oneshot_chan_cfg_t;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg,
                               adc_oneshot_unit_handle_t *ret_unit);
esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t unit);
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t unit,
                                     adc_channel_t channel,
                                     const adc_oneshot_chan_cfg_t *cfg);
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t unit,
                           adc_channel_t channel, int *out_raw);
