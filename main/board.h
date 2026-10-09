/*
 * board.h — pinovi front panela. Ista dodjela na prototipu (YD-ESP32-S3 N16R8, klon
 * DevKitC-1) i na nasoj PCB sa modulom DFR0896 = ESP32-S3-WROOM-1-N4 (4 MB, bez PSRAM-a;
 * firmver ga ne koristi). Raspored po letvicama dev ploce, sema glavne ploce i plocice
 * sa tasterima: docs/plan_qr_poklon_pranje.md (CWS repo), sekcija "Raspored pinova".
 *
 * Izbjegavati: GPIO43/44 (CH343 USB-UART na dev ploci), GPIO48 (RGB LED), GPIO35-37
 * (octal PSRAM kod N16R8), GPIO19/20 (USB-Serial-JTAG = konzola), GPIO0/3/45/46 (strapping).
 * GPIO38 (LED1) je na DevKitC-1 v1.1 RGB LED — bezopasno, WS2812 ulaz je visoke impedanse.
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
 * Front panel — dvije varijante na ISTIM GPIO pinovima, svaka na svom konektoru glavne
 * ploce (prikljucena je samo jedna):
 *
 * A: J_FP 2x10 IDC — plocica Touch_5T_4cm (izvedena iz Touch_7T_4cm, raspored 3+2),
 *    isti X2 raspored kao tasteri boksa CWS ploce:
 *
 *   J_FP | signal | GPIO | znacenje
 *   -----+--------+------+-------------------------------------------------------------
 *    3   | T1     |   1  | taster 1 KM   (BC817 open-collector, aktivno LOW)
 *    4   | LT1    |  38  | LED prsten 1 KM (low-side MOSFET na glavnoj ploci, HIGH = upaljen)
 *    5   | T2     |   2  | taster 2 KM
 *    6   | LT2    |  39  | LED prsten 2 KM
 *    7   | T3     |   4  | taster 5 KM
 *    8   | LT3    |  40  | LED prsten 5 KM
 *    9   | T4     |   5  | PONISTI
 *   10   | LT4    |  41  | LED prsten PONISTI
 *   11   | T5     |   6  | PLACANJE
 *   12   | LT5    |  42  | LED prsten PLACANJE
 *   19   | GND    |      |
 *   20   | +12V   |      | LED prstenovi (anode) + REG1117 -> 3V3 za TTP223 na plocici
 *   1, 2, 13-18: NC (kod Touch_7T: T_STOP/LT_STOP, T6/LT6, 3V3 spolja)
 *
 *    Na glavnoj ploci: 10 k pull-up na 3V3 na T1-T5 (ujedno I2C pull-up za B) i AO3400
 *    po LED liniji (gate 100 R iz GPIO38-42, 100 k pull-down).
 *
 * B: J_LCD 2x8 — SPI displej (npr. 3.5" 320x480 ST7796) + kapacitivni touch na I2C
 *    (FT6336/GT911/CST816), pinovi ispod (LCD_*).
 *
 * Varijantu bira panel_variant.c: NVS "panel" = auto (default) / ttp / lcd / pads;
 * auto = touch kontroler displeja na I2C (GPIO1/2) -> lcd, inace ttp. "pads" (ESP32
 * touch na golim padovima, shield GPIO14) ostaje u firmveru, ali za njega nema plocice.
 */
#define KEY_COUNT       5
#define KEY_GPIOS       { 1, 2, 4, 5, 6 }   /* 1 KM, 2 KM, 5 KM, PONISTI, PLACANJE */
#define KEY_TOUCH_CHANS KEY_GPIOS
#define KEY_TTP_ACTIVE_LEVEL 0              /* BC817 open-collector na Touch_5T */
#define KEY_LED_GPIOS   { 38, 39, 40, 41, 42 }
#define TOUCH_USE_SHIELD 1                   /* samo "pads": GPIO14 = shield elektroda */

/* B: J_LCD 2x8 (1 +5V, 2 +3V3, 3/4/16 GND; ostali pinovi u komentarima) */
#define LCD_TP_SDA_GPIO 1      /* J_LCD 5  (10 k pull-up na glavnoj ploci) */
#define LCD_TP_SCL_GPIO 2      /* J_LCD 6  (10 k pull-up) */
#define LCD_TP_INT_GPIO 4      /* J_LCD 7 */
#define LCD_TP_RST_GPIO 5      /* J_LCD 8  (10 k pull-up; probe ga vozi open-drain) */
#define LCD_DC_GPIO     6      /* J_LCD 9 */
#define LCD_RST_GPIO    7      /* J_LCD 10 */
#define LCD_CS_GPIO     10     /* J_LCD 11 (FSPI IOMUX — puna brzina SPI-ja) */
#define LCD_MOSI_GPIO   11     /* J_LCD 12 (FSPI IOMUX) */
#define LCD_SCK_GPIO    12     /* J_LCD 13 (FSPI IOMUX) */
#define LCD_MISO_GPIO   13     /* J_LCD 14 (FSPI IOMUX, opciono) */
#define LCD_BL_GPIO     14     /* J_LCD 15 (PWM) */
