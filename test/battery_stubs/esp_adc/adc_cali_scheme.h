/* Host-test stub: esp_adc/adc_cali_scheme.h. The S3 offers curve fitting,
 * which is the branch battery.c compiles for the EE02. */
#pragma once

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"

#define ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED 1
#define ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED 0

typedef struct {
    adc_unit_t unit_id;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_cali_curve_fitting_config_t;

typedef struct {
    adc_unit_t unit_id;
    adc_atten_t atten;
    adc_bitwidth_t bitwidth;
} adc_cali_line_fitting_config_t;

esp_err_t adc_cali_create_scheme_curve_fitting(
    const adc_cali_curve_fitting_config_t *cfg, adc_cali_handle_t *ret_handle);
esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle);
esp_err_t adc_cali_create_scheme_line_fitting(
    const adc_cali_line_fitting_config_t *cfg, adc_cali_handle_t *ret_handle);
esp_err_t adc_cali_delete_scheme_line_fitting(adc_cali_handle_t handle);
