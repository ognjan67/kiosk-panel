/*
 * pos_payment.c — vidi pos_payment.h.
 */
#include "pos_payment.h"
#include "board.h"
#include "cws_link.h"
#include "payten_ecr.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "pos";

#define POS_MAX_CENTS   1000u     /* max 10 KM po jednom placanju */
#define AMT_IDLE_MS     60000     /* unesen iznos bez placanja se brise poslije 60 s */
#define FAIL_SHOW_MS    4000

static uint32_t amount_cents;     /* iznos koji kupac sabira tasterima */
static int64_t  amount_ms;
static bool     seq_resync;       /* NVS prazan: seq = max iz liste terminala + 1 */
static uint16_t seq_max;
static int64_t  fail_ms = -100000;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* ---------------- displej kioska (CWS) ---------------- */

static void show_amount(int st)
{
    char pl[24];
    snprintf(pl, sizeof(pl), "AMT,%lu,%d", (unsigned long)(amount_cents / 10u), st);
    cws_link_send(pl);
}

static void show_fail(void)
{
    fail_ms = now_ms();
    show_amount(2);
}

/* ---------------- payten HAL ---------------- */

static void hal_write(const uint8_t *buf, uint16_t len)
{
    uart_write_bytes(POS_UART, buf, len);
}

static uint32_t hal_millis(void)
{
    return (uint32_t)now_ms();
}

