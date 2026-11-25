/*
 * SAFE-NODE gateway firmware (ESP-IDF v5). Runs in the control room on an
 * ESP32 + SX1276; the SAFE-NODE carrier PCB can be used as the gateway board
 * (buzzer = control-room siren, LEDs = summary).
 *
 *  - receives node frames over LoRa (and ESP-NOW fallback), ACKs alerts and
 *    faults at once, drops duplicates (Technical Documentation V-C);
 *  - broadcasts a beacon every 3 s for the heartbeat slots;
 *  - flags a node OFFLINE after 3 missed heartbeats (FR-21);
 *  - drives the control-room siren itself, so alerts are annunciated even if
 *    the host has crashed; sounds a distinct system-fault tone for a FAULT or
 *    OFFLINE node, or when the host stops answering PING within 3 s (FR-24);
 *  - forwards every event to the host as one JSON object per line (V-D).
 *
 * Host -> gateway lines: {"t":"PONG"}  {"t":"OPACK","n":3}
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "bsp.h"
#include "node_table.h"
#include "sn_proto.h"
#include "sx1276.h"

static const char *TAG = "gateway";

#define PING_PERIOD_MS      1000u
#define HOST_MISSED_PINGS      3u
#define STATUS_PERIOD_MS   10000u

typedef struct {
    uint8_t len;
    uint8_t data[SN_FRAME_LEN];
    int16_t rssi;
    float   snr;
    bool    espnow;
} rx_item_t;

static node_table_t s_table;
static SemaphoreHandle_t s_table_lock;
static SemaphoreHandle_t s_out_lock;      /* one JSON line at a time on the UART */
static QueueHandle_t s_espnow_q;
static QueueHandle_t s_opack_q;           /* node ids to acknowledge over the air */
static volatile uint32_t s_pings_unanswered;
static volatile uint32_t s_crc_errors;
static bool s_lora_ok;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void out_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out_line(const char *fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    xSemaphoreTake(s_out_lock, portMAX_DELAY);
    fputs(buf, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    xSemaphoreGive(s_out_lock);
}

static void flags_json(uint8_t flags, char *out, size_t n)
{
    static const char *const names[] = { "flame", "gas_fault", "temp_fault", "flame_fault",
                                         "low_batt", "latched", "via_espnow" };
    size_t pos = (size_t)snprintf(out, n, "[");
    bool first = true;
    for (int b = 0; b < 7 && pos < n; b++) {
        if (flags & (1u << b)) {
            pos += (size_t)snprintf(out + pos, n - pos, "%s\"%s\"", first ? "" : ",", names[b]);
            first = false;
        }
    }
    snprintf(out + pos, n - pos, "]");
}

static void forward(const sn_frame_t *f, const rx_item_t *it, const char *type_override)
{
    char fl[96];
    flags_json(f->flags, fl, sizeof fl);
    out_line("{\"n\":%u,\"t\":\"%s\",\"seq\":%u,\"st\":\"%s\",\"gas\":%.3f,\"tc\":%.2f,\"fl\":%d,"
             "\"bat\":%u,\"rssi\":%d,\"snr\":%.1f,\"flags\":%s,\"rc\":%u,\"rn\":%u,\"up\":%lu,\"via\":\"%s\"}",
             f->node_id, type_override ? type_override : sn_type_name(f->type), f->seq,
             sn_state_name(f->state), f->gas_x1000 / 1000.0, f->temp_x100 / 100.0,
             (f->flags & SN_F_FLAME) ? 1 : 0, f->vbat_mv, it->rssi, it->snr, fl, f->reset_cause,
             f->reset_count, (unsigned long)f->uptime_s, it->espnow ? "espnow" : "lora");
}

static void send_ack(uint8_t node_id, uint16_t seq)
{
    if (!s_lora_ok) {
        return;
    }
    uint8_t ack[SN_ACK_LEN];
    sn_ack_encode(node_id, seq, ack);
    sx1276_send(ack, sizeof ack, 200);
}

static void handle_frame(const rx_item_t *it)
{
    sn_frame_t f;
    sn_err_t e = sn_frame_decode(it->data, it->len, &f);
    if (e != SN_OK) {
        s_crc_errors++;          /* a bad frame counts as missing (V-C) */
        return;
    }
    /* ACK first: the node is waiting with a 200 ms timeout. */
    if (!it->espnow && (f.type == SN_T_ALERT || f.type == SN_T_FAULT || f.type == SN_T_WARNING)) {
        send_ack(f.node_id, f.seq);
    }
    xSemaphoreTake(s_table_lock, portMAX_DELAY);
    nt_result_t r = nt_on_frame(&s_table, &f, now_ms());
    xSemaphoreGive(s_table_lock);

    if (r == NT_DUPLICATE) {
        return;                  /* acknowledged but not re-forwarded */
    }
    if (r == NT_RECOVERED) {
        out_line("{\"n\":%u,\"t\":\"RECOVERED\",\"st\":\"%s\"}", f.node_id, sn_state_name(f.state));
    }
    forward(&f, it, NULL);
}

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (len != SN_FRAME_LEN) {
        return;
    }
    rx_item_t it = { .len = (uint8_t)len, .rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0, .espnow = true };
    memcpy(it.data, data, SN_FRAME_LEN);
    xQueueSend(s_espnow_q, &it, 0);
}

