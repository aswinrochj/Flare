/*
 * Register-level SX1276 (HopeRF RFM95W) LoRa driver for ESP-IDF v5.
 * DIO0 interrupt signals TX done / RX done / CAD done (DPR Section IV-C).
 * The carrier frequency is locked to the 865-867 MHz Indian licence-exempt
 * band (HR-04): sx1276_init() refuses any channel that would spill outside it.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SX1276_BAND_MIN_HZ 865000000UL
#define SX1276_BAND_MAX_HZ 867000000UL

typedef struct {
    spi_host_device_t host;
    int pin_sck, pin_miso, pin_mosi, pin_nss, pin_rst, pin_dio0;
    uint32_t freq_hz;      /* channel centre */
    uint8_t  sf;           /* 7..12 */
    uint32_t bw_hz;        /* 125000 or 250000 */
    uint8_t  cr_denom;     /* 5 => 4/5 */
    uint16_t preamble;     /* symbols */
    int8_t   tx_dbm;       /* 2..17 on PA_BOOST */
    uint8_t  sync_word;    /* 0x12 private network */
} sx1276_config_t;

typedef struct {
    int16_t rssi_dbm;
    float   snr_db;
} sx1276_rx_info_t;

esp_err_t sx1276_init(const sx1276_config_t *cfg);

/* Chip version register (0x12 for a genuine SX1276). */
uint8_t   sx1276_version(void);

/* Blocking transmit; returns ESP_ERR_TIMEOUT if TX-done not seen. The radio
 * returns to continuous RX afterwards. */
esp_err_t sx1276_send(const uint8_t *data, size_t len, uint32_t timeout_ms);

/* Wait up to timeout_ms (0 = poll) for a received packet. Returns the length,
 * 0 on timeout, or -1 on a payload CRC error (packet discarded). */
int       sx1276_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms, sx1276_rx_info_t *info);

/* Channel-activity detection. true = LoRa preamble detected (channel busy). */
bool      sx1276_channel_busy(uint32_t timeout_ms);

/* Put the radio to sleep (used by the "mute radio" failure-injection hook). */
void      sx1276_sleep(void);
void      sx1276_start_rx(void);

#ifdef __cplusplus
}
#endif
