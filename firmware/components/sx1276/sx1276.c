/* SX1276 LoRa driver. SPDX-License-Identifier: MIT */
#include "sx1276.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "sx1276";

/* Registers (LoRa mode) */
#define REG_FIFO              0x00
#define REG_OP_MODE           0x01
#define REG_FRF_MSB           0x06
#define REG_FRF_MID           0x07
#define REG_FRF_LSB           0x08
#define REG_PA_CONFIG         0x09
#define REG_OCP               0x0B
#define REG_LNA               0x0C
#define REG_FIFO_ADDR_PTR     0x0D
#define REG_FIFO_TX_BASE      0x0E
#define REG_FIFO_RX_BASE      0x0F
#define REG_FIFO_RX_CURRENT   0x10
#define REG_IRQ_FLAGS         0x12
#define REG_RX_NB_BYTES       0x13
#define REG_PKT_SNR           0x19
#define REG_PKT_RSSI          0x1A
#define REG_MODEM_CONFIG1     0x1D
#define REG_MODEM_CONFIG2     0x1E
#define REG_PREAMBLE_MSB      0x20
#define REG_PREAMBLE_LSB      0x21
#define REG_PAYLOAD_LENGTH    0x22
#define REG_MODEM_CONFIG3     0x26
#define REG_DETECT_OPTIMIZE   0x31
#define REG_DETECTION_THRESH  0x37
#define REG_SYNC_WORD         0x39
#define REG_DIO_MAPPING1      0x40
#define REG_VERSION           0x42
#define REG_PA_DAC            0x4D

#define MODE_LONG_RANGE       0x80
#define MODE_SLEEP            0x00
#define MODE_STDBY            0x01
#define MODE_TX               0x03
#define MODE_RX_CONTINUOUS    0x05
#define MODE_CAD              0x07

#define IRQ_RX_TIMEOUT        0x80
#define IRQ_RX_DONE           0x40
#define IRQ_PAYLOAD_CRC_ERR   0x20
#define IRQ_TX_DONE           0x08
#define IRQ_CAD_DONE          0x04
#define IRQ_CAD_DETECTED      0x01

#define DIO0_RX_DONE          0x00
#define DIO0_TX_DONE          0x40
#define DIO0_CAD_DONE         0x80

static spi_device_handle_t s_spi;
static SemaphoreHandle_t s_dio0_sem;
static SemaphoreHandle_t s_lock;
static sx1276_config_t s_cfg;

static void IRAM_ATTR dio0_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dio0_sem, &woken);
    if (woken) {
        portYIELD_FROM_ISR();
    }
}

static uint8_t rd(uint8_t reg)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA,
        .length = 16,
        .tx_data = { reg & 0x7F, 0x00 },
    };
    spi_device_polling_transmit(s_spi, &t);
    return t.rx_data[1];
}

static void wr(uint8_t reg, uint8_t val)
{
    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_TXDATA,
        .length = 16,
        .tx_data = { reg | 0x80, val },
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void wr_burst(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + 64];
    if (len > 64) {
        len = 64;
    }
    buf[0] = reg | 0x80;
    memcpy(&buf[1], data, len);
    spi_transaction_t t = { .length = (1 + len) * 8, .tx_buffer = buf };
    spi_device_polling_transmit(s_spi, &t);
}

static void rd_burst(uint8_t reg, uint8_t *data, size_t len)
{
    uint8_t tx[1 + 64] = { reg & 0x7F };
    uint8_t rx[1 + 64];
    if (len > 64) {
        len = 64;
    }
    spi_transaction_t t = { .length = (1 + len) * 8, .tx_buffer = tx, .rx_buffer = rx };
    spi_device_polling_transmit(s_spi, &t);
    memcpy(data, &rx[1], len);
}

static void set_mode(uint8_t mode) { wr(REG_OP_MODE, MODE_LONG_RANGE | mode); }

static uint8_t bw_code(uint32_t bw)
{
    switch (bw) {
    case 62500:  return 6;
    case 125000: return 7;
    case 250000: return 8;
    case 500000: return 9;
    default:     return 7;
    }
}

