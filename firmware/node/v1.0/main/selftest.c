/* Power-on self-test. SPDX-License-Identifier: MIT */
#include "selftest.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "app_config.h"
#include "bsp.h"
#include "dht22.h"
#include "eventlog.h"
#include "outputs.h"
#include "sensors.h"
#include "sx1276.h"

static int s_index;

static bool report(const char *name, bool ran, bool pass, const char *detail)
{
    const char *res = !ran ? "SKIP" : pass ? "OK" : "FAIL";
    printf("[CHECK] %-12s %-4s %s\n", name, res, detail);
    if (ran && !pass) {
        eventlog_add(EV_SELFTEST_FAIL, (uint8_t)s_index, 0);
    }
    s_index++;
    return ran && !pass;
}

void selftest_run(node_ctx_t *ctx)
{
    char d[64];
    bool fail = false;
    s_index = 0;

    sensor_sample_t s;
    sensors_read(&s);

    /* 1. Gas input: open wire or railed */
    if (!s.gas_ok) {
        fail |= report("gas input", true, false, "ADC read error");
    } else if (s.gas_pin_mv < CFG_GAS_OPEN_PIN_MV) {
        snprintf(d, sizeof d, "%lu mV at GPIO32: open circuit or sensor missing", (unsigned long)s.gas_pin_mv);
        fail |= report("gas input", true, false, d);
    } else if (s.gas_raw >= CFG_GAS_RAIL_RAW || s.gas_ao_mv >= CFG_GAS_RAIL_AO_MV) {
        snprintf(d, sizeof d, "railed (%u counts)", s.gas_raw);
        fail |= report("gas input", true, false, d);
    } else {
        snprintf(d, sizeof d, "%lu mV at AO, Rs/R0 %.2f%s", (unsigned long)s.gas_ao_mv, s.gas_x1000 / 1000.0,
                 sensors_is_commissioned() ? "" : " (R0 not commissioned)");
        report("gas input", true, true, d);
    }

    /* 2. Flame input: the ADC conversion itself (the module has no open-wire signature) */
    snprintf(d, sizeof d, "ADC read %u counts%s", s.flame_raw, s.flame_active ? ", FLAME ACTIVE" : "");
    fail |= report("flame input", true, s.flame_ok, d);

    /* 3. DHT22 read with checksum */
    int16_t t = 0, h = 0;
    esp_err_t e = dht22_read(BSP_PIN_DHT, &t, &h);
    ctx->boot_dht = (dht_result_t){ .ok = e == ESP_OK, .temp_x100 = t, .hum_x10 = h };
    if (e == ESP_OK) {
        snprintf(d, sizeof d, "%.1f C, %.1f %%RH", t / 100.0, h / 10.0);
    } else {
        snprintf(d, sizeof d, "%s", esp_err_to_name(e));
    }
    report("DHT22", true, e == ESP_OK, d);   /* temperature fault is handled by the classifier */

    /* 4. Output pins: drive and read back the pad (the LED itself is not sensed) */
    static const struct { const char *name; int pin; } outs[] = {
        { "LED red", BSP_PIN_LED_RED },
        { "LED green", BSP_PIN_LED_GREEN },
        { "LED yellow", BSP_PIN_LED_YELLOW },
        { "buzzer drive", BSP_PIN_BUZZER },
    };
    for (size_t i = 0; i < sizeof outs / sizeof outs[0]; i++) {
        bool ok = outputs_pin_check(outs[i].pin);
        fail |= report(outs[i].name, true, ok, ok ? "pin drives high and low" : "pin stuck (short?)");
    }
    gpio_set_level(BSP_PIN_BUZZER, 1);         /* 50 ms audible chirp for the operator */
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(BSP_PIN_BUZZER, 0);

    /* 5. LoRa radio */
    if (ctx->radio_ok) {
        snprintf(d, sizeof d, "SX1276 version 0x%02x", sx1276_version());
        report("LoRa radio", true, true, d);
    } else {
        fail |= report("LoRa radio", true, false, "SX1276 not responding");
    }

    /* 6. Battery sense */
    if (s.vbat_mv <= CFG_VBAT_ABSENT_MV) {
        report("battery", false, false, "no cell fitted (mains only)");
    } else {
        snprintf(d, sizeof d, "%u mV", s.vbat_mv);
        report("battery", true, s.vbat_mv >= CFG_VBAT_LOW_MV, d);
    }

    ctx->selftest_fault = fail;
    printf("[SYSTEM] self-test %s\n", fail ? "FAILED: node stays in FAULT" : "passed");
}
