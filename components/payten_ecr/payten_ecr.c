/*
 * payten_ecr.c — Payten ECR protokol, ECR strana. Vidi payten_ecr.h.
 */
#include "payten_ecr.h"

#include <string.h>
#include <stdio.h>

#define STX 0x02u
#define ETX 0x03u
#define EOT 0x04u
#define ACK 0x06u
#define NAK 0x15u
#define FS  '\x1c'

#define CURRENCY_BAM "977"
#define HOLD_CARD_READ "54"

/* ======================================================================
 * Cisti protokol
 * ====================================================================== */

uint8_t pecr_lrc(const uint8_t *data, uint16_t len)
{
    uint8_t x = 0;
    while (len--) {
        x ^= *data++;
    }
    return x;
}

uint16_t pecr_frame(const char *data, uint8_t *out, uint16_t out_size)
{
    uint16_t n = (uint16_t)strlen(data);
    if ((uint32_t)n + 4u > out_size) {
        return 0;
    }
    out[0] = STX;
    out[1] = STX;
    memcpy(&out[2], data, n);
    out[2 + n] = ETX;
    out[3 + n] = pecr_lrc(&out[2], (uint16_t)(n + 1));   /* <DATA><ETX> */
    return (uint16_t)(n + 4);
}

uint16_t pecr_build_request(char *out, uint16_t out_size, const char *tx_type,
                            uint16_t seq, uint32_t cents, uint32_t tx_no)
{
    char txno[8] = "";
    int n;
    if (tx_no) {
        snprintf(txno, sizeof(txno), "%06lu", (unsigned long)tx_no);
    }
    /* header: id, terminal ID, source ID, seq, tip, printer flag 0 (bez stampe), cashier 00 */
    n = snprintf(out, out_size, "000000%04u%s000%s%c", (unsigned)seq, tx_type, txno, FS);
    if (n < 0 || n >= out_size) {
        return 0;
    }
    if (cents) {
        /* amount FS | FS | +0 FS | 977 FS | 24 prazna slota (polja 17..61) — ukupno 29 FS */
        int m = snprintf(out + n, (size_t)(out_size - n), "%lu%c%c+0%c%s%c",
                         (unsigned long)cents, FS, FS, FS, CURRENCY_BAM, FS);
        if (m < 0 || n + m + 24 >= out_size) {
            return 0;
        }
        n += m;
        memset(out + n, FS, 24);
        n += 24;
        out[n] = '\0';
    }
    /* bez iznosa: samo header + FS (terminal odbija prazna polja iznosa/valute) */
    return (uint16_t)n;
}

static uint32_t num(const char *s, uint16_t len)
{
    uint32_t v = 0;
    while (len-- && *s >= '0' && *s <= '9') {
        v = v * 10u + (uint32_t)(*s++ - '0');
    }
    return v;
}

/* kopira [s, s+len) u dst (C string), odsijeca razmake na kraju */
static void copy_str(char *dst, uint16_t dst_size, const char *s, uint16_t len)
{
    if (len >= dst_size) {
        len = (uint16_t)(dst_size - 1);
    }
    memcpy(dst, s, len);
    while (len && dst[len - 1] == ' ') {
        len--;
    }
    dst[len] = '\0';
}

#define COPY(dst, s, len) copy_str((dst), (uint16_t)sizeof(dst), (s), (len))

static void parse_totals(pecr_msg_t *m, const char *s, uint16_t len)
{
    if (len == 42) {         /* tip 32: prefiks naziv acquirer-a (10) */
        s += 10;
        len = 32;
    }
    if (len == 32) {         /* broj zaduzenja 4 + iznos 12 + broj povrata 4 + iznos 12 */
        m->debit_count = (uint16_t)num(s, 4);
        m->debit_cents = num(s + 4, 12);
    }
}

