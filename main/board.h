/*
 * board.h — pinovi front panela. Ista dodjela na prototipu (YD-ESP32-S3 N16R8, klon
 * DevKitC-1) i na nasoj PCB sa modulom DFR0896 = ESP32-S3-WROOM-1-N4 (4 MB, bez PSRAM-a;
 * firmver ga ne koristi). Raspored po letvicama dev ploce i sema plocice sa padovima:
 * docs/plan_qr_poklon_pranje.md (CWS repo), sekcija "Raspored pinova".
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
 * Front panel konektor 2x10 IDC 2,54 mm — ISTI pinovi, dvije varijante (nikad obje):
 *
 *   kon.  | GPIO | A: tasteri (PADS / TTP)   | B: SPI displej + cap. touch (PANEL_LCD)
 *   ------+------+---------------------------+------------------------------------------
 *     1   |  —   | +5V (LED preko MOSFET-a)  | +5V
 *     2   |  —   | +3V3                      | +3V3
 *    3,4  |  —   | GND                       | GND
 *     5   |   1  | K1 taster 1 KM            | touch I2C SDA
 *     6   |   2  | K2 taster 2 KM            | touch I2C SCL
 *     7   |   4  | K3 taster 5 KM            | touch INT
 *     8   |   5  | K4 PONISTI                | touch RST
 *     9   |   6  | K5 PLACANJE               | LCD DC
 *    10   |  14  | shield (samo ESP touch)   | LCD pozadinsko svjetlo (PWM)
 *  11-15  | 38-42| LED1-LED5 ispod tastera   | slobodno
 *    16   |   7  | —                         | LCD RST
 *    17   |  10  | —                         | LCD CS    (FSPI IOMUX — puna brzina SPI-ja)
 *    18   |  11  | —                         | LCD MOSI  (FSPI IOMUX)
 *    19   |  12  | —                         | LCD SCK   (FSPI IOMUX)
 *    20   |  13  | —                         | LCD MISO  (FSPI IOMUX, opciono)
 *
 * Na glavnoj ploci K1-K5 i shield bez pull otpornika i kondenzatora.
 *
 * Tasteri A su goli padovi (ESP32 touch) ili padovi sa TTP223 (digitalni ulaz) — vidi
 * ispod. Varijantu bira panel_variant.c: NVS "panel" = auto (default) / pads / ttp /
 * lcd; auto = TTP223 izlazi drze pinove LOW -> ttp, inace probe touch kontrolera
 * displeja na I2C (GPIO1/2) -> lcd, inace pads.
 */

/*
 * A: kapacitivni tasteri iza pleksija, ista plocica u dvije izvedbe (nikad mijesano):
 *   PANEL_PADS  pad -> R_A 0 R -> GPIO, ESP32 touch kanal N = GPIO N (+ shield GPIO14)
 *   PANEL_TTP   pad -> TTP223 TP; TTP223 Q -> R_B 1 k -> GPIO (digitalni ulaz)
 *               TTP223 na 3V3, TOG=0 AHLB=0 (direktno, aktivno HIGH), Cs 0-50 pF za
 *               osjetljivost kroz pleksi; R_A se ne lemi; shield se ne koristi.
 *               R_B stiti od sudara izlaza dok auto-probe displeja drzi GPIO5 kao izlaz.
 */
#define KEY_COUNT       5
#define KEY_GPIOS       { 1, 2, 4, 5, 6 }   /* 1 KM, 2 KM, 5 KM, PONISTI, PLACANJE */
#define KEY_TOUCH_CHANS KEY_GPIOS
#define KEY_TTP_ACTIVE_LEVEL 1
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
