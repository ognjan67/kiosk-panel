/*
 * payten_ecr.h — Payten ECR protokol (ECR SPEC v2.63), ECR strana.
 *
 * Prenosiv C99 modul: bez malloc-a, bez HAL-a, bez blokiranja. Radi na STM32
 * (CWS mjenjacnica/kiosk) i ESP32. Port Python prototipa
 * CWP2026/Tools/payten_ecr/payten_ecr.py — ponasanje provjereno na terminalu
 * PAY10 250429/01 (TID 1017CSTL, 9600 8N1).
 *
 * Upotreba:
 *   pecr_init(&hal);                       // jednom, sa HAL callback-ovima
 *   pecr_rx_byte(b);                       // za svaki primljeni bajt (iz main loop-a,
 *                                          //   NE iz ISR-a — moze poslati ACK)
 *   pecr_poll();                           // periodicno (npr. svakih 10 ms)
 *   pecr_start_sale(300);                  // 3,00 KM; rezultat stize kroz hal.event
 *   pecr_cancel();                         // PONISTI — radi samo prije citanja kartice
 *   if (pecr_has_pending()) pecr_recover(); // po startu: izgubljen odgovor -> auto storno
 *
 * Specificnosti terminala (razlikuju se od spec-a, provjereno):
 *   - zahtjev bez iznosa = samo header + FS (inace "06 POGRESAN UNOS");
 *   - poslije Cancel Current (23 -> 24) NE dolazi odgovor 10;
 *   - odgovor na Void nosi seq ORIGINALNE prodaje;
 *   - Cancel previous (11) i ponavljanje istog seq-a NE rade — oporavak ide preko
 *     liste transakcija (tip 34), gdje svaka transakcija nosi seq originalnog zahtjeva.
 */
#ifndef PAYTEN_ECR_H
#define PAYTEN_ECR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef PECR_BUILD_DLL
#define PECR_API __declspec(dllexport)
#else
#define PECR_API
#endif

#ifndef PECR_RX_MAX
#define PECR_RX_MAX        1024u   /* max DATA dio primljene poruke (odgovor 10 sa EMV ~400) */
#endif
#define PECR_TX_MAX        128u    /* max poslat okvir */

#define PECR_ACK_TIMEOUT_MS        1000u
#define PECR_ACK_RETRIES           3u
#define PECR_FIRST_MSG_TIMEOUT_MS  30000u
#define PECR_HOLD_TIMEOUT_MS       120000u
#define PECR_AFTER_ERROR_TIMEOUT_MS 5000u

/* ---------------- parsirana poruka terminala ---------------- */
typedef struct {
    char     id[3];          /* "10", "25", "26", "24", ... */
    uint16_t seq;
    char     tx_type[3];
    char     flag[3];        /* "01"/"02" odobreno, "04" odbijeno, "06" greska komunikacije */
    uint32_t tx_no;
    uint16_t batch;
    char     date[7];        /* DDMMYY */
    char     time[7];        /* HHMMSS */
    uint32_t amount_cents;
    char     card_source;    /* 'C' contactless, 'S' chip, 'M' magnet, ... */
    char     pan[20];        /* maskiran */
    char     auth_code[7];
    char     tid[9];
    char     company[17];    /* MASTERCARD, VISA, ... ili izdavalac/TOTAL kod tipa 34 */
    char     display[26];
    char     code[4];        /* kod hold-a (25) / greske (26) */
    char     full_resp_code[7];
    char     rrn[13];
    bool     more_messages;
    /* zbirovi (tip 32/34): broj i iznos zaduzenja */
    uint16_t debit_count;
    uint32_t debit_cents;
} pecr_msg_t;

/* ---------------- dogadjaji prema aplikaciji ---------------- */
typedef enum {
    PECR_EV_HOLD = 1,        /* msg.code/display: 51 ubaci karticu, 54 kartica procitana, ... */
    PECR_EV_ERROR,           /* Extended Error (26); transakcija jos moze zavrsiti */
    PECR_EV_DONE,            /* kraj operacije — vidi result */
    PECR_EV_LIST_ITEM,       /* pecr_start_list: jedna transakcija ili zbir izdavaoca */
} pecr_event_kind_t;