bool pecr_parse(const char *d, uint16_t len, pecr_msg_t *m)
{
    memset(m, 0, sizeof(*m));
    if (len < 2) {
        return false;
    }
    m->id[0] = d[0];
    m->id[1] = d[1];

    if (!memcmp(d, "10", 2)) {
        uint16_t pos, idx = 0;
        if (len < 36) {
            return false;
        }
        m->seq   = (uint16_t)num(d + 6, 4);
        COPY(m->tx_type, d + 10, 2);
        COPY(m->flag, d + 12, 2);
        m->tx_no = num(d + 14, 6);
        m->batch = (uint16_t)num(d + 20, 4);
        COPY(m->date, d + 24, 6);
        COPY(m->time, d + 30, 6);
        pos = 36;
        if (pos < len && d[pos] == FS) {
            pos++;
        }
        /* FS-odvojena polja poslije fiksnog prefiksa (indeksi kao u Python parseru) */
        while (pos <= len) {
            uint16_t end = pos;
            while (end < len && d[end] != FS) {
                end++;
            }
            {
                const char *f = d + pos;
                uint16_t fl = (uint16_t)(end - pos);
                switch (idx) {
                case 0:  m->amount_cents = num(f, fl); break;
                case 4:  m->card_source = fl ? f[0] : 0; break;
                case 5:  COPY(m->pan, f, fl); break;
                case 9:  COPY(m->auth_code, f, fl); break;
                case 10: COPY(m->tid, f, fl); break;
                case 12: COPY(m->company, f, fl); break;
                case 19: COPY(m->display, f, fl); break;
                case 24: parse_totals(m, f, fl); break;
                case 25: m->more_messages = (fl && f[0] == '1'); break;
                case 27: COPY(m->full_resp_code, f, fl); break;
                case 33: COPY(m->rrn, f, fl); break;
                default: break;
                }
            }
            if (end >= len) {
                break;
            }
            pos = (uint16_t)(end + 1);
            idx++;
        }
        return true;
    }

    if (!memcmp(d, "25", 2) || !memcmp(d, "26", 2)) {      /* display FS code FS */
        uint16_t p = 8, e = 8;
        while (e < len && d[e] != FS) e++;
        if (p < len) COPY(m->display, d + p, (uint16_t)(e - p));
        p = (uint16_t)(e + 1);
        e = p;
        while (e < len && d[e] != FS) e++;
        if (p < len) COPY(m->code, d + p, (uint16_t)(e - p));
        return true;
    }

    if (!memcmp(d, "20", 2) || !memcmp(d, "22", 2) || !memcmp(d, "24", 2)) {
        uint16_t e = 8;
        while (e < len && d[e] != FS) e++;
        if (len > 8) COPY(m->display, d + 8, (uint16_t)(e - 8));
        return true;
    }
    return true;     /* ostale poruke (30, 35, 32...) — samo id */
}

/* ======================================================================
 * Link sloj + operacije
 * ====================================================================== */

static const pecr_hal_t *H;
static pecr_persist_t P;

/* deframer */
static enum { DF_IDLE, DF_DATA, DF_LRC } df;
static char     rx_buf[PECR_RX_MAX + 1];
static uint16_t rx_len;
static uint8_t  rx_lrc;

/* okvir koji ceka ACK (jedan u isto vrijeme) */
static uint8_t  tx_buf[PECR_TX_MAX];
static uint16_t tx_len;
static uint8_t  tx_tries;
static uint32_t tx_t0;
static bool     tx_wait_ack;
static bool     tx_main;           /* true: glavni zahtjev operacije; false: pomocni (23, 36) */
static bool     aux_cancel_queued;

/* operacija */
static enum { ST_IDLE, ST_WAIT_ACK, ST_WAIT_MSG } st;
static pecr_op_t op;
static bool      recover_void_phase;
static uint16_t  op_seq;
static uint32_t  msg_t0, msg_timeout;
static bool      card_read, got_error;
static pecr_msg_t msg;
static uint32_t  rec_tx_no;        /* recover: nadjena transakcija */

static void logf_(const char *fmt, const char *a, const char *b)
{
    if (H && H->log) {
        char line[96];
        snprintf(line, sizeof(line), fmt, a, b);
        H->log(line);
    }
}

