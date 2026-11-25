/* LEDs and buzzer. SPDX-License-Identifier: MIT */
#include "outputs.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "bsp.h"
#include "sn_proto.h"

void outputs_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BSP_PIN_BUZZER) | (1ULL << BSP_PIN_LED_RED) |
                        (1ULL << BSP_PIN_LED_GREEN) | (1ULL << BSP_PIN_LED_YELLOW),
        .mode = GPIO_MODE_INPUT_OUTPUT,   /* input enabled so the self-test can read the pad */
    };
    gpio_config(&io);
    outputs_all_off();
}

void outputs_all_off(void)
{
    gpio_set_level(BSP_PIN_BUZZER, 0);
    gpio_set_level(BSP_PIN_LED_RED, 0);
    gpio_set_level(BSP_PIN_LED_GREEN, 0);
    gpio_set_level(BSP_PIN_LED_YELLOW, 0);
}

void outputs_apply(uint8_t state, bool blink_phase)
{
    bool red = false, green = false, yellow = false, buzz = false;
    switch (state) {
    case SN_ST_DANGER:  red = true; buzz = true; break;   /* hazard over fault (FR-06) */
    case SN_ST_FAULT:   yellow = blink_phase; break;       /* buzzer off */
    case SN_ST_WARNING: yellow = true; break;
    case SN_ST_NORMAL:  green = true; break;
    default:            yellow = blink_phase; break;
    }
    gpio_set_level(BSP_PIN_LED_RED, red);
    gpio_set_level(BSP_PIN_LED_GREEN, green);
    gpio_set_level(BSP_PIN_LED_YELLOW, yellow);
    gpio_set_level(BSP_PIN_BUZZER, buzz);
}

bool outputs_pin_check(int gpio)
{
    gpio_set_level(gpio, 1);
    esp_rom_delay_us(50);
    bool hi = gpio_get_level(gpio) == 1;
    gpio_set_level(gpio, 0);
    esp_rom_delay_us(50);
    bool lo = gpio_get_level(gpio) == 0;
    return hi && lo;
}
