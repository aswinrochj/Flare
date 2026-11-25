/*
 * Logger task: prints events and a once-per-second status line (AR-10),
 * stores events in the NVS ring, and runs the UART console.
 *
 * Console commands (115200 baud, end with Enter):
 *   help | status | log | ver | ack | cal | reboot
 *   Debug build only (CONFIG_SAFENODE_DEBUG_CONSOLE), failure injection:
 *   hang sensor | hang alarm   stop a safety task  -> watchdog reset (TC-4)
 *   flood <s>                  logger uses 100 % of idle time for <s> s (TC-6)
 *   mute | unmute              silence the radio  -> gateway OFFLINE (TC-3)
 *   fault gas|flame|temp       toggle an injected sensor fault (SIM-6, TC-5)
 *   gas <ratio> | gas off      override Rs/R0, e.g. "gas 0.40" (TC-1 dry run)
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "app_config.h"
#include "classifier.h"
#include "eventlog.h"
#include "node.h"
#include "sensors.h"

static const char *TAG = "console";

static void print_status(const node_ctx_t *ctx)
{
    node_status_t st;
    if (xQueuePeek(ctx->status_mbx, &st, 0) != pdTRUE) {
        return;
    }
    printf("[STATUS] %s (%s) gas Rs/R0 %.3f raw %u | flame %u%s | T %.2f C | Vbat %u mV | flags 0x%02x | up %lus\n",
           sn_state_name(st.state), cls_cause_name(st.cause), st.gas_x1000 / 1000.0, st.gas_raw, st.flame_raw,
           (st.flags & SN_F_FLAME) ? " FLAME" : "", st.temp_x100 / 100.0, st.vbat_mv, st.flags,
           (unsigned long)st.uptime_s);
}

static void send_cmd(const node_ctx_t *ctx, cmd_id_t id, uint32_t arg)
{
    node_cmd_t c = { .id = id, .arg = arg };
    if (xQueueSend(ctx->cmd_q, &c, pdMS_TO_TICKS(100)) != pdTRUE) {
        printf("[CONSOLE] busy, try again\n");
    }
}

static void help(void)
{
    printf("commands: help status log ver ack cal reboot\n");
#if CONFIG_SAFENODE_DEBUG_CONSOLE
    printf("debug:    hang sensor|alarm  flood <s>  mute  unmute  fault gas|flame|temp  gas <ratio>|off\n");
#endif
}

static void handle(node_ctx_t *ctx, char *line, int64_t *flood_until_us)
{
    char *cmd = strtok(line, " \t\r\n");
    char *arg = strtok(NULL, " \t\r\n");
    if (!cmd) {
        return;
    }
    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        help();
    } else if (!strcmp(cmd, "status")) {
        print_status(ctx);
    } else if (!strcmp(cmd, "log")) {
        eventlog_dump();
    } else if (!strcmp(cmd, "ver")) {
        printf("[VER] %s\n", esp_app_get_description()->version);
    } else if (!strcmp(cmd, "ack")) {
        send_cmd(ctx, CMD_OPERATOR_ACK, 0);
        printf("[CONSOLE] operator acknowledge sent\n");
    } else if (!strcmp(cmd, "cal")) {
        printf("[CAL] capturing R0: the air around the sensor must be clean\n");
        send_cmd(ctx, CMD_CALIBRATE, 0);
    } else if (!strcmp(cmd, "reboot")) {
        esp_restart();
#if CONFIG_SAFENODE_DEBUG_CONSOLE
    } else if (!strcmp(cmd, "hang") && arg) {
        EventBits_t b = !strcmp(arg, "sensor") ? DBG_HANG_SENSOR : !strcmp(arg, "alarm") ? DBG_HANG_ALARM : 0;
        if (b) {
            printf("[INJECT] hanging %s task; expect a watchdog reset within 5 s\n", arg);
            xEventGroupSetBits(ctx->debug, b);
        }
    } else if (!strcmp(cmd, "flood")) {
        int s = arg ? atoi(arg) : 10;
        *flood_until_us = esp_timer_get_time() + (int64_t)s * 1000000;
        printf("[INJECT] logger flooding for %d s\n", s);
    } else if (!strcmp(cmd, "mute")) {
        xEventGroupSetBits(ctx->debug, DBG_MUTE_RADIO);
    } else if (!strcmp(cmd, "unmute")) {
        xEventGroupClearBits(ctx->debug, DBG_MUTE_RADIO);
    } else if (!strcmp(cmd, "fault") && arg) {
        uint32_t f = !strcmp(arg, "gas") ? SN_F_GAS_FAULT : !strcmp(arg, "flame") ? SN_F_FLAME_FAULT
                   : !strcmp(arg, "temp") ? SN_F_TEMP_FAULT : 0;
        if (f) {
            send_cmd(ctx, CMD_TOGGLE_FAULT, f);
            printf("[INJECT] toggled %s fault\n", arg);
        }
    } else if (!strcmp(cmd, "gas") && arg) {
        uint32_t v = !strcmp(arg, "off") ? 0 : (uint32_t)(atof(arg) * 1000.0);
        send_cmd(ctx, CMD_GAS_OVERRIDE, v > 0xFFFE ? 0xFFFE : v);
        printf("[INJECT] gas override %s\n", v ? arg : "off");
#endif
    } else {
        printf("[CONSOLE] unknown command '%s'\n", cmd);
        help();
    }
}

void logger_task(void *arg)
{
    node_ctx_t *ctx = arg;
    if (uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0) != ESP_OK) {
        ESP_LOGW(TAG, "console input unavailable");
    }
    help();

    char line[64];
    size_t pos = 0;
    int64_t flood_until = 0;
    int64_t next_status = esp_timer_get_time();

    for (;;) {
        ev_t ev;
        while (eventlog_take(&ev, 0)) {
            printf("[EVENT] t=%lus %s a=%u b=%u\n", (unsigned long)ev.uptime_s, eventlog_name(ev.code), ev.a, ev.b);
            eventlog_persist(&ev);
        }

        int64_t now = esp_timer_get_time();
        if (now >= next_status) {
            print_status(ctx);
            next_status = now + (int64_t)CFG_STATUS_PRINT_MS * 1000;
        }

        /* TC-6: burn every idle cycle on core 0 without yielding. Higher-priority
         * tasks pre-empt this loop, which is exactly what the test checks. */
        if (now < flood_until) {
            volatile uint32_t x = 0;
            for (uint32_t i = 0; i < 200000; i++) {
                x += i;
            }
            continue;
        }

        uint8_t c;
        if (uart_read_bytes(UART_NUM_0, &c, 1, pdMS_TO_TICKS(20)) == 1) {
            if (c == '\n' || c == '\r') {
                line[pos] = 0;
                if (pos) {
                    handle(ctx, line, &flood_until);
                }
                pos = 0;
            } else if (pos < sizeof line - 1) {
                line[pos++] = (char)c;
            }
        }
    }
}
