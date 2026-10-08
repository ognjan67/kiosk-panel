/*
 * touch_keys — 5 kapacitivnih tastera iza pleksija (ESP32-S3 touch, novi driver
 * esp_driver_touch_sens) + LED ispod svakog tastera.
 *
 * Prag = benchmark * prag_%. Kroz 5 mm pleksi promjena je mala (red velicine
 * 1-3 %), pa se prag podesava na licu mjesta: konzola "touch" ispisuje benchmark i
 * smooth vrijednosti, "thr <promil>" postavlja prag (cuva se u NVS-u).
 *
 * Rezerva: TTP223 na plocici sa padovima (ttp = true) — isti pinovi su tada digitalni
 * ulazi (aktivno HIGH), prozivaju se iz glavne petlje; osjetljivost se podesava
 * kondenzatorom Cs na TTP223, ne pragom.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { KEY_1KM = 0, KEY_2KM, KEY_5KM, KEY_CANCEL, KEY_PAY } key_id_t;

void touch_keys_init(bool ttp);            /* false = ESP32 touch, true = TTP223 izlazi */
bool touch_keys_get(key_id_t *key);        /* sljedeci pritisak (neblokirajuce) */
void touch_keys_dump(void);
void touch_keys_set_threshold(uint32_t permille);
void key_led_set(key_id_t key, bool on);
