/* DHT22 bit-banged driver. SPDX-License-Identifier: MIT */
#include "dht22.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

void dht22_init(gpio_num_t pin)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,   /* open drain; R7 / module pull-up */
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&io);
    gpio_set_level(pin, 1);
}

/* Wait while the line stays at `level`; return microseconds waited or -1. */
static int wait_while(gpio_num_t pin, int level, int timeout_us)
{
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(pin) == level) {
        if (esp_timer_get_time() - t0 > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - t0);
}

esp_err_t dht22_read(gpio_num_t pin, int16_t *temp_x100, int16_t *hum_x10)
{
    uint8_t d[5] = { 0 };

    /* Start signal: hold low >= 1 ms, then release. */
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(2));

    esp_err_t err = ESP_OK;
    portENTER_CRITICAL(&s_mux);   /* ~5 ms with interrupts off on this core */
    gpio_set_level(pin, 1);
    esp_rom_delay_us(30);
    if (wait_while(pin, 1, 60) < 0 || wait_while(pin, 0, 100) < 0 || wait_while(pin, 1, 100) < 0) {
        err = ESP_ERR_TIMEOUT;
    } else {
        for (int i = 0; i < 40; i++) {
            if (wait_while(pin, 0, 80) < 0) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
            int high = wait_while(pin, 1, 100);
            if (high < 0) {
                err = ESP_ERR_TIMEOUT;
                break;
            }
            d[i / 8] = (uint8_t)((d[i / 8] << 1) | (high > 40 ? 1 : 0)); /* 26-28 us = 0, 70 us = 1 */
        }
    }
    portEXIT_CRITICAL(&s_mux);

    if (err != ESP_OK) {
        return err;
    }
    if ((uint8_t)(d[0] + d[1] + d[2] + d[3]) != d[4]) {
        return ESP_ERR_INVALID_CRC;
    }
    int16_t hum = (int16_t)((d[0] << 8) | d[1]);              /* 0.1 %RH */
    int16_t t = (int16_t)(((d[2] & 0x7F) << 8) | d[3]);       /* 0.1 C, sign bit */
    if (d[2] & 0x80) {
        t = (int16_t)-t;
    }
    if (hum > 1000 || t < -400 || t > 800) {
        return ESP_ERR_INVALID_RESPONSE;                      /* outside sensor range: treat as NaN */
    }
    *temp_x100 = (int16_t)(t * 10);
    *hum_x10 = hum;
    return ESP_OK;
}
