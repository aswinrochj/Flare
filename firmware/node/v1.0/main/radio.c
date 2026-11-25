/*
 * Radio task (LoRa primary, ESP-NOW fallback started on demand) and heartbeat task.
 * Protocol: Technical Documentation Section V; timing: Table XI.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "bsp.h"
#include "eventlog.h"
#include "node.h"
#include "sx1276.h"

static const char *TAG = "radio";
static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static bool s_lora_ok;          /* set once in radio_init, before the tasks start */
static bool s_espnow_tried;     /* radio task only */
static bool s_espnow_ok;        /* radio task only */

static bool espnow_init(void)
{
#if CONFIG_SAFENODE_ESPNOW_FALLBACK
    esp_err_t e = esp_netif_init();
    if (e == ESP_OK) {
        e = esp_event_loop_create_default();
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (e == ESP_OK) e = esp_wifi_init(&cfg);
    if (e == ESP_OK) e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (e == ESP_OK) e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e == ESP_OK) e = esp_wifi_start();
    if (e == ESP_OK) e = esp_wifi_set_channel(CONFIG_SAFENODE_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
    if (e == ESP_OK) e = esp_now_init();
    if (e == ESP_OK) {
        esp_now_peer_info_t peer = { .channel = CONFIG_SAFENODE_ESPNOW_CHANNEL, .ifidx = WIFI_IF_STA, .encrypt = false };
        memcpy(peer.peer_addr, BROADCAST, ESP_NOW_ETH_ALEN);
        e = esp_now_add_peer(&peer);
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW unavailable: %s", esp_err_to_name(e));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool radio_init(void)
{
    sx1276_config_t cfg = {
        .host = BSP_LORA_SPI_HOST,
        .pin_sck = BSP_LORA_SCK, .pin_miso = BSP_LORA_MISO, .pin_mosi = BSP_LORA_MOSI,
        .pin_nss = BSP_LORA_NSS, .pin_rst = BSP_LORA_RST, .pin_dio0 = BSP_LORA_DIO0,
        .freq_hz = CFG_LORA_FREQ_HZ,
        .sf = CONFIG_SAFENODE_LORA_SF,
        .bw_hz = CFG_LORA_BW_HZ,
        .cr_denom = CFG_LORA_CR_DENOM,
        .preamble = CFG_LORA_PREAMBLE,
        .tx_dbm = CFG_LORA_TX_DBM,
        .sync_word = CFG_LORA_SYNC_WORD,
    };
    s_lora_ok = sx1276_init(&cfg) == ESP_OK;
    /* Wi-Fi (needed by ESP-NOW) stays off in normal operation to respect the
     * power budget (Tech. Doc. Table V, "ESP32 Wi-Fi off"). It is started the
     * first time an alert cannot be delivered over LoRa, and stays on while
     * the node is in that degraded mode. */
    return s_lora_ok;
}

/* Handle one received packet (beacon / ACK). Returns true if it was the ACK
 * for `expect_seq`. */
static bool handle_rx(node_ctx_t *ctx, const uint8_t *buf, int len, int expect_seq)
{
    if (len == SN_BEACON_LEN && sn_peek_type(buf, len) == SN_T_BEACON) {
        sn_beacon_t b;
        if (sn_beacon_decode(buf, (size_t)len, &b) == SN_OK) {
            TickType_t t = xTaskGetTickCount();
            xQueueOverwrite(ctx->beacon_mbx, &t);
        }
        return false;
    }
    sn_ack_t a;
    if (len == SN_ACK_LEN && sn_ack_decode(buf, (size_t)len, &a) == SN_OK && a.node_id == ctx->node_id) {
        if (a.seq == SN_SEQ_OPERATOR_ACK) {
            node_cmd_t c = { .id = CMD_OPERATOR_ACK };
            xQueueSend(ctx->cmd_q, &c, 0);
            return false;
        }
        return expect_seq >= 0 && a.seq == (uint16_t)expect_seq;
    }
    return false;   /* frames from other nodes are ignored */
}

static bool poll_rx(node_ctx_t *ctx, uint32_t timeout_ms, int expect_seq)
{
    if (!s_lora_ok) {
        if (timeout_ms) {
            vTaskDelay(pdMS_TO_TICKS(timeout_ms));
        }
        return false;
    }
    uint8_t buf[32];
    int n = sx1276_receive(buf, sizeof buf, timeout_ms, NULL);
    return n > 0 && handle_rx(ctx, buf, n, expect_seq);
}

static bool wait_ack(node_ctx_t *ctx, uint16_t seq)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(CFG_ACK_TIMEOUT_MS);
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        if ((int32_t)(deadline - now) <= 0) {
            return false;
        }
        if (poll_rx(ctx, pdTICKS_TO_MS(deadline - now), seq)) {
            return true;
        }
    }
}

static void build_frame(const node_ctx_t *ctx, const radio_msg_t *m, uint16_t seq, sn_frame_t *f)
{
    *f = (sn_frame_t){
        .node_id = ctx->node_id,
        .type = m->type,
        .seq = seq,
        .state = m->st.state,
        .flags = m->st.flags,
        .gas_x1000 = m->st.gas_x1000,
        .temp_x100 = m->st.temp_x100,
        .vbat_mv = m->st.vbat_mv,
        .reset_cause = ctx->reset_cause,
        .reset_count = ctx->reset_count,
        .uptime_s = m->st.uptime_s,
        .auth = 0,
    };
}

static void transmit(node_ctx_t *ctx, const radio_msg_t *m, uint16_t *seq_ctr)
{
    uint16_t seq = (*seq_ctr)++;
    if (*seq_ctr == SN_SEQ_OPERATOR_ACK) {
        *seq_ctr = 0;               /* 0xFFFF is reserved for operator ACKs */
    }
    sn_frame_t f;
    build_frame(ctx, m, seq, &f);
    uint8_t buf[SN_FRAME_LEN];
    sn_frame_encode(&f, buf);

    bool need_ack = m->type == SN_T_ALERT || m->type == SN_T_FAULT;
    bool delivered = false;

    if (s_lora_ok) {
        for (unsigned attempt = 0; attempt <= CFG_ALERT_RETRIES; attempt++) {
            if (attempt > 0 || sx1276_channel_busy(20)) {
                uint32_t backoff = CFG_BACKOFF_MIN_MS + esp_random() % (CFG_BACKOFF_MAX_MS - CFG_BACKOFF_MIN_MS + 1);
                vTaskDelay(pdMS_TO_TICKS(backoff));
            }
            if (sx1276_send(buf, sizeof buf, CFG_TX_TIMEOUT_MS) != ESP_OK) {
                continue;
            }
            if (!need_ack || wait_ack(ctx, seq)) {
                delivered = true;
                break;
            }
        }
    }

    if (!delivered && need_ack) {
        eventlog_add(EV_TX_FAIL, m->type, seq);
        if (!s_espnow_tried) {
            s_espnow_tried = true;
            s_espnow_ok = espnow_init();   /* ~100-300 ms, failure path only */
        }
        if (s_espnow_ok) {
            f.flags |= SN_F_VIA_ESPNOW;
            sn_frame_encode(&f, buf);
            if (esp_now_send(BROADCAST, buf, sizeof buf) == ESP_OK) {
                eventlog_add(EV_ESPNOW_SENT, m->type, seq);
            }
        }
    }
}

void radio_task(void *arg)
{
    node_ctx_t *ctx = arg;
    uint16_t seq = 0;
    bool muted = false;
    for (;;) {
        if (xEventGroupGetBits(ctx->debug) & DBG_MUTE_RADIO) {
            if (!muted) {
                ESP_LOGW(TAG, "radio muted (TC-3)");
                if (s_lora_ok) {
                    sx1276_sleep();
                }
                muted = true;
            }
            radio_msg_t drop;
            xQueueReceive(ctx->radio_q, &drop, pdMS_TO_TICKS(100));
            continue;
        }
        if (muted) {
            ESP_LOGW(TAG, "radio unmuted");
            if (s_lora_ok) {
                sx1276_start_rx();
            }
            muted = false;
        }

        radio_msg_t m;
        if (xQueueReceive(ctx->radio_q, &m, pdMS_TO_TICKS(10)) == pdTRUE) {
            transmit(ctx, &m, &seq);
        }
        poll_rx(ctx, 0, -1);
    }
}

/* Heartbeat in the node's own slot after the gateway beacon (slot = id x 150 ms),
 * or free-running every 3 s if no beacon has been heard. */
void heartbeat_task(void *arg)
{
    node_ctx_t *ctx = arg;
    const TickType_t period = pdMS_TO_TICKS(CFG_HB_PERIOD_MS);
    const TickType_t slot = pdMS_TO_TICKS(CFG_SLOT_MS * ctx->node_id);

    node_status_t st;
    xQueuePeek(ctx->status_mbx, &st, pdMS_TO_TICKS(1000));

    /* First packet after an abnormal reset reports the recovery (FR-15). */
    if (ctx->reset_cause != SN_RST_POWERON) {
        radio_msg_t m = { .type = SN_T_FAULT, .st = st };
        xQueueSend(ctx->radio_q, &m, 0);
    }

    TickType_t next = xTaskGetTickCount() + slot;
    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t beacon;
        if (xQueuePeek(ctx->beacon_mbx, &beacon, 0) == pdTRUE && (now - beacon) < 3 * period) {
            next = beacon + slot;
            while ((int32_t)(next - now) <= 0) {
                next += period;
            }
        }
        if ((int32_t)(next - now) > 0) {
            vTaskDelay(next - now);
        }
        if (xQueuePeek(ctx->status_mbx, &st, 0) == pdTRUE) {
            radio_msg_t m = { .type = SN_T_HEARTBEAT, .st = st };   /* real state, not a fixed "OK" (AR-6) */
            xQueueSend(ctx->radio_q, &m, 0);
        }
        next += period;
    }
}
