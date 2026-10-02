/*
 * panel_variant.c — vidi panel_variant.h.
 */
#include "panel_variant.h"
#include "board.h"

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "panel";

static const struct { uint8_t addr; const char *name; } tp_chips[] = {
    { 0x38, "FT6336/FT5x06" },
    { 0x5D, "GT911" },
    { 0x14, "GT911" },
    { 0x15, "CST816" },
};

static void mode_get(char *buf, size_t len)
{
    nvs_handle_t h;

    strlcpy(buf, "auto", len);
    if (nvs_open("panel", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, "mode", buf, &len);
        nvs_close(h);
    }
}

static bool probe_touch_controller(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = -1,
        .sda_io_num = LCD_TP_SDA_GPIO,
        .scl_io_num = LCD_TP_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    i2c_master_bus_handle_t bus;
    bool found = false;

    /* touch kontroler odgovara tek kad mu RST nije aktivan (aktivno nizak) */
    gpio_reset_pin(LCD_TP_RST_GPIO);
    gpio_set_direction(LCD_TP_RST_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_TP_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_TP_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(60));                   /* GT911/FT6336 boot ~50 ms */

    if (i2c_new_master_bus(&cfg, &bus) == ESP_OK) {
        for (size_t i = 0; i < sizeof(tp_chips) / sizeof(tp_chips[0]) && !found; i++) {
            if (i2c_master_probe(bus, tp_chips[i].addr, 20) == ESP_OK) {
                ESP_LOGI(TAG, "touch kontroler %s na 0x%02X", tp_chips[i].name, tp_chips[i].addr);
                found = true;
            }
        }
        i2c_del_master_bus(bus);
    }

    /* vrati pinove u pocetno stanje — touch_keys ili LCD driver ih konfigurise iznova */
    gpio_reset_pin(LCD_TP_SDA_GPIO);
    gpio_reset_pin(LCD_TP_SCL_GPIO);
    if (!found) {
        gpio_reset_pin(LCD_TP_RST_GPIO);             /* PONISTI pad */
    }
    return found;
}

panel_variant_t panel_variant_detect(void)
{
    char mode[8];
    panel_variant_t v;

    mode_get(mode, sizeof(mode));
    if (!strcmp(mode, "pads")) {
        v = PANEL_PADS;
    } else if (!strcmp(mode, "lcd")) {
        v = PANEL_LCD;
    } else {
        v = probe_touch_controller() ? PANEL_LCD : PANEL_PADS;
    }
    ESP_LOGI(TAG, "front panel: %s (podesavanje: %s)", panel_variant_name(v), mode);
    return v;
}

const char *panel_variant_name(panel_variant_t v)
{
    return v == PANEL_LCD ? "displej sa touch ekranom" : "touch tasteri";
}

void panel_variant_console(const char *arg)
{
    nvs_handle_t h;
    char mode[8];

    while (*arg == ' ') {
        arg++;
    }
    if (!*arg) {
        mode_get(mode, sizeof(mode));
        printf("[panel] podesavanje: %s (auto / pads / lcd — vazi poslije restarta)\n", mode);
        return;
    }
    if (strcmp(arg, "auto") && strcmp(arg, "pads") && strcmp(arg, "lcd")) {
        printf("[panel] panel auto | pads | lcd\n");
        return;
    }
    if (nvs_open("panel", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "mode", arg);
        nvs_commit(h);
        nvs_close(h);
    }
    printf("[panel] podesavanje: %s — vazi poslije restarta\n", arg);
}
