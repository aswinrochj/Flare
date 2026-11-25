/* Event log with NVS ring. SPDX-License-Identifier: MIT */
#include "eventlog.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "nvs.h"

#define RING_LEN 32

typedef struct {
    uint8_t head;
    uint8_t count;
    ev_t    ev[RING_LEN];
} ring_t;

static QueueHandle_t s_q;

esp_err_t eventlog_init(void)
{
    s_q = xQueueCreate(16, sizeof(ev_t));
    return s_q ? ESP_OK : ESP_ERR_NO_MEM;
}

void eventlog_add(ev_code_t code, uint8_t a, uint16_t b)
{
    ev_t ev = {
        .uptime_s = (uint32_t)(esp_timer_get_time() / 1000000),
        .code = (uint8_t)code,
        .a = a,
        .b = b,
    };
    xQueueSend(s_q, &ev, 0);   /* never block the caller */
}

int eventlog_take(ev_t *ev, uint32_t timeout_ms)
{
    return xQueueReceive(s_q, ev, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void eventlog_persist(const ev_t *ev)
{
    nvs_handle_t h;
    if (nvs_open("safenode", NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    static ring_t ring;            /* logger task only */
    size_t len = sizeof(ring);
    if (nvs_get_blob(h, "evlog", &ring, &len) != ESP_OK || len != sizeof(ring)) {
        memset(&ring, 0, sizeof(ring));
    }
    ring.ev[ring.head] = *ev;
    ring.head = (uint8_t)((ring.head + 1) % RING_LEN);
    if (ring.count < RING_LEN) {
        ring.count++;
    }
    nvs_set_blob(h, "evlog", &ring, sizeof(ring));
    nvs_commit(h);
    nvs_close(h);
}

const char *eventlog_name(uint8_t code)
{
    switch (code) {
    case EV_BOOT:          return "BOOT";
    case EV_STATE:         return "STATE";
    case EV_SELFTEST_FAIL: return "SELFTEST_FAIL";
    case EV_TX_FAIL:       return "TX_FAIL";
    case EV_ESPNOW_SENT:   return "ESPNOW_SENT";
    case EV_OPERATOR_ACK:  return "OPERATOR_ACK";
    case EV_CALIBRATED:    return "CALIBRATED";
    case EV_DRIFT:         return "BASELINE_DRIFT";
    case EV_INJECT:        return "INJECT";
    default:               return "?";
    }
}

void eventlog_dump(void)
{
    nvs_handle_t h;
    ring_t ring;
    size_t len = sizeof(ring);
    if (nvs_open("safenode", NVS_READONLY, &h) != ESP_OK) {
        printf("[LOG] empty\n");
        return;
    }
    if (nvs_get_blob(h, "evlog", &ring, &len) != ESP_OK || len != sizeof(ring)) {
        nvs_close(h);
        printf("[LOG] empty\n");
        return;
    }
    nvs_close(h);
    uint8_t start = (uint8_t)((ring.head + RING_LEN - ring.count) % RING_LEN);
    for (uint8_t i = 0; i < ring.count; i++) {
        const ev_t *e = &ring.ev[(start + i) % RING_LEN];
        printf("[LOG] t=%lus %s a=%u b=%u\n", (unsigned long)e->uptime_s, eventlog_name(e->code), e->a, e->b);
    }
}