static void espnow_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) {
        return;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(CONFIG_GATEWAY_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(espnow_recv_cb);
    }
}

/* Owns the SX1276: receives, ACKs, sends beacons and operator ACKs. */
static void radio_task(void *arg)
{
    uint16_t cycle = 0;
    uint32_t next_beacon = now_ms();
    for (;;) {
        if (s_lora_ok && (int32_t)(now_ms() - next_beacon) >= 0) {
            uint8_t b[SN_BEACON_LEN];
            sn_beacon_encode(cycle++, b);
            sx1276_send(b, sizeof b, 200);
            next_beacon += SN_HB_PERIOD_MS;
        }

        uint8_t id;
        while (xQueueReceive(s_opack_q, &id, 0) == pdTRUE) {
            send_ack(id, SN_SEQ_OPERATOR_ACK);
            vTaskDelay(pdMS_TO_TICKS(60));
            send_ack(id, SN_SEQ_OPERATOR_ACK);   /* sent twice: no ACK-of-ACK in the protocol */
        }

        rx_item_t it = { 0 };
        if (s_lora_ok) {
            sx1276_rx_info_t info;
            int n = sx1276_receive(it.data, sizeof it.data, 20, &info);
            if (n == -1) {
                s_crc_errors++;
            } else if (n > 0) {
                it.len = (uint8_t)n;
                it.rssi = info.rssi_dbm;
                it.snr = info.snr_db;
                handle_frame(&it);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        while (xQueueReceive(s_espnow_q, &it, 0) == pdTRUE) {
            handle_frame(&it);
        }
    }
}

/* Liveness, host link and annunciation, every 100 ms. */
static void supervisor_task(void *arg)
{
    uint32_t last_ping = now_ms();
    uint32_t last_status = now_ms();
    bool host_lost_reported = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
        uint32_t now = now_ms();

        xSemaphoreTake(s_table_lock, portMAX_DELAY);
        uint32_t off = nt_check_liveness(&s_table, now);
        bool danger = nt_any_danger(&s_table);
        bool unsafe = nt_any_unsafe(&s_table);
        int known = 0;
        for (unsigned i = 1; i <= SN_MAX_NODE_ID; i++) {
            known += s_table.node[i].known;
        }
        xSemaphoreGive(s_table_lock);

        for (unsigned id = 1; id <= SN_MAX_NODE_ID; id++) {
            if (off & (1UL << id)) {
                out_line("{\"n\":%u,\"t\":\"OFFLINE\",\"st\":\"OFFLINE\"}", id);
            }
        }

        if (now - last_ping >= PING_PERIOD_MS) {
            last_ping = now;
            s_pings_unanswered++;
            out_line("{\"t\":\"PING\"}");
        }
        bool host_lost = s_pings_unanswered >= HOST_MISSED_PINGS;   /* 3 PINGs unanswered: <= 3 s (FR-24, TC-7) */
        if (host_lost && !host_lost_reported) {
            out_line("{\"t\":\"HOSTLOST\"}");
            ESP_LOGE(TAG, "host bridge silent: system-fault tone");
        }
        host_lost_reported = host_lost;

        /* Siren: continuous for DANGER; fault tone (200 ms every 2 s) for a
         * FAULT / OFFLINE node or a lost host. */
        bool tone = (now % 2000) < 200;
        bool siren = danger || ((unsafe || host_lost) && tone);
        gpio_set_level(BSP_PIN_BUZZER, siren);
        gpio_set_level(BSP_PIN_LED_RED, danger);
        gpio_set_level(BSP_PIN_LED_YELLOW, unsafe || host_lost);
        gpio_set_level(BSP_PIN_LED_GREEN, !danger && !unsafe && !host_lost && s_lora_ok);

        if (now - last_status >= STATUS_PERIOD_MS) {
            last_status = now;
            out_line("{\"t\":\"GW\",\"up\":%lu,\"nodes\":%d,\"crc_err\":%lu,\"lora\":%s,\"host\":\"%s\"}",
                     (unsigned long)(now / 1000), known, (unsigned long)s_crc_errors,
                     s_lora_ok ? "true" : "false", host_lost ? "lost" : "ok");
        }
    }
}

/* Reads host lines: {"t":"PONG"} and {"t":"OPACK","n":<id>}. */
static void host_rx_task(void *arg)
{
    char line[128];
    size_t pos = 0;
    for (;;) {
        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(100)) != 1) {
            continue;
        }
        if (c != '\n' && c != '\r') {
            if (pos < sizeof line - 1) {
                line[pos++] = (char)c;
            }
            continue;
        }
        line[pos] = 0;
        pos = 0;
        if (strstr(line, "\"PONG\"")) {
            s_pings_unanswered = 0;
        } else if (strstr(line, "\"OPACK\"")) {
            const char *p = strstr(line, "\"n\":");
            int id = p ? atoi(p + 4) : 0;
            if (id >= 1 && id <= (int)SN_MAX_NODE_ID) {
                xSemaphoreTake(s_table_lock, portMAX_DELAY);
                nt_silence(&s_table, (uint8_t)id);
                xSemaphoreGive(s_table_lock);
                uint8_t u = (uint8_t)id;
                xQueueSend(s_opack_q, &u, 0);
                out_line("{\"n\":%d,\"t\":\"OPACKED\"}", id);
            }
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    s_table_lock = xSemaphoreCreateMutex();
    s_out_lock = xSemaphoreCreateMutex();
    s_espnow_q = xQueueCreate(8, sizeof(rx_item_t));
    s_opack_q = xQueueCreate(4, sizeof(uint8_t));
    nt_init(&s_table);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BSP_PIN_BUZZER) | (1ULL << BSP_PIN_LED_RED) |
                        (1ULL << BSP_PIN_LED_GREEN) | (1ULL << BSP_PIN_LED_YELLOW),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);
    esp_log_level_set("*", ESP_LOG_WARN);   /* keep the UART mostly JSON */

    sx1276_config_t cfg = {
        .host = BSP_LORA_SPI_HOST,
        .pin_sck = BSP_LORA_SCK, .pin_miso = BSP_LORA_MISO, .pin_mosi = BSP_LORA_MOSI,
        .pin_nss = BSP_LORA_NSS, .pin_rst = BSP_LORA_RST, .pin_dio0 = BSP_LORA_DIO0,
        .freq_hz = SN_LORA_FREQ_HZ, .sf = CONFIG_GATEWAY_LORA_SF, .bw_hz = SN_LORA_BW_HZ,
        .cr_denom = SN_LORA_CR_DENOM, .preamble = SN_LORA_PREAMBLE, .tx_dbm = 14,
        .sync_word = SN_LORA_SYNC_WORD,
    };
    s_lora_ok = sx1276_init(&cfg) == ESP_OK;
    espnow_init();

    const esp_app_desc_t *app = esp_app_get_description();
    out_line("{\"t\":\"BOOT\",\"fw\":\"%s\",\"lora\":%s}", app->version, s_lora_ok ? "true" : "false");

    xTaskCreatePinnedToCore(radio_task, "radio", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(supervisor_task, "supervisor", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(host_rx_task, "host_rx", 3072, NULL, 3, NULL, 1);
}
