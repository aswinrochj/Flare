/*
 * SAFE-NODE sensor node firmware v1.0 (ESP-IDF v5 + FreeRTOS).
 *
 * Task set (DPR Table III):
 *   sensor     prio 5, core 1, 100 ms    sample, filter, classify, self-test
 *   alarm      prio 5, core 1, notified  siren, LEDs, latched DANGER
 *   health     prio 4, core 1, 500 ms    check alive bits, feed watchdog
 *   radio      prio 4, core 0, queue     LoRa TX/RX, ACK, retry, ESP-NOW
 *   heartbeat  prio 3, core 0, 3 s slot  status, battery, reset cause
 *   diag       prio 3, core 0, 2 s       DHT22
 *   logger     prio 1, core 0, idle      NVS event ring, UART console
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "bsp.h"
#include "classifier.h"
#include "dht22.h"
#include "eventlog.h"
#include "node.h"
#include "outputs.h"
#include "selftest.h"
#include "sensors.h"

static const char *TAG = "node";

/* Survives software, panic and watchdog resets; cleared on power-on. */
RTC_NOINIT_ATTR static uint32_t s_rtc_magic;
RTC_NOINIT_ATTR static uint32_t s_rtc_reset_count;
#define RTC_MAGIC 0x5AFE0DE1u

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static uint8_t map_reset(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:   return SN_RST_POWERON;
    case ESP_RST_SW:        return SN_RST_SW;
    case ESP_RST_PANIC:     return SN_RST_PANIC;
    case ESP_RST_TASK_WDT:  return SN_RST_TASK_WDT;
    case ESP_RST_INT_WDT:
    case ESP_RST_WDT:       return SN_RST_INT_WDT;
    case ESP_RST_BROWNOUT:  return SN_RST_BROWNOUT;
    case ESP_RST_DEEPSLEEP: return SN_RST_DEEPSLEEP;
    case ESP_RST_EXT:       return SN_RST_EXT;
    default:                return SN_RST_OTHER;
    }
}