static void emit(pecr_event_kind_t kind, pecr_result_t res, const pecr_msg_t *m)
{
    pecr_event_t ev;
    ev.kind = kind;
    ev.op = op;
    ev.result = res;
    ev.msg = m;
    if (H && H->event) {
        H->event(&ev);
    }
}

static void save(void)
{
    if (H && H->save) {
        H->save(&P);
    }
}

static void next_seq(void)
{
    P.seq = (uint16_t)(P.seq % 9999u + 1u);
    save();
}

static void clear_pending(void)
{
    P.pending_seq = 0;
    P.pending_cents = 0;
    save();
}

static void finish(pecr_result_t res, const pecr_msg_t *m)
{
    st = ST_IDLE;
    tx_wait_ack = false;
    aux_cancel_queued = false;
    emit(PECR_EV_DONE, res, m);
    op = PECR_OP_NONE;
    recover_void_phase = false;
}

static void tx_send(const char *data, bool main_request)
{
    tx_len = pecr_frame(data, tx_buf, sizeof(tx_buf));
    tx_tries = 1;
    tx_main = main_request;
    tx_wait_ack = true;
    tx_t0 = H->millis();
    H->write(tx_buf, tx_len);
}

static void send_aux(const char *data)
{
    tx_send(data, false);
}

static void start_request(const char *tx_type, uint16_t seq, uint32_t cents, uint32_t tx_no)
{
    char data[PECR_TX_MAX];
    pecr_build_request(data, sizeof(data), tx_type, seq, cents, tx_no);
    card_read = false;
    got_error = false;
    st = ST_WAIT_ACK;
    tx_send(data, true);
}

static void on_ack(bool ok)
{
    if (!tx_wait_ack) {
        return;
    }
    if (!ok) {                      /* NAK -> retransmit odmah */
        tx_t0 = H->millis() - PECR_ACK_TIMEOUT_MS;
        return;
    }
    tx_wait_ack = false;
    if (tx_main && st == ST_WAIT_ACK) {
        st = ST_WAIT_MSG;
        msg_t0 = H->millis();
        msg_timeout = PECR_FIRST_MSG_TIMEOUT_MS;
    }
    if (aux_cancel_queued && st == ST_WAIT_MSG) {
        aux_cancel_queued = false;
        send_aux("23000000");
    }
}

static bool approved(const pecr_msg_t *m)
{
    return !strcmp(m->flag, "01") || !strcmp(m->flag, "02");
}

static void on_response10(const pecr_msg_t *m)
{
    switch (op) {
    case PECR_OP_SALE:
        if (m->seq != op_seq && m->seq != 0) {
            logf_("odgovor za drugi seq, ignorisem%s%s", "", "");
            return;
        }
        clear_pending();
        next_seq();
        finish(approved(m) ? PECR_RES_APPROVED : PECR_RES_DECLINED, m);
        return;

    case PECR_OP_VOID:                 /* odgovor nosi seq ORIGINALNE prodaje — ne provjerava se */
        next_seq();
        finish(approved(m) ? PECR_RES_APPROVED : PECR_RES_DECLINED, m);
        return;

    case PECR_OP_TID:
        finish(PECR_RES_INFO, m);
        return;

    case PECR_OP_LIST:
    case PECR_OP_RECOVER:
        if (op == PECR_OP_RECOVER && recover_void_phase) {
            next_seq();
            finish(approved(m) ? PECR_RES_RECOVER_VOIDED : PECR_RES_RECOVER_VOID_FAILED, m);
            return;
        }
        if (op == PECR_OP_LIST) {
            emit(PECR_EV_LIST_ITEM, PECR_RES_NONE, m);
        } else if (!strcmp(m->tx_type, "01") && m->seq == P.pending_seq
                   && m->amount_cents == P.pending_cents) {
            /* nedovrsena prodaja JE odobrena. Iznos se provjerava i uz seq: poslije
             * gubitka trajnog stanja seq krece iz pocetka, pa isti seq moze postojati
             * u batch-u od ranije — bez provjere iznosa storno bi pogodio pogresnu. */
            rec_tx_no = m->tx_no;
        }
        if (m->more_messages) {
            msg_t0 = H->millis();      /* stize jos poruka */
            return;
        }
        if (op == PECR_OP_LIST) {
            finish(PECR_RES_INFO, m);
        } else if (rec_tx_no) {
            /* politika: kupac je platio, usluga nije isporucena -> automatski storno */
            uint32_t cents = P.pending_cents;
            logf_("nedovrsena transakcija odobrena — automatski storno%s%s", "", "");
            clear_pending();
            recover_void_phase = true;
            op_seq = P.seq;
            start_request("10", P.seq, cents, rec_tx_no);
        } else {
            clear_pending();
            finish(PECR_RES_RECOVER_NOT_FOUND, m);
        }
        return;

    default:
        return;
    }
}

