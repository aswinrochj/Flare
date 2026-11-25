/* Analog sensor drivers. SPDX-License-Identifier: MIT */
#include "sensors.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "app_config.h"
#include "bsp.h"

static const char *TAG = "sensors";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static uint32_t s_r0_x1000 = CFG_GAS_R0_DEFAULT_X1000;
static bool s_commissioned;
static float s_baseline_x1000;  /* tracked (Rs/RL) in clean air */

esp_err_t sensors_init(void)
{
    adc_oneshot_unit_init_cfg_t unit = { .unit_id = BSP_ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit, &s_adc));
    adc_oneshot_chan_cfg_t ch = { .atten = BSP_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_12 };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BSP_ADC_GAS_CH, &ch));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BSP_ADC_FLAME_CH, &ch));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BSP_ADC_VBAT_CH, &ch));

    adc_cali_line_fitting_config_t cal = {
        .unit_id = BSP_ADC_UNIT,
        .atten = BSP_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = 1100,
    };
    if (adc_cali_create_scheme_line_fitting(&cal, &s_cali) != ESP_OK) {
        ESP_LOGW(TAG, "ADC eFuse calibration unavailable; using raw scaling");
        s_cali = NULL;
    }

    nvs_handle_t h;
    if (nvs_open("safenode", NVS_READONLY, &h) == ESP_OK) {
        uint32_t v;
        if (nvs_get_u32(h, "r0_x1000", &v) == ESP_OK && v > 0) {
            s_r0_x1000 = v;
            s_commissioned = true;
        }
        nvs_close(h);
    }
    if (!s_commissioned) {
        ESP_LOGW(TAG, "gas sensor not commissioned: using default R0; run 'cal' in clean air");
    }
    s_baseline_x1000 = (float)s_r0_x1000 * CFG_GAS_CLEAN_AIR_RATIO_X100 / 100.0f;
    return ESP_OK;
}

static bool read_mv(adc_channel_t ch, int n, uint16_t *raw_out, uint32_t *mv_out)
{
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int raw;
        if (adc_oneshot_read(s_adc, ch, &raw) != ESP_OK) {
            return false;
        }
        sum += raw;
    }
    int raw = sum / n;
    int mv;
    if (s_cali == NULL || adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
        mv = raw * 2450 / 4095;
    }
    *raw_out = (uint16_t)raw;
    *mv_out = (uint32_t)mv;
    return true;
}

/* (Rs/RL) x 1000 from the module output voltage. RL cancels in Rs/R0. */
static uint32_t rs_rl_x1000(uint32_t ao_mv)
{
    if (ao_mv == 0) {
        return UINT32_MAX;
    }
    if (ao_mv >= CFG_SENSOR_SUPPLY_MV) {
        return 0;
    }
    return (uint32_t)(((uint64_t)(CFG_SENSOR_SUPPLY_MV - ao_mv) * 1000u) / ao_mv);
}

void sensors_read(sensor_sample_t *s)
{
    uint32_t mv;
    s->gas_ok = read_mv(BSP_ADC_GAS_CH, CFG_GAS_OVERSAMPLE, &s->gas_raw, &mv);
    s->gas_pin_mv = mv;
    s->gas_ao_mv = mv * CFG_GAS_DIV_NUM / CFG_GAS_DIV_DEN;
    uint32_t rs = rs_rl_x1000(s->gas_ao_mv);
    uint64_t ratio = rs == UINT32_MAX ? 0xFFFF : (uint64_t)rs * 1000u / s_r0_x1000;
    s->gas_x1000 = ratio > 0xFFFF ? 0xFFFF : (uint16_t)ratio;

    s->flame_ok = read_mv(BSP_ADC_FLAME_CH, 4, &s->flame_raw, &mv);
#if CONFIG_SAFENODE_FLAME_ACTIVE_LOW
    s->flame_active = s->flame_ok && s->flame_raw < CFG_FLAME_THRESHOLD_RAW;
#else
    s->flame_active = s->flame_ok && s->flame_raw >= CFG_FLAME_THRESHOLD_RAW;
#endif

    uint16_t vraw;
    if (read_mv(BSP_ADC_VBAT_CH, 4, &vraw, &mv)) {
        s->vbat_mv = (uint16_t)(mv * CFG_VBAT_DIV);
    } else {
        s->vbat_mv = 0;
    }
}

uint32_t sensors_r0_x1000(void) { return s_r0_x1000; }
bool sensors_is_commissioned(void) { return s_commissioned; }

esp_err_t sensors_calibrate(int n)
{
    uint64_t sum = 0;
    int good = 0;
    for (int i = 0; i < n; i++) {
        sensor_sample_t s;
        sensors_read(&s);
        if (s.gas_ok && s.gas_pin_mv >= CFG_GAS_OPEN_PIN_MV) {
            sum += rs_rl_x1000(s.gas_ao_mv);
            good++;
        }
    }
    if (good < n / 2) {
        return ESP_ERR_INVALID_STATE;
    }
    /* R0 = Rs(clean air) / 9.83 (MQ-2 datasheet clean-air ratio) */
    uint32_t r0 = (uint32_t)((sum / good) * 100u / CFG_GAS_CLEAN_AIR_RATIO_X100);
    if (r0 == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open("safenode", NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u32(h, "r0_x1000", r0);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    if (err == ESP_OK) {
        s_r0_x1000 = r0;
        s_commissioned = true;
        s_baseline_x1000 = (float)r0 * CFG_GAS_CLEAN_AIR_RATIO_X100 / 100.0f;
        ESP_LOGI(TAG, "R0 captured: (Rs/RL) = %lu / 1000", (unsigned long)r0);
    }
    return err;
}

bool sensors_track_baseline(const sensor_sample_t *s, bool clean_air)
{
    if (clean_air && s->gas_ok && s->gas_pin_mv >= CFG_GAS_OPEN_PIN_MV) {
        uint32_t rs = rs_rl_x1000(s->gas_ao_mv);
        if (rs != UINT32_MAX) {
            /* time constant ~ 2 h at 10 Hz */
            s_baseline_x1000 += ((float)rs - s_baseline_x1000) / 72000.0f;
        }
    }
    float expected = (float)s_r0_x1000 * CFG_GAS_CLEAN_AIR_RATIO_X100 / 100.0f;
    float dev = (s_baseline_x1000 - expected) / expected;
    if (dev < 0) {
        dev = -dev;
    }
    return dev * 100.0f > CFG_GAS_DRIFT_LIMIT_PCT;
}
