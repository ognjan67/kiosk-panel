/*
 * qr_reader — DFR0660 / GM65 QR citac na UART2 (9600 8N1).
 *
 * Citac se podesava setup barkodovima iz njegovog uputstva (ili serijskim komandama):
 * izlaz na UART, rezim "induction" ili "continuous", rep CR. Kraj linije je CR/LF ili
 * pauza od 60 ms (radi i bez repa). Procitan link fiskalnog racuna ide CWS ploci kao
 * "QR,<link>"; isti kod u roku od 5 s se ne salje ponovo (continuous rezim cita
 * isti racun vise puta).
 */
#pragma once

void qr_reader_init(void);
void qr_reader_poll(void);                 /* glavna petlja */
void qr_reader_inject(const char *text);   /* konzola: kao da je citac procitao */
