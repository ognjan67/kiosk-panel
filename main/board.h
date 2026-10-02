/*
 * board.h — pinovi front panela (YD-ESP32-S3 N16R8 dev ploca; ista dodjela na budućoj PCB).
 * Vidi docs/plan_qr_poklon_pranje.md (CWS repo, grana qr-poklon).
 *
 * Izbjegavati: GPIO43/44 (CH343 USB-UART na dev ploci), GPIO48 (RGB LED), GPIO35-37
 * (octal PSRAM kod N16R8), GPIO19/20 (USB-Serial-JTAG = konzola), GPIO0/3/45/46 (strapping).
 */
#pragma once

#include "driver/uart.h"

/* Link ka CWS ploci kioska (USART3, 3V3 TTL direktno) */
#define LINK_UART       UART_NUM_0
#define LINK_TX_GPIO    21
#define LINK_RX_GPIO    47
#define LINK_BAUD       9600

/* Payten POS terminal (RS-232 preko MAX3232) */
#define POS_UART        UART_NUM_1
#define POS_TX_GPIO     17
#define POS_RX_GPIO     18
#define POS_BAUD        9600

/* DFR0660 / GM65 QR citac (5 V; TX citaca kroz level shifter na RX) */
#define QR_UART         UART_NUM_2
#define QR_TX_GPIO      8
#define QR_RX_GPIO      9
#define QR_BAUD         9600

/*
 * Front panel konektor — ISTI pinovi, dvije varijante (nikad obje istovremeno):
 *
 *   GPIO | A: touch tasteri (PANEL_PADS) | B: SPI displej + cap. touch (PANEL_LCD)
 *   -----+-------------------------------+------------------------------------------
 *     1  | taster 1 KM                   | touch I2C SDA
 *     2  | taster 2 KM                   | touch I2C SCL
 *     4  | taster 5 KM                   | touch INT
 *     5  | PONISTI                       | touch RST
 *     6  | PLACANJE                      | LCD DC
 *     7  | —                             | LCD RST
 *    10  | —                             | LCD CS    (FSPI IOMUX — puna brzina SPI-ja)
 *    11  | —                             | LCD MOSI  (FSPI IOMUX)
 *    12  | —                             | LCD SCK   (FSPI IOMUX)
 *    13  | —                             | LCD MISO  (FSPI IOMUX, opciono)
 *    14  | shield elektroda              | LCD pozadinsko svjetlo (PWM)
 *  38-42 | LED ispod tastera             | slobodno
 *        + 3V3, GND
 *
 * Varijantu bira panel_variant.c: NVS "panel" = auto (default) / pads / lcd; auto =
 * probe touch kontrolera displeja na I2C (GPIO1/2).
 */

/* A: kapacitivni tasteri iza pleksija — touch kanal N = GPIO N na ESP32-S3 */
#define KEY_COUNT       5
#define KEY_TOUCH_CHANS { 1, 2, 4, 5, 6 }   /* 1 KM, 2 KM, 5 KM, PONISTI, PLACANJE */
#define KEY_LED_GPIOS   { 38, 39, 40, 41, 42 }
#define TOUCH_USE_SHIELD 1                   /* GPIO14 = shield elektroda (voda na pleksiju) */

/* B: SPI displej (npr. 3.5" 320x480 ST7796) + kapacitivni touch na I2C (FT6336/GT911/CST816) */
#define LCD_TP_SDA_GPIO 1
#define LCD_TP_SCL_GPIO 2
#define LCD_TP_INT_GPIO 4
#define LCD_TP_RST_GPIO 5
#define LCD_DC_GPIO     6
#define LCD_RST_GPIO    7
#define LCD_CS_GPIO     10
#define LCD_MOSI_GPIO   11
#define LCD_SCK_GPIO    12
#define LCD_MISO_GPIO   13
#define LCD_BL_GPIO     14
