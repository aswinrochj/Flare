/*
 * Board-support header for the SAFE-NODE carrier PCB rev 1.0 with an
 * ESP32-DevKitC V4 (WROOM-32). All pin, ADC and radio wiring for one board
 * lives here, like a device-tree fragment (DPR Section IV-C), so the
 * application compiles unchanged for another ESP32 board.
 *
 * Pin map: README "Hardware at a glance" and Technical Documentation Table IV.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "hal/adc_types.h"

/* Analog inputs: ADC1 only, because ADC2 is shared with Wi-Fi / ESP-NOW */
#define BSP_ADC_UNIT          ADC_UNIT_1
#define BSP_ADC_ATTEN         ADC_ATTEN_DB_12
#define BSP_ADC_GAS_CH        ADC_CHANNEL_4     /* GPIO32, MQ-2 AO via R1/R2, C4 */
#define BSP_ADC_FLAME_CH      ADC_CHANNEL_5     /* GPIO33, flame AO, C5          */
#define BSP_ADC_VBAT_CH       ADC_CHANNEL_7     /* GPIO35, VBAT via R8/R9, C7    */

#define BSP_PIN_DHT           GPIO_NUM_4        /* J4 pin 2, R7 pull-up          */
#define BSP_PIN_BUZZER        GPIO_NUM_13       /* R6 -> Q1 base                 */
#define BSP_PIN_LED_RED       GPIO_NUM_14       /* D1 hazard                     */
#define BSP_PIN_LED_GREEN     GPIO_NUM_16       /* D2 healthy                    */
#define BSP_PIN_LED_YELLOW    GPIO_NUM_17       /* D3 fault / warning            */

/* RFM95W (SX1276) on VSPI */
#define BSP_LORA_SPI_HOST     SPI3_HOST
#define BSP_LORA_SCK          GPIO_NUM_18
#define BSP_LORA_MISO         GPIO_NUM_19
#define BSP_LORA_MOSI         GPIO_NUM_23
#define BSP_LORA_NSS          GPIO_NUM_5
#define BSP_LORA_RST          GPIO_NUM_27
#define BSP_LORA_DIO0         GPIO_NUM_26
