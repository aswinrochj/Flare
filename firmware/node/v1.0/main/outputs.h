/* LEDs and buzzer. SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

void outputs_init(void);
/* Drive the outputs for a node state (FR-05). `blink_phase` toggles the
 * yellow LED in FAULT so a fault (blinking) differs from WARNING (steady). */
void outputs_apply(uint8_t state, bool blink_phase);
/* Drive one pin and read the pad back; true if it follows (self-test). */
bool outputs_pin_check(int gpio);
void outputs_all_off(void);
