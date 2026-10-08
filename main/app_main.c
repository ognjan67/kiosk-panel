/*
 * kiosk-panel — front panel naplatnog kioska CWS praonice (ESP32-S3).
 *
 *   UART0  link ka CWS ploci kioska (QR poklon, kredit kartice, displej, RDY)
 *   UART1  Payten POS terminal
 *   UART2  DFR0660 / GM65 QR citac
 *   touch  5 tastera iza pleksija (1/2/5 KM, PONISTI, PLACANJE) + LED — ESP32 touch
 *          ili TTP223 na istim pinovima (vidi board.h)
 *   USB    konzola (USB-Serial-JTAG)
 *
 * Sve osim konzole radi u jednoj petlji (5 ms) — moduli nemaju dijeljeno stanje
 * izmedju taskova. Konzola samo prosljedjuje linije u red.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "nvs_flash.h"

#include "board.h"
#include "cws_link.h"
#include "qr_reader.h"
#include "touch_keys.h"
#include "pos_payment.h"
#include "panel_variant.h"

static const char *TAG = "panel";

#define CON_LINE_MAX  960

static QueueHandle_t con_q;   /* char* linije iz konzole (malloc, oslobadja glavna petlja) */

static void on_gift(bool xfer, int code, int value)
{
    if (!xfer) {
        if (code == 0) {
            ESP_LOGI(TAG, "QR poklon ODOBREN: %d s — kupac bira boks na kiosku", value);
        } else {
            ESP_LOGW(TAG, "QR poklon odbijen: E%d", code);
        }
    } else if (code == 0) {
        ESP_LOGI(TAG, "QR poklon poslat na box %d", value);
    } else {
        ESP_LOGW(TAG, "QR poklon: izbor boksa nije uspio E%d", code);
    }
}

static void help(void)
{
    printf("komande:\n"
           "  1 2 5 c p      tasteri: 1/2/5 KM, PONISTI, PLACANJE\n"
           "  s t l r        Payten: stanje, TID, lista transakcija, oporavak\n"
           "  touch          vrijednosti touch tastera (kalibracija kroz pleksi)\n"
           "  thr <promil>   prag touch tastera (npr. thr 20 = 2 %%)\n"
           "  link           stanje linka ka CWS ploci\n"
           "  panel [auto|pads|ttp|lcd]  varijanta front panela (vazi poslije restarta)\n"
           "  https://...    zalijepljen link racuna = kao da je QR citac procitao\n");
}

static void on_console_line(char *s)
{
    if (!strncmp(s, "http", 4)) {
        qr_reader_inject(s);
    } else if (!strcmp(s, "touch")) {
        touch_keys_dump();
    } else if (!strncmp(s, "thr ", 4)) {
        touch_keys_set_threshold((uint32_t)atoi(s + 4));
    } else if (!strncmp(s, "panel", 5) && (s[5] == '\0' || s[5] == ' ')) {
        panel_variant_console(s + 5);
    } else if (!strcmp(s, "link")) {
        cws_link_status();
    } else if (strlen(s) == 1) {
        switch (s[0]) {
        case '1': pos_payment_key(KEY_1KM); break;
        case '2': pos_payment_key(KEY_2KM); break;
        case '5': pos_payment_key(KEY_5KM); break;
        case 'c': pos_payment_key(KEY_CANCEL); break;
        case 'p': pos_payment_key(KEY_PAY); break;
        case 's': pos_payment_console('s'); cws_link_status(); break;
        case 't': case 'l': case 'r': pos_payment_console(s[0]); break;
        default: help(); break;
        }
    } else if (s[0]) {
        help();
    }
}

static void console_task(void *arg)
{
    static char line[CON_LINE_MAX + 1];
    int len = 0;
    bool overflow = false;
    uint8_t c;

    for (;;) {
        if (usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (!overflow && len) {
                char *copy;
                line[len] = '\0';
                copy = strdup(line);
                if (copy && xQueueSend(con_q, &copy, 0) != pdTRUE) {
                    free(copy);
                }
            }
            len = 0;
            overflow = false;
        } else if (len < CON_LINE_MAX) {
            line[len++] = (char)c;
        } else {
            overflow = true;
        }
    }
}

void app_main(void)
{
    usb_serial_jtag_driver_config_t usj = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    usj.rx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usj));
    usb_serial_jtag_vfs_use_driver();

    ESP_LOGI(TAG, "kiosk front panel start");
    cws_link_init(on_gift);
    qr_reader_init();
    panel_variant_t panel = panel_variant_detect();
    if (panel == PANEL_PADS || panel == PANEL_TTP) {
        touch_keys_init(panel == PANEL_TTP);
    } else {
        /* TODO: SPI displej (esp_lcd ST7796) + touch + LVGL UI sa istih 5 dugmadi */
        ESP_LOGW(TAG, "displej varijanta: UI jos nije implementiran — tasteri samo preko konzole");
    }
    pos_payment_init();

    con_q = xQueueCreate(4, sizeof(char *));
    xTaskCreate(console_task, "console", 4096, NULL, 3, NULL);
    help();

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    for (;;) {
        key_id_t key;
        char *line;

        cws_link_poll();
        qr_reader_poll();
        pos_payment_poll();
        while (touch_keys_get(&key)) {
            ESP_LOGI(TAG, "taster %d", (int)key);
            pos_payment_key(key);
        }
        while (xQueueReceive(con_q, &line, 0) == pdTRUE) {
            on_console_line(line);
            free(line);
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