void sx1276_start_rx(void)
{
    wr(REG_DIO_MAPPING1, DIO0_RX_DONE);
    wr(REG_FIFO_ADDR_PTR, 0);
    set_mode(MODE_RX_CONTINUOUS);
}

esp_err_t sx1276_init(const sx1276_config_t *cfg)
{
    s_cfg = *cfg;
    uint32_t half_bw = cfg->bw_hz / 2;
    if (cfg->freq_hz - half_bw < SX1276_BAND_MIN_HZ || cfg->freq_hz + half_bw > SX1276_BAND_MAX_HZ) {
        ESP_LOGE(TAG, "channel %lu Hz +/- %lu Hz is outside 865-867 MHz; refusing (HR-04)",
                 (unsigned long)cfg->freq_hz, (unsigned long)half_bw);
        return ESP_ERR_INVALID_ARG;
    }

    s_dio0_sem = xSemaphoreCreateBinary();
    s_lock = xSemaphoreCreateMutex();

    spi_bus_config_t bus = {
        .sclk_io_num = cfg->pin_sck,
        .miso_io_num = cfg->pin_miso,
        .mosi_io_num = cfg->pin_mosi,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 80,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(cfg->host, &bus, SPI_DMA_DISABLED));
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 8 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = cfg->pin_nss,
        .queue_size = 2,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(cfg->host, &dev, &s_spi));

    gpio_config_t rst = { .pin_bit_mask = 1ULL << cfg->pin_rst, .mode = GPIO_MODE_OUTPUT };
    gpio_config(&rst);
    gpio_set_level(cfg->pin_rst, 0);
    esp_rom_delay_us(200);
    gpio_set_level(cfg->pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ver = rd(REG_VERSION);
    if (ver != 0x12) {
        ESP_LOGE(TAG, "unexpected version 0x%02x (radio missing or wiring fault)", ver);
        return ESP_ERR_NOT_FOUND;
    }

    set_mode(MODE_SLEEP); /* LoRa bit can only be set in sleep */
    vTaskDelay(pdMS_TO_TICKS(2));

    uint64_t frf = ((uint64_t)cfg->freq_hz << 19) / 32000000ULL;
    wr(REG_FRF_MSB, (uint8_t)(frf >> 16));
    wr(REG_FRF_MID, (uint8_t)(frf >> 8));
    wr(REG_FRF_LSB, (uint8_t)frf);

    int8_t p = cfg->tx_dbm < 2 ? 2 : (cfg->tx_dbm > 17 ? 17 : cfg->tx_dbm);
    wr(REG_PA_CONFIG, 0x80 | (uint8_t)(p - 2)); /* PA_BOOST (RFM95W) */
    wr(REG_PA_DAC, 0x84);                         /* normal (<= 17 dBm) */
    wr(REG_OCP, 0x2B);                            /* 100 mA */
    wr(REG_LNA, 0x23);                            /* max gain, HF boost */

    wr(REG_FIFO_TX_BASE, 0);
    wr(REG_FIFO_RX_BASE, 0);

    uint8_t cr = (uint8_t)(cfg->cr_denom - 4);     /* 4/5 -> 1 */
    wr(REG_MODEM_CONFIG1, (uint8_t)((bw_code(cfg->bw_hz) << 4) | (cr << 1))); /* explicit header */
    wr(REG_MODEM_CONFIG2, (uint8_t)((cfg->sf << 4) | 0x04));                  /* CRC on */
    /* Low-data-rate optimisation is required when the symbol time exceeds 16 ms. */
    uint32_t tsym_us = (uint32_t)((1000000ULL << cfg->sf) / cfg->bw_hz);
    wr(REG_MODEM_CONFIG3, (uint8_t)(0x04 | (tsym_us > 16000 ? 0x08 : 0x00)));  /* AGC auto */
    wr(REG_PREAMBLE_MSB, (uint8_t)(cfg->preamble >> 8));
    wr(REG_PREAMBLE_LSB, (uint8_t)cfg->preamble);
    wr(REG_DETECT_OPTIMIZE, 0xC3);
    wr(REG_DETECTION_THRESH, 0x0A);
    wr(REG_SYNC_WORD, cfg->sync_word);

    gpio_config_t io = {
        .pin_bit_mask = 1ULL << cfg->pin_dio0,
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&io);
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    gpio_isr_handler_add(cfg->pin_dio0, dio0_isr, NULL);

    wr(REG_IRQ_FLAGS, 0xFF);
    sx1276_start_rx();
    ESP_LOGI(TAG, "ready: %lu Hz SF%u BW%lu CR4/%u %d dBm", (unsigned long)cfg->freq_hz, cfg->sf,
             (unsigned long)cfg->bw_hz, cfg->cr_denom, p);
    return ESP_OK;
}

