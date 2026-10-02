/*
 * cws_link — UART link ka CWS ploci kioska (FpLink.c na CWS strani).
 *
 * Okvir oba smjera: "<payload>*<XX>\n", XX = XOR bajtova payload-a (2 hex cifre).
 *
 * ESP32 -> CWS:
 *   QR,<tekst QR koda>                procitan QR racuna (poklon pranje)
 *   AMT,<x10>,<st>                    iznos za karticu na displeju kioska;
 *                                     st 0 = unos, 1 = prinesite karticu, 2 = nije uspjelo
 *   POS,<x10>,<tx_no>,<auth>,<kartica>  odobreno placanje -> kredit kioska (POUZDANO)
 *   POSV,<x10>,<tx_no>,<1|0>          oporavak: auto storno / storno nije uspio (POUZDANO)
 *   POSE,<kod>                        greska terminala (server log)
 * CWS -> ESP32:
 *   RDY,<0|1>                         svake sekunde: kiosk spreman da primi uplatu
 *   ACK,<tip>,<tx_no>                 potvrda pouzdane poruke (POS/POSV)
 *   GIFT,<kod>,<sek> / GIFTX,<kod>,<box>   rezultat QR poklona
 *
 * Pouzdane poruke se cuvaju u NVS-u i ponavljaju svake sekunde dok CWS ne potvrdi —
 * kupac je karticom vec platio, kredit se ne smije izgubiti ni poslije restarta.
 * CWS odbacuje duplikate po <tip>,<tx_no>.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void (*cws_link_gift_cb_t)(bool xfer, int code, int value);

void cws_link_init(cws_link_gift_cb_t on_gift);
void cws_link_poll(void);                      /* glavna petlja */
void cws_link_send(const char *payload);
/* type = "POS"/"POSV"; tx_no = kljuc za ACK. false = red pun (ne bi smjelo). */
bool cws_link_send_reliable(const char *type, uint32_t tx_no, const char *payload);
bool cws_link_ready(void);                     /* RDY,1 u zadnje 3 s */
bool cws_link_up(void);                        /* bilo kakav RDY u zadnje 3 s */
void cws_link_status(void);