static void hal_save(const pecr_persist_t *p)
{
    nvs_handle_t h;
    if (nvs_open("pos", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "pecr", p, sizeof(*p));
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool persist_load(pecr_persist_t *p)
{
    nvs_handle_t h;
    size_t len = sizeof(*p);
    bool ok = false;

    if (nvs_open("pos", NVS_READONLY, &h) == ESP_OK) {
        ok = nvs_get_blob(h, "pecr", p, &len) == ESP_OK && len == sizeof(*p)
             && p->seq >= 1 && p->seq <= 9999;
        nvs_close(h);
    }
    return ok;
}

static void hal_log(const char *text)
{
    ESP_LOGI(TAG, "%s", text);
}

static void on_done(const pecr_event_t *ev)
{
    const pecr_msg_t *m = ev->msg;
    char pl[64];

    switch (ev->result) {
    case PECR_RES_APPROVED:
        if (ev->op == PECR_OP_SALE) {
            ESP_LOGI(TAG, "ODOBRENO %lu.%02lu KM tx=%06lu auth=%s %s %s",
                     (unsigned long)(m->amount_cents / 100u), (unsigned long)(m->amount_cents % 100u),
                     (unsigned long)m->tx_no, m->auth_code, m->company, m->pan);
            /* iznos koji je TERMINAL odobrio (ne lokalni amount_cents) ide u kredit */
            snprintf(pl, sizeof(pl), "POS,%lu,%06lu,%s,%s", (unsigned long)(m->amount_cents / 10u),
                     (unsigned long)m->tx_no, m->auth_code, m->company);
            cws_link_send_reliable("POS", m->tx_no, pl);
            amount_cents = 0;
            show_amount(0);
        } else {
            ESP_LOGI(TAG, "STORNO ODOBREN tx=%06lu", (unsigned long)m->tx_no);
        }
        break;
    case PECR_RES_DECLINED:
        ESP_LOGW(TAG, "NIJE ODOBRENO: %s (flag %s)", m->display, m->flag);
        show_fail();
        break;
    case PECR_RES_CANCELLED:
        ESP_LOGI(TAG, "PLACANJE OTKAZANO");
        show_amount(0);
        break;
    case PECR_RES_NO_ACK:
        ESP_LOGE(TAG, "terminal ne odgovara");
        cws_link_send("POSE,NOACK");
        show_fail();
        break;
    case PECR_RES_TIMEOUT:
        ESP_LOGE(TAG, "nema odgovora terminala%s",
                 pecr_has_pending() ? " — sljedece placanje prvo radi oporavak" : "");
        show_fail();
        break;
    case PECR_RES_TERMINAL_ERROR:
        ESP_LOGE(TAG, "GRESKA TERMINALA %s: %s", m ? m->code : "", m ? m->display : "");
        snprintf(pl, sizeof(pl), "POSE,%s", m ? m->code : "?");
        cws_link_send(pl);
        show_fail();
        break;
    case PECR_RES_INFO:
        if (ev->op == PECR_OP_TID) {
            ESP_LOGI(TAG, "TERMINAL ID: %s", m->tid);
        } else if (seq_resync) {
            seq_resync = false;
            pecr_set_seq((uint16_t)(seq_max % 9999u + 1u));
            ESP_LOGI(TAG, "seq obnovljen iz liste terminala: %u", pecr_persist()->seq);
        } else {
            ESP_LOGI(TAG, "lista zavrsena");
        }
        break;
    case PECR_RES_RECOVER_NOT_FOUND:
        ESP_LOGI(TAG, "OPORAVAK: nedovrsena transakcija nije naplacena");
        break;
    case PECR_RES_RECOVER_VOIDED:
    case PECR_RES_RECOVER_VOID_FAILED: {
        bool ok = (ev->result == PECR_RES_RECOVER_VOIDED);
        if (ok) {
            ESP_LOGW(TAG, "OPORAVAK: tx=%06lu bila odobrena — AUTOMATSKI STORNIRANA", (unsigned long)m->tx_no);
        } else {
            ESP_LOGE(TAG, "OPORAVAK: tx=%06lu odobrena, storno NIJE uspio — rucna intervencija!",
                     (unsigned long)m->tx_no);
        }
        snprintf(pl, sizeof(pl), "POSV,%lu,%06lu,%d", (unsigned long)(m->amount_cents / 10u),
                 (unsigned long)m->tx_no, ok ? 1 : 0);
        cws_link_send_reliable("POSV", m->tx_no, pl);
        break;
    }
    default:
        break;
    }
}

static void hal_event(const pecr_event_t *ev)
{
    const pecr_msg_t *m = ev->msg;

    switch (ev->kind) {
    case PECR_EV_HOLD:
        ESP_LOGI(TAG, "%s (%s)", m->display, m->code);
        break;
    case PECR_EV_ERROR:
        ESP_LOGW(TAG, "terminal: %s (%s)", m->display, m->code);
        break;
    case PECR_EV_LIST_ITEM:
        if (strcmp(m->tx_type, "34") != 0) {          /* transakcija (ne zbir izdavaoca) */
            if (m->seq > seq_max) {
                seq_max = m->seq;
            }
            if (!seq_resync) {
                printf("[pos]  tx=%06lu seq=%04u %lu.%02lu KM %s auth=%s\n", (unsigned long)m->tx_no, m->seq,
                       (unsigned long)(m->amount_cents / 100u), (unsigned long)(m->amount_cents % 100u),
                       m->company, m->auth_code);
            }
        } else if (!seq_resync && !strcmp(m->company, "TOTAL")) {
            printf("[pos]  UKUPNO: %u transakcija, %lu.%02lu KM\n", m->debit_count,
                   (unsigned long)(m->debit_cents / 100u), (unsigned long)(m->debit_cents % 100u));
        }
        break;
    case PECR_EV_DONE:
        on_done(ev);
        break;
    }
}

static const pecr_hal_t pos_hal = {
    hal_write, hal_millis, hal_event, hal_save, hal_log,
};

/* ---------------- tasteri ---------------- */

static void start_payment(void)
{
    pecr_status_t s;

    if (!cws_link_ready()) {
        ESP_LOGW(TAG, "placanje nije moguce: kiosk %s", cws_link_up() ? "nije spreman" : "ne odgovara");
        show_fail();
        return;
    }
    if (pecr_busy()) {
        return;
    }
    if (pecr_has_pending()) {
        ESP_LOGW(TAG, "nedovrsena transakcija — prvo oporavak, pa ponovo PLACANJE");
        pecr_recover();
        return;
    }
    if (amount_cents == 0) {
        return;
    }
    s = pecr_start_sale(amount_cents);
    if (s == PECR_OK) {
        ESP_LOGI(TAG, "PRINESITE KARTICU TERMINALU (%lu.%02lu KM)",
                 (unsigned long)(amount_cents / 100u), (unsigned long)(amount_cents % 100u));
        show_amount(1);
    } else {
        ESP_LOGE(TAG, "placanje nije pokrenuto (status %d)", (int)s);
        show_fail();
    }
}

void pos_payment_key(key_id_t key)
{
    switch (key) {
    case KEY_1KM:
    case KEY_2KM:
    case KEY_5KM: {
        static const uint32_t add_tab[] = { 100u, 200u, 500u };
        uint32_t add = add_tab[key];
        if (pecr_busy()) {
            break;
        }
        if (!cws_link_ready()) {
            show_fail();
            break;
        }
        if (amount_cents + add > POS_MAX_CENTS) {
            ESP_LOGW(TAG, "maksimalni iznos je %u KM", POS_MAX_CENTS / 100u);
            break;
        }
        amount_cents += add;
        amount_ms = now_ms();
        ESP_LOGI(TAG, "IZNOS: %lu.%02lu KM", (unsigned long)(amount_cents / 100u),
                 (unsigned long)(amount_cents % 100u));
        show_amount(0);
        break;
    }
    case KEY_CANCEL:
        if (!pecr_busy()) {
            amount_cents = 0;
            ESP_LOGI(TAG, "PONISTENO");
            show_amount(0);
        } else if (pecr_cancel() == PECR_OK) {
            ESP_LOGI(TAG, "otkazujem na terminalu...");
        } else {
            ESP_LOGW(TAG, "kartica je vec procitana — sacekajte zavrsetak");
        }
        break;
    case KEY_PAY:
        start_payment();
        break;
    }
}

void pos_payment_console(char c)
{
    const pecr_persist_t *p;

    switch (c) {
    case 's':
        p = pecr_persist();
        printf("[pos] iznos=%lu c seq=%u pending=%u/%lu c busy=%d\n", (unsigned long)amount_cents,
               p->seq, p->pending_seq, (unsigned long)p->pending_cents, pecr_busy());
        break;
    case 't':
        if (pecr_start_tid() != PECR_OK) printf("[pos] zauzeto\n");
        break;
    case 'l':
        if (pecr_start_list() != PECR_OK) printf("[pos] zauzeto\n");
        break;
    case 'r':
        if (pecr_recover() != PECR_OK) printf("[pos] nema nedovrsene transakcije ili zauzeto\n");
        break;
    default:
        break;
    }
}

/* ---------------- LED ispod tastera ---------------- */

static void update_leds(void)
{
    bool ready = cws_link_ready();
    bool busy = pecr_busy();
    bool blink = (now_ms() / 300) & 1;
    bool fail = now_ms() - fail_ms < FAIL_SHOW_MS;

    key_led_set(KEY_1KM, ready && !busy);
    key_led_set(KEY_2KM, ready && !busy);
    key_led_set(KEY_5KM, ready && !busy);
    key_led_set(KEY_CANCEL, ready && (amount_cents || busy));
    /* PLACANJE trepce kad je iznos unesen (pritisni), stalno dok terminal radi, brzo na gresku */
    key_led_set(KEY_PAY, fail ? ((now_ms() / 100) & 1)
                              : busy ? true : (ready && amount_cents && blink));
}

void pos_payment_poll(void)
{
    uint8_t rx[64];
    int n;

    while ((n = uart_read_bytes(POS_UART, rx, sizeof(rx), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            pecr_rx_byte(rx[i]);
        }
    }
    pecr_poll();

    if (amount_cents && !pecr_busy() && now_ms() - amount_ms > AMT_IDLE_MS) {
        ESP_LOGI(TAG, "iznos istekao (nije placeno) — ponisteno");
        amount_cents = 0;
        show_amount(0);
    }
    update_leds();
}

void pos_payment_init(void)
{
    const uart_config_t cfg = {
        .baud_rate = POS_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    pecr_persist_t p;
    bool ok;

    ESP_ERROR_CHECK(uart_driver_install(POS_UART, 1024, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(POS_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(POS_UART, POS_TX_GPIO, POS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    ok = persist_load(&p);
    pecr_init(&pos_hal, ok ? &p : NULL);
    ESP_LOGI(TAG, "Payten terminal UART%d TX=%d RX=%d %d 8N1, seq=%u%s", POS_UART, POS_TX_GPIO, POS_RX_GPIO,
             POS_BAUD, pecr_persist()->seq, ok ? "" : " (NVS prazan)");

    if (!ok) {
        seq_resync = true;
        seq_max = 0;
        pecr_start_list();
    } else if (pecr_has_pending()) {
        ESP_LOGW(TAG, "nedovrsena transakcija seq=%u %lu c — oporavak", p.pending_seq,
                 (unsigned long)p.pending_cents);
        pecr_recover();
    }
}