/* ------------------------------------------------------------------ */
static void sensor_task(void *arg)
{
    node_ctx_t *ctx = arg;
    classifier_t cls;
    cls_init(&cls, now_ms(), ctx->selftest_fault, ctx->boot_dht.ok, ctx->boot_dht.temp_x100);

    uint8_t injected = 0;
    uint16_t gas_override = 0;
    bool operator_ack = false;
    bool drift_reported = false;
    uint8_t last_state = 0xFF;
    const uint8_t report_flags = SN_F_GAS_FAULT | SN_F_TEMP_FAULT | SN_F_FLAME_FAULT | SN_F_LOW_BATT;
    uint8_t last_flags = 0;
    const int probe = CONFIG_SAFENODE_LATENCY_PROBE_GPIO;

    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CFG_SAMPLE_PERIOD_MS));

        if (xEventGroupGetBits(ctx->debug) & DBG_HANG_SENSOR) {
            ESP_LOGW(TAG, "sensor task hung on request (TC-4)");
            for (;;) {
                vTaskDelay(portMAX_DELAY);
            }
        }

        sensor_sample_t s;
        sensors_read(&s);

        cls_input_t in = {
            .now_ms = now_ms(),
            .gas_ok = s.gas_ok, .gas_raw = s.gas_raw, .gas_pin_mv = s.gas_pin_mv,
            .gas_ao_mv = s.gas_ao_mv, .gas_x1000 = s.gas_x1000,
            .flame_ok = s.flame_ok, .flame_raw = s.flame_raw, .flame_active = s.flame_active,
            .vbat_mv = s.vbat_mv,
        };

        dht_result_t d;
        if (xQueueReceive(ctx->dht_mbx, &d, 0) == pdTRUE) {
            in.temp_new = true;
            in.temp_ok = d.ok;
            in.temp_x100 = d.temp_x100;
        }

        node_cmd_t cmd;
        while (xQueueReceive(ctx->cmd_q, &cmd, 0) == pdTRUE) {
            switch (cmd.id) {
            case CMD_OPERATOR_ACK:
                operator_ack = true;
                eventlog_add(EV_OPERATOR_ACK, 0, 0);
                break;
            case CMD_TOGGLE_FAULT:
                injected ^= (uint8_t)cmd.arg;
                eventlog_add(EV_INJECT, (uint8_t)cmd.arg, injected);
                break;
            case CMD_GAS_OVERRIDE:
                gas_override = (uint16_t)cmd.arg;
                eventlog_add(EV_INJECT, 0x80, gas_override);
                break;
            case CMD_CALIBRATE: {
                esp_err_t e = sensors_calibrate(50);
                printf("[CAL] %s\n", e == ESP_OK ? "R0 stored" : esp_err_to_name(e));
                if (e == ESP_OK) {
                    eventlog_add(EV_CALIBRATED, 0, (uint16_t)sensors_r0_x1000());
                }
                break;
            }
            }
        }
        if (gas_override) {
            in.gas_x1000 = gas_override;
        }
        in.injected_faults = injected;
        in.operator_ack = operator_ack;
        operator_ack = false;

        cls_output_t o = cls_step(&cls, &in);

        /* Wake the alarm task at once (direct-to-task notification). */
        xTaskNotify(ctx->alarm_task, (uint32_t)o.state | ((uint32_t)o.flags << 8), eSetValueWithOverwrite);
        if (probe >= 0) {
            gpio_set_level(probe, o.state == SN_ST_DANGER);
        }

        node_status_t st = {
            .state = o.state, .flags = o.flags, .cause = o.cause,
            .gas_x1000 = in.gas_x1000, .gas_raw = s.gas_raw, .flame_raw = s.flame_raw,
            .temp_x100 = o.temp_x100, .vbat_mv = s.vbat_mv,
            .uptime_s = in.now_ms / 1000,
        };
        xQueueOverwrite(ctx->status_mbx, &st);

        bool flags_event = (o.flags & report_flags) != (last_flags & report_flags);
        if (o.state != last_state || flags_event) {
            radio_msg_t m = { .st = st };
            switch (o.state) {
            case SN_ST_DANGER:  m.type = SN_T_ALERT; break;
            case SN_ST_WARNING: m.type = SN_T_WARNING; break;
            case SN_ST_FAULT:   m.type = SN_T_FAULT; break;
            default:            m.type = SN_T_HEARTBEAT; break;   /* back to NORMAL: report now */
            }
            if (xQueueSend(ctx->radio_q, &m, 0) != pdTRUE) {
                ESP_LOGW(TAG, "radio queue full");
            }
            if (o.state != last_state) {
                eventlog_add(EV_STATE, o.state, o.cause);
            }
            last_state = o.state;
            last_flags = o.flags;
        }

        bool drift = sensors_track_baseline(&s, o.state == SN_ST_NORMAL);
        if (drift && !drift_reported) {
            eventlog_add(EV_DRIFT, 0, 0);   /* maintenance flag: recalibrate */
        }
        drift_reported = drift;

        xEventGroupSetBits(ctx->alive, ALIVE_SENSOR);
    }
}

/* ------------------------------------------------------------------ */
static void alarm_task(void *arg)
{
    node_ctx_t *ctx = arg;
    uint8_t state = SN_ST_FAULT;   /* until the first classification arrives */
    for (;;) {
        uint32_t v;
        if (xTaskNotifyWait(0, UINT32_MAX, &v, pdMS_TO_TICKS(CFG_SAMPLE_PERIOD_MS)) == pdTRUE) {
            state = (uint8_t)(v & 0xFF);
        }
        if (xEventGroupGetBits(ctx->debug) & DBG_HANG_ALARM) {
            ESP_LOGW(TAG, "alarm task hung on request (TC-4)");
            for (;;) {
                vTaskDelay(portMAX_DELAY);
            }
        }
        bool blink = ((xTaskGetTickCount() / pdMS_TO_TICKS(500)) & 1) != 0;
        outputs_apply(state, blink);
        xEventGroupSetBits(ctx->alive, ALIVE_ALARM);
    }
}

/* ------------------------------------------------------------------ */
/* The only task subscribed to the task watchdog. It feeds it only when both
 * safety tasks have checked in since the previous check (FR-14). */
static void health_task(void *arg)
{
    node_ctx_t *ctx = arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    bool warned = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CFG_HEALTH_PERIOD_MS));
        EventBits_t bits = xEventGroupClearBits(ctx->alive, ALIVE_ALL);  /* returns bits before clearing */
        if ((bits & ALIVE_ALL) == ALIVE_ALL) {
            esp_task_wdt_reset();
            warned = false;
        } else if (!warned) {
            ESP_LOGE(TAG, "safety task silent (sensor=%d alarm=%d): watchdog will reset",
                     (bits & ALIVE_SENSOR) != 0, (bits & ALIVE_ALARM) != 0);
            warned = true;
        }
    }
}

