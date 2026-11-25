/* DHT22 (AM2302) single-wire driver. SPDX-License-Identifier: MIT */
#pragma once

#include <stdint.h>
#include "driver/gpio.h"
#include "esp_err.h"

/* Read once. Returns ESP_OK, ESP_ERR_TIMEOUT (no response) or
 * ESP_ERR_INVALID_CRC (checksum error). Must not be called more often than
 * every 2 s (datasheet; FR-02). */
esp_err_t dht22_read(gpio_num_t pin, int16_t *temp_x100, int16_t *hum_x10);
void      dht22_init(gpio_num_t pin);
