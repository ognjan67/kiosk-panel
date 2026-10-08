/*
 * panel_variant — koja je front panel pločica priključena (vidi tabelu u board.h).
 *
 * NVS "panel/mode": "auto" (default), "pads", "ttp", "lcd". U auto rezimu:
 *  1. pasivno: pinovi tastera kao ulaz sa pull-up — ako ih je bar 3 od 5 LOW, na njima
 *     su izlazi TTP223 (u mirovanju i tokom kalibracije aktivno drze LOW) -> TTP223;
 *     goli padovi i LCD plocica (I2C pull-up, INT open-drain) citaju HIGH;
 *  2. pokusa se procitati touch kontroler displeja na I2C (SDA GPIO1, SCL GPIO2, touch
 *     RST GPIO5 se drzi u 1): FT6336/FT5x06 0x38, GT911 0x5D/0x14, CST816 0x15.
 *     Odgovor = displej, inace ESP32 touch tasteri. Na plocici sa padovima probe ne
 *     smeta — padovi su samo bakar, a pinovi se poslije probe vracaju u pocetno stanje.
 */
#pragma once

typedef enum { PANEL_PADS = 0, PANEL_TTP, PANEL_LCD } panel_variant_t;

panel_variant_t panel_variant_detect(void);
const char     *panel_variant_name(panel_variant_t v);
void            panel_variant_console(const char *arg);   /* "panel [auto|pads|ttp|lcd]" */