/* ------------------------------------------------------------------ */
static void diag_task(void *arg)
{
    node_ctx_t *ctx = arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CFG_DHT_PERIOD_MS));
        dht_result_t r = { 0 };
        r.ok = dht22_read(BSP_PIN_DHT, &r.temp_x100, &r.hum_x10) == ESP_OK;
        xQueueOverwrite(ctx->dht_mbx, &r);
    }
}

/* ------------------------------------------------------------------ */
void app_main(void)
{
    static node_ctx_t ctx;   /* filled here, read-only for the tasks afterwards */

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_POWERON || s_rtc_magic != RTC_MAGIC) {
        s_rtc_magic = RTC_MAGIC;
        s_rtc_reset_count = 0;
    } else {
        s_rtc_reset_count++;
    }

    ctx.node_id = CONFIG_SAFENODE_NODE_ID;
    ctx.reset_cause = map_reset(rr);
    ctx.reset_count = (uint8_t)(s_rtc_reset_count & 0x0F);

    const esp_app_desc_t *app = esp_app_get_description();
    printf("\n==============================================\n");
    printf(" SAFE-NODE sensor node  firmware %s\n", app->version);
    printf(" built %s %s, ESP-IDF %s\n", app->date, app->time, app->idf_ver);
    printf(" node id %u, reset cause %u, resets since power-on %lu\n", ctx.node_id, ctx.reset_cause,
           (unsigned long)s_rtc_reset_count);
    printf(" Student prototype: NOT a certified gas or fire detector.\n");
    printf("==============================================\n");

    ESP_ERROR_CHECK(eventlog_init());
    eventlog_add(EV_BOOT, ctx.reset_cause, (uint16_t)s_rtc_reset_count);

    outputs_init();
    outputs_apply(SN_ST_FAULT, true);   /* yellow until the node can vouch for itself */
    sensors_init();
    dht22_init(BSP_PIN_DHT);
    if (CONFIG_SAFENODE_LATENCY_PROBE_GPIO >= 0) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << CONFIG_SAFENODE_LATENCY_PROBE_GPIO, .mode = GPIO_MODE_OUTPUT };
        gpio_config(&io);
        gpio_set_level(CONFIG_SAFENODE_LATENCY_PROBE_GPIO, 0);
    }
    ctx.radio_ok = radio_init();

    vTaskDelay(pdMS_TO_TICKS(2000));    /* DHT22 needs 2 s after power-up */
    selftest_run(&ctx);

    ctx.radio_q = xQueueCreate(CFG_TELEMETRY_QUEUE_LEN, sizeof(radio_msg_t));
    ctx.status_mbx = xQueueCreate(1, sizeof(node_status_t));
    ctx.dht_mbx = xQueueCreate(1, sizeof(dht_result_t));
    ctx.beacon_mbx = xQueueCreate(1, sizeof(TickType_t));
    ctx.cmd_q = xQueueCreate(4, sizeof(node_cmd_t));
    ctx.alive = xEventGroupCreate();
    ctx.debug = xEventGroupCreate();
    configASSERT(ctx.radio_q && ctx.status_mbx && ctx.dht_mbx && ctx.beacon_mbx && ctx.cmd_q && ctx.alive && ctx.debug);

    /* Core 1: safety tasks. Core 0: radio stacks, heartbeat, logger. */
    xTaskCreatePinnedToCore(alarm_task, "alarm", 3072, &ctx, 5, &ctx.alarm_task, 1);
    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, &ctx, 5, NULL, 1);
    xTaskCreatePinnedToCore(health_task, "health", 3072, &ctx, 4, NULL, 1);
    xTaskCreatePinnedToCore(radio_task, "radio", 4096, &ctx, 4, NULL, 0);
    xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", 3072, &ctx, 3, NULL, 0);
    xTaskCreatePinnedToCore(diag_task, "diag", 3072, &ctx, 3, NULL, 0);
    xTaskCreatePinnedToCore(logger_task, "logger", 4096, &ctx, 1, NULL, 0);

    printf("[SYSTEM] tasks started%s\n", ctx.selftest_fault ? " (FAULT: see [CHECK] lines)" : "");
}