static void dispatch(const pecr_msg_t *m)
{
    if (st != ST_WAIT_MSG) {
        return;                        /* zakasnjela/neocekivana poruka (vec ACK-ovana) */
    }
    if (!strcmp(m->id, "20") || !strcmp(m->id, "25")) {
        if (!strcmp(m->code, HOLD_CARD_READ)) {
            card_read = true;
        }
        msg_t0 = H->millis();
        msg_timeout = PECR_HOLD_TIMEOUT_MS;
        emit(PECR_EV_HOLD, PECR_RES_NONE, m);
    } else if (!strcmp(m->id, "22") || !strcmp(m->id, "26")) {
        got_error = true;
        msg = *m;                      /* zapamti gresku za eventualni timeout */
        msg_t0 = H->millis();
        msg_timeout = PECR_AFTER_ERROR_TIMEOUT_MS;
        emit(PECR_EV_ERROR, PECR_RES_NONE, m);
    } else if (!strcmp(m->id, "24")) {
        /* Cancel Current potvrdjen — terminal NE salje odgovor 10 */
        if (op == PECR_OP_SALE) {
            clear_pending();
            next_seq();
        }
        finish(PECR_RES_CANCELLED, m);
    } else if (!strcmp(m->id, "35")) {
        char resp[16];
        snprintf(resp, sizeof(resp), "360000001%c%c%c", FS, FS, FS);   /* 36: nastavi */
        send_aux(resp);
    } else if (!strcmp(m->id, "10")) {
        on_response10(m);
    }
}

static void on_frame(void)
{
    static pecr_msg_t m;               /* static: ne trosi stek na malim MCU */
    rx_buf[rx_len] = '\0';
    if (pecr_parse(rx_buf, rx_len, &m)) {
        logf_("<- %s %s", m.id, m.display[0] ? m.display : m.code);
        dispatch(&m);
    }
}

/* ---------------- javni API ---------------- */

void pecr_init(const pecr_hal_t *hal, const pecr_persist_t *saved)
{
    H = hal;
    memset(&P, 0, sizeof(P));
    if (saved) {
        P = *saved;
    }
    if (P.seq == 0 || P.seq > 9999u) {
        P.seq = 1;
    }
    df = DF_IDLE;
    st = ST_IDLE;
    op = PECR_OP_NONE;
    tx_wait_ack = false;
    aux_cancel_queued = false;
    recover_void_phase = false;
}

void pecr_rx_byte(uint8_t b)
{
    switch (df) {
    case DF_IDLE:
        if (b == STX) {
            df = DF_DATA;
            rx_len = 0;
            rx_lrc = 0;
        } else if (b == ACK || b == NAK) {
            on_ack(b == ACK);
        }
        break;
    case DF_DATA:
        if (b == STX && rx_len == 0) {
            break;                     /* tolerisi STX STX */
        }
        if (b == ETX) {
            rx_lrc ^= ETX;
            df = DF_LRC;
        } else if (rx_len >= PECR_RX_MAX) {
            df = DF_IDLE;              /* preduga poruka — odbaci */
        } else {
            rx_buf[rx_len++] = (char)b;
            rx_lrc ^= b;
        }
        break;
    case DF_LRC:
        df = DF_IDLE;
        if (b == rx_lrc) {
            uint8_t a = ACK;
            H->write(&a, 1);
            on_frame();
        } else {
            uint8_t n = NAK;
            H->write(&n, 1);
            logf_("pogresan LRC — NAK%s%s", "", "");
        }
        break;
    }
}

