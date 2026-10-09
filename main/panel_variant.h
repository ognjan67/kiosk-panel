/*
 * panel_variant — koja je front panel pločica priključena (vidi tabelu u board.h).
 *
 * NVS "panel/mode": "auto" (default), "ttp", "lcd", "pads". U auto rezimu se pri startu
 * pokusa procitati touch kontroler displeja na I2C (SDA GPIO1, SCL GPIO2, touch RST GPIO5
 * open-drain): FT6336/FT5x06 0x38, GT911 0x5D/0x14, CST816 0x15. Odgovor = displej,
 * inace plocica Touch_5T sa TTP223 tasterima (open-collector linije sa pull-up-om ne
 * odgovaraju na I2C). "pads" (ESP32 touch na golim padovima) samo eksplicitno.
 */
#pragma once

typedef enum { PANEL_PADS = 0, PANEL_TTP, PANEL_LCD } panel_variant_t;

panel_variant_t panel_variant_detect(void);
const char     *panel_variant_name(panel_variant_t v);
void            panel_variant_console(const char *arg);   /* "panel [auto|pads|ttp|lcd]" */