typedef enum {
    PECR_RES_NONE = 0,
    PECR_RES_APPROVED,           /* Sale/Void odobren (msg = odgovor 10) */
    PECR_RES_DECLINED,           /* odgovor 10 sa flagom != 01/02 */
    PECR_RES_CANCELLED,          /* Cancel Current potvrdjen (24) — nista naplaceno */
    PECR_RES_NO_ACK,             /* terminal ne potvrdjuje (3 pokusaja) */
    PECR_RES_TIMEOUT,            /* nema finalnog odgovora -> ostaje PENDING (pecr_recover) */
    PECR_RES_TERMINAL_ERROR,     /* Extended Error bez odgovora 10 */
    PECR_RES_INFO,               /* TID / lista zavrsena (msg = zadnja poruka) */
    PECR_RES_RECOVER_NOT_FOUND,  /* nedovrsena transakcija nije naplacena */
    PECR_RES_RECOVER_VOIDED,     /* bila je odobrena -> automatski stornirana */
    PECR_RES_RECOVER_VOID_FAILED,/* bila je odobrena, storno NIJE uspio — rucna intervencija */
} pecr_result_t;

typedef enum {
    PECR_OP_NONE = 0, PECR_OP_SALE, PECR_OP_VOID, PECR_OP_TID, PECR_OP_LIST, PECR_OP_RECOVER,
} pecr_op_t;

typedef struct {
    pecr_event_kind_t kind;
    pecr_op_t         op;
    pecr_result_t     result;     /* samo za PECR_EV_DONE */
    const pecr_msg_t *msg;        /* moze biti NULL */
} pecr_event_t;

/* Trajno stanje — aplikacija ga cuva (EEPROM/NVS) i vraca pri startu. */
typedef struct {
    uint16_t seq;             /* sljedeci sequence broj, 1..9999 */
    uint16_t pending_seq;     /* 0 = nema nedovrsenog zahtjeva */
    uint32_t pending_cents;
} pecr_persist_t;

typedef struct {
    void     (*write)(const uint8_t *buf, uint16_t len);   /* posalji na UART */
    uint32_t (*millis)(void);
    void     (*event)(const pecr_event_t *ev);
    void     (*save)(const pecr_persist_t *p);             /* upisi trajno stanje */
    void     (*log)(const char *text);                     /* opciono, moze NULL */
} pecr_hal_t;

typedef enum {
    PECR_OK = 0,
    PECR_ERR_BUSY,            /* operacija vec u toku */
    PECR_ERR_PENDING,         /* postoji nedovrsena transakcija — prvo pecr_recover() */
    PECR_ERR_ARG,
    PECR_ERR_STATE,           /* npr. cancel poslije citanja kartice */
} pecr_status_t;

/* ---------------- API ---------------- */
PECR_API void          pecr_init(const pecr_hal_t *hal, const pecr_persist_t *saved);
PECR_API void          pecr_rx_byte(uint8_t b);
PECR_API void          pecr_poll(void);
PECR_API bool          pecr_busy(void);
PECR_API bool          pecr_card_read(void);      /* stigao hold 54 -> cancel vise ne radi */
PECR_API bool          pecr_has_pending(void);
PECR_API const pecr_persist_t *pecr_persist(void);
/* Postavlja sljedeci seq (1..9999) i upisuje stanje — npr. poslije gubitka trajnog
 * stanja: seq = najveci seq iz liste transakcija (tip 34) + 1. Samo kad !pecr_busy(). */
PECR_API void          pecr_set_seq(uint16_t seq);

PECR_API pecr_status_t pecr_start_sale(uint32_t cents);
PECR_API pecr_status_t pecr_start_void(uint32_t tx_no, uint32_t cents);
PECR_API pecr_status_t pecr_start_tid(void);
PECR_API pecr_status_t pecr_start_list(void);      /* tip 34; svaka stavka kao PECR_EV_LIST_ITEM */
PECR_API pecr_status_t pecr_recover(void);
PECR_API pecr_status_t pecr_cancel(void);

/* ---------------- cisti protokol (bez stanja) — izlozeno radi testova ---------------- */
PECR_API uint8_t  pecr_lrc(const uint8_t *data, uint16_t len);
/* Pravi okvir STX STX <data> ETX LRC u out; vraca duzinu ili 0 ako ne stane. */
PECR_API uint16_t pecr_frame(const char *data, uint8_t *out, uint16_t out_size);
/* Poruka 00 u out (C string); tx_type "01","10","34","96"; cents 0 = bez iznosa. */
PECR_API uint16_t pecr_build_request(char *out, uint16_t out_size, const char *tx_type,
                                     uint16_t seq, uint32_t cents, uint32_t tx_no);
PECR_API bool     pecr_parse(const char *data, uint16_t len, pecr_msg_t *m);

#endif /* PAYTEN_ECR_H */
