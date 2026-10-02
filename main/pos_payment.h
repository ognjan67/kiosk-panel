/*
 * pos_payment — karticno placanje (Payten terminal na UART1) sa 5 tastera:
 * 1 / 2 / 5 KM (sabiraju se, max 10 KM), PONISTI, PLACANJE.
 *
 * Port PosCard.c (CWS ploca, grana payten-pos) na ESP32. Trajno stanje payten_ecr
 * modula (seq, nedovrsena transakcija) je u NVS-u. Odobren iznos ide CWS ploci kao
 * pouzdana poruka "POS,..." (kredit kioska + MQTT serveru), iznos koji se sabira
 * kao "AMT,..." za LED displej kioska.
 */
#pragma once

#include <stdbool.h>
#include "touch_keys.h"

void pos_payment_init(void);
void pos_payment_poll(void);               /* glavna petlja */
void pos_payment_key(key_id_t key);        /* touch taster ili konzola */
void pos_payment_console(char c);          /* s = stanje, t = TID, l = lista, r = oporavak */
