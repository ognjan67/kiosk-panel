/*
 * cws_link.c — vidi cws_link.h.
 */
#include "cws_link.h"
#include "board.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "link";

#define LINK_LINE_MAX        128
#define RDY_TIMEOUT_MS  3000
#define RESEND_MS       1000
#define RQ_LEN          4
#define RQ_PAYLOAD      64

typedef struct {
    char     type[6];
    uint32_t tx_no;
    char     payload[RQ_PAYLOAD];
} rq_item_t;

static cws_link_gift_cb_t gift_cb;
static char     line[LINK_LINE_MAX + 1];
static int      line_len;
static bool     line_overflow;
static bool     rdy;
static int64_t  rdy_ms = -100000;
static rq_item_t rq[RQ_LEN];       /* type[0] == 0 -> slobodno */
static int64_t  rq_sent_ms;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint8_t xor_sum(const char *s, size_t len)
{
    uint8_t x = 0;
    while (len--) {
        x ^= (uint8_t)*s++;
    }
    return x;
}

void cws_link_send(const char *payload)
{
    char tail[6];
    size_t len = strlen(payload);

    snprintf(tail, sizeof(tail), "*%02X\n", xor_sum(payload, len));
    uart_write_bytes(LINK_UART, payload, len);
    uart_write_bytes(LINK_UART, tail, strlen(tail));
}

/* ---------------- pouzdane poruke (NVS) ---------------- */

static void rq_save(void)
{
    nvs_handle_t h;
    if (nvs_open("link", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "rq", rq, sizeof(rq));
        nvs_commit(h);
        nvs_close(h);
    }
}

static void rq_load(void)
{
    nvs_handle_t h;
    size_t len = sizeof(rq);

    memset(rq, 0, sizeof(rq));
    if (nvs_open("link", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_blob(h, "rq", rq, &len) != ESP_OK || len != sizeof(rq)) {
            memset(rq, 0, sizeof(rq));
        }
        nvs_close(h);
    }
    for (int i = 0; i < RQ_LEN; i++) {
        if (rq[i].type[0]) {
            ESP_LOGW(TAG, "nepotvrdjena poruka iz NVS-a: %s", rq[i].payload);
        }
    }
}

bool cws_link_send_reliable(const char *type, uint32_t tx_no, const char *payload)
{
    for (int i = 0; i < RQ_LEN; i++) {
        if (!rq[i].type[0]) {
            strlcpy(rq[i].type, type, sizeof(rq[i].type));
            rq[i].tx_no = tx_no;
            strlcpy(rq[i].payload, payload, sizeof(rq[i].payload));
            rq_save();                         /* PRIJE slanja — preziv restart */
            cws_link_send(payload);
            return true;
        }
    }
    ESP_LOGE(TAG, "red pouzdanih poruka pun — %s", payload);
    return false;
}

static void rq_ack(const char *type, uint32_t tx_no)
{
    for (int i = 0; i < RQ_LEN; i++) {
        if (rq[i].type[0] && !strcmp(rq[i].type, type) && rq[i].tx_no == tx_no) {
            ESP_LOGI(TAG, "CWS potvrdio %s tx=%06lu", type, (unsigned long)tx_no);
            memset(&rq[i], 0, sizeof(rq[i]));
            rq_save();
            return;
        }
    }
}

static void rq_resend(void)
{
    if (now_ms() - rq_sent_ms < RESEND_MS) {
        return;
    }
    rq_sent_ms = now_ms();
    for (int i = 0; i < RQ_LEN; i++) {
        if (rq[i].type[0]) {
            cws_link_send(rq[i].payload);
        }
    }
}

/* ---------------- prijem ---------------- */

static void on_payload(char *p)
{
    char type[6];
    unsigned long tx;
    int a, b;

    if (!strncmp(p, "RDY,", 4)) {
        rdy = (p[4] == '1');
        rdy_ms = now_ms();
    } else if (sscanf(p, "ACK,%5[^,],%lu", type, &tx) == 2) {
        rq_ack(type, (uint32_t)tx);
    } else if (sscanf(p, "GIFTX,%d,%d", &a, &b) == 2) {
        if (gift_cb) gift_cb(true, a, b);
    } else if (sscanf(p, "GIFT,%d,%d", &a, &b) == 2) {
        if (gift_cb) gift_cb(false, a, b);
    } else {
        ESP_LOGW(TAG, "nepoznata poruka: %.40s", p);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void on_line(void)
{
    char *star;

    line[line_len] = '\0';
    star = strrchr(line, '*');
    if (!star || star + 3 != line + line_len || hexval(star[1]) < 0 || hexval(star[2]) < 0) {
        /* CWS pri startu ispise par linija debug-a na USART3 prije prelaska na link */
        ESP_LOGD(TAG, "linija bez okvira: %.40s", line);
        return;
    }
    *star = '\0';
    if (xor_sum(line, (size_t)(star - line)) != (uint8_t)(hexval(star[1]) * 16 + hexval(star[2]))) {
        ESP_LOGW(TAG, "los checksum: %.40s", line);
        return;
    }
    on_payload(line);
}

void cws_link_poll(void)
{
    uint8_t buf[64];
    int n;

    while ((n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = (char)buf[i];
            if (c == '\n' || c == '\r') {
                if (!line_overflow && line_len) {
                    on_line();
                }
                line_len = 0;
                line_overflow = false;
            } else if (line_len < LINK_LINE_MAX) {
                line[line_len++] = c;
            } else {
                line_overflow = true;
            }
        }
    }
    rq_resend();
}

bool cws_link_up(void)
{
    return now_ms() - rdy_ms < RDY_TIMEOUT_MS;
}

bool cws_link_ready(void)
{
    return rdy && cws_link_up();
}

void cws_link_status(void)
{
    int pending = 0;
    for (int i = 0; i < RQ_LEN; i++) {
        if (rq[i].type[0]) {
            pending++;
        }
    }
    printf("[link] CWS %s, kiosk %s, nepotvrdjenih poruka: %d\n",
           cws_link_up() ? "povezan" : "NE ODGOVARA", rdy ? "spreman" : "nije spreman", pending);
}

void cws_link_init(cws_link_gift_cb_t on_gift)
{
    const uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    gift_cb = on_gift;
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, 1024, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TX_GPIO, LINK_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    rq_load();
    ESP_LOGI(TAG, "CWS link UART%d TX=%d RX=%d %d 8N1", LINK_UART, LINK_TX_GPIO, LINK_RX_GPIO, LINK_BAUD);
}