void pecr_poll(void)
{
    uint32_t now;
    if (!H) {
        return;
    }
    now = H->millis();
    if (tx_wait_ack && now - tx_t0 >= PECR_ACK_TIMEOUT_MS) {
        if (tx_tries >= PECR_ACK_RETRIES) {
            tx_wait_ack = false;
            if (tx_main && st == ST_WAIT_ACK) {
                /* sale: pending ostaje — pecr_recover() ce provjeriti da li je ipak prosla */
                finish(PECR_RES_NO_ACK, NULL);
                return;
            }
            logf_("pomocna poruka bez ACK-a%s%s", "", "");
        } else {
            tx_tries++;
            tx_t0 = now;
            H->write(tx_buf, tx_len);
        }
    }
    if (st == ST_WAIT_MSG && now - msg_t0 >= msg_timeout) {
        /* sale: pending ostaje za pecr_recover() */
        finish(got_error ? PECR_RES_TERMINAL_ERROR : PECR_RES_TIMEOUT, got_error ? &msg : NULL);
    }
}

bool pecr_busy(void)            { return st != ST_IDLE; }
bool pecr_card_read(void)       { return card_read; }
bool pecr_has_pending(void)     { return P.pending_seq != 0; }
const pecr_persist_t *pecr_persist(void) { return &P; }

void pecr_set_seq(uint16_t seq)
{
    if (st != ST_IDLE) {
        return;
    }
    P.seq = (seq == 0 || seq > 9999u) ? 1u : seq;
    save();
}

pecr_status_t pecr_start_sale(uint32_t cents)
{
    if (st != ST_IDLE) return PECR_ERR_BUSY;
    if (P.pending_seq) return PECR_ERR_PENDING;
    if (!cents) return PECR_ERR_ARG;
    op = PECR_OP_SALE;
    op_seq = P.seq;
    P.pending_seq = P.seq;             /* upisi PRIJE slanja — pad struje/veze -> recover */
    P.pending_cents = cents;
    save();
    start_request("01", P.seq, cents, 0);
    return PECR_OK;
}

pecr_status_t pecr_start_void(uint32_t tx_no, uint32_t cents)
{
    if (st != ST_IDLE) return PECR_ERR_BUSY;
    if (!tx_no || !cents) return PECR_ERR_ARG;
    op = PECR_OP_VOID;
    op_seq = P.seq;
    start_request("10", P.seq, cents, tx_no);
    return PECR_OK;
}

pecr_status_t pecr_start_tid(void)
{
    if (st != ST_IDLE) return PECR_ERR_BUSY;
    op = PECR_OP_TID;
    start_request("96", 0, 0, 0);
    return PECR_OK;
}

pecr_status_t pecr_start_list(void)
{
    if (st != ST_IDLE) return PECR_ERR_BUSY;
    op = PECR_OP_LIST;
    start_request("34", 0, 0, 0);
    return PECR_OK;
}

pecr_status_t pecr_recover(void)
{
    if (st != ST_IDLE) return PECR_ERR_BUSY;
    if (!P.pending_seq) return PECR_ERR_STATE;
    op = PECR_OP_RECOVER;
    recover_void_phase = false;
    rec_tx_no = 0;
    start_request("34", 0, 0, 0);
    return PECR_OK;
}

pecr_status_t pecr_cancel(void)
{
    if (op != PECR_OP_SALE || st == ST_IDLE) return PECR_ERR_STATE;
    if (card_read) return PECR_ERR_STATE;          /* poslije hold 54 cancel vise ne radi */
    if (tx_wait_ack) {
        aux_cancel_queued = true;                  /* posalji cim glavni zahtjev dobije ACK */
    } else {
        send_aux("23000000");
    }
    return PECR_OK;
}
