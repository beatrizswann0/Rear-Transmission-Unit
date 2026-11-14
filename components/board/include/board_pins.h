#pragma once

#include "soc/gpio_num.h"
#include "hal/uart_types.h"

/*
 * Pinout RTU — LoRa com módulo invertido na PCB (HW).
 * Sem GPS nesta placa — UART1 reservado ao A7670.
 *
 * Lacuna: A7670 RXD em GPIO35 é input-only no ESP32-WROOM-32 (ESP não TX).
 * SD: CS=23 MOSI=22 MISO=19 CLK=21 (M0 LoRa em GPIO18).
 */

/* ---- LEDs de debug ---- */
#define PIN_LED_STATUS          GPIO_NUM_2   /* D1 — saúde CAN */
#define PIN_LED_SD              GPIO_NUM_25  /* D2 — saúde LoRa (AUX) */

/* ---- LoRa Ebyte E32 (UART2) — bancada: fios cruzados vs silk antigo ---- */
#define PIN_E32_UART_NUM        UART_NUM_2
#define PIN_E32_TX              GPIO_NUM_17  /* ESP TX → E32 RXD (validado C1) */
#define PIN_E32_RX              GPIO_NUM_16  /* ESP RX ← E32 TXD (validado C1) */
#define PIN_E32_M0              GPIO_NUM_18
#define PIN_E32_M1              GPIO_NUM_5
#define PIN_E32_AUX             GPIO_NUM_4

/* ---- GPRS A7670SA (UART1, via LLC 1.8↔3.3) ---- */
#define PIN_A7670_UART_NUM      UART_NUM_1
#define PIN_A7670_TX            GPIO_NUM_32  /* ESP TX → modem RXD (HW a corrigir) */ /*tinha uma mensagem falando que precisava corrigir *bea */
#define PIN_A7670_RX            GPIO_NUM_33  /* ESP RX ← modem TXD (input-only ok) */ /*troquei para ficar mais proximo do gpio 32*/

/* ---- SD SPI (SPI2) ---- */
#define PIN_SD_MISO             GPIO_NUM_19
#define PIN_SD_MOSI             GPIO_NUM_22
#define PIN_SD_CLK              GPIO_NUM_21
#define PIN_SD_CS               GPIO_NUM_23

/* ---- CAN TWAI — TJA1050 ---- */
#define PIN_CAN_TX              GPIO_NUM_27  /* ESP TWAI TX → TJA TXD */
#define PIN_CAN_RX              GPIO_NUM_14  /* ESP TWAI RX ← TJA RXD */
