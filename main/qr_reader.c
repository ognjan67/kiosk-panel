/*
 * qr_reader.c — vidi qr_reader.h.
 */
#include "qr_reader.h"
#include "board.h"
#include "cws_link.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "qr";

#define QR_MAX          900       /* link racuna ~830 znakova */
#define IDLE_END_MS     60
#define REPEAT_MS       5000

static char     buf[QR_MAX + 1];
static int      len;
static bool     overflow;
static int64_t  last_rx_ms;
static char     last_sent[QR_MAX + 4];   /* "QR," + kod */
static int64_t  last_sent_ms = -100000;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void on_code(const char *text)
{
    static char pl[QR_MAX + 4];
    const char *vl = strstr(text, "vl=");

    if (strncmp(text, "http", 4) != 0 || !vl) {
        ESP_LOGW(TAG, "nije link fiskalnog racuna (%u znakova): %.40s", (unsigned)strlen(text), text);
        /* ipak proslijedi — CWS prikaze "E 2" da kupac zna da je kod procitan */
        snprintf(pl, sizeof(pl), "QR,%s", text);
    } else {
        /* samo vl (bez "https://suf.../v/?vl=") — kraci prenos, CWS prihvata oba oblika */
        snprintf(pl, sizeof(pl), "QR,%s", vl + 3);
    }
    if (!strcmp(pl, last_sent) && now_ms() - last_sent_ms < REPEAT_MS) {
        return;
    }
    strcpy(last_sent, pl);
    last_sent_ms = now_ms();
    ESP_LOGI(TAG, "procitan QR (%u znakova) -> CWS", (unsigned)strlen(text));
    cws_link_send(pl);
}

static void end_line(void)
{
    if (overflow) {
        ESP_LOGW(TAG, "QR predug — odbacen");
    } else if (len) {
        buf[len] = '\0';
        on_code(buf);
    }
    len = 0;
    overflow = false;
}

void qr_reader_inject(const char *text)
{
    on_code(text);
}

void qr_reader_poll(void)
{
    uint8_t rx[128];
    int n;

    while ((n = uart_read_bytes(QR_UART, rx, sizeof(rx), 0)) > 0) {
        last_rx_ms = now_ms();
        for (int i = 0; i < n; i++) {
            char c = (char)rx[i];
            if (c == '\r' || c == '\n') {
                end_line();
            } else if (len < QR_MAX) {
                buf[len++] = c;
            } else {
                overflow = true;
            }
        }
    }
    if ((len || overflow) && now_ms() - last_rx_ms > IDLE_END_MS) {
        end_line();
    }
}

void qr_reader_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = QR_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(QR_UART, 2048, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(QR_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(QR_UART, QR_TX_GPIO, QR_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG, "QR citac UART%d TX=%d RX=%d %d 8N1", QR_UART, QR_TX_GPIO, QR_RX_GPIO, QR_BAUD);
}