uint8_t sx1276_version(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t v = rd(REG_VERSION);
    xSemaphoreGive(s_lock);
    return v;
}

esp_err_t sx1276_send(const uint8_t *data, size_t len, uint32_t timeout_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_mode(MODE_STDBY);
    wr(REG_IRQ_FLAGS, 0xFF);
    xSemaphoreTake(s_dio0_sem, 0); /* drop stale edge */
    wr(REG_DIO_MAPPING1, DIO0_TX_DONE);
    wr(REG_FIFO_ADDR_PTR, 0);
    wr_burst(REG_FIFO, data, len);
    wr(REG_PAYLOAD_LENGTH, (uint8_t)len);
    set_mode(MODE_TX);

    esp_err_t res = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(s_dio0_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE &&
        (rd(REG_IRQ_FLAGS) & IRQ_TX_DONE)) {
        res = ESP_OK;
    }
    wr(REG_IRQ_FLAGS, 0xFF);
    sx1276_start_rx();
    xSemaphoreGive(s_lock);
    return res;
}

int sx1276_receive(uint8_t *buf, size_t max_len, uint32_t timeout_ms, sx1276_rx_info_t *info)
{
    if (xSemaphoreTake(s_dio0_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
        /* Also poll the flags, in case an edge was consumed elsewhere. */
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint8_t f = rd(REG_IRQ_FLAGS);
        xSemaphoreGive(s_lock);
        if (!(f & IRQ_RX_DONE)) {
            return 0;
        }
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t flags = rd(REG_IRQ_FLAGS);
    wr(REG_IRQ_FLAGS, 0xFF);
    int ret = 0;
    if (flags & IRQ_RX_DONE) {
        if (flags & IRQ_PAYLOAD_CRC_ERR) {
            ret = -1;
        } else {
            uint8_t n = rd(REG_RX_NB_BYTES);
            if (n > max_len) {
                n = (uint8_t)max_len;
            }
            wr(REG_FIFO_ADDR_PTR, rd(REG_FIFO_RX_CURRENT));
            rd_burst(REG_FIFO, buf, n);
            if (info) {
                int8_t snr_raw = (int8_t)rd(REG_PKT_SNR);
                info->snr_db = snr_raw / 4.0f;
                int16_t rssi = -157 + rd(REG_PKT_RSSI);
                if (snr_raw < 0) {
                    rssi += snr_raw / 4;
                }
                info->rssi_dbm = rssi;
            }
            ret = n;
        }
    }
    xSemaphoreGive(s_lock);
    return ret;
}

bool sx1276_channel_busy(uint32_t timeout_ms)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_mode(MODE_STDBY);
    wr(REG_IRQ_FLAGS, 0xFF);
    xSemaphoreTake(s_dio0_sem, 0);
    wr(REG_DIO_MAPPING1, DIO0_CAD_DONE);
    set_mode(MODE_CAD);
    bool busy = false;
    if (xSemaphoreTake(s_dio0_sem, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        busy = (rd(REG_IRQ_FLAGS) & IRQ_CAD_DETECTED) != 0;
    }
    wr(REG_IRQ_FLAGS, 0xFF);
    sx1276_start_rx();
    xSemaphoreGive(s_lock);
    return busy;
}

void sx1276_sleep(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    set_mode(MODE_SLEEP);
    xSemaphoreGive(s_lock);
}
