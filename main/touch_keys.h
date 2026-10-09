/*
 * touch_keys — 5 kapacitivnih tastera iza pleksija + LED prsten oko svakog tastera.
 *
 * ttp = true (glavna varijanta): plocica Touch_5T_4cm — TTP223 + BC817 open-collector po
 * tasteru, linije su digitalni ulazi aktivni LOW (pull-up), prozivaju se iz glavne
 * petlje; osjetljivost se podesava kondenzatorom Cs na TTP223, ne pragom.
 *
 * ttp = false ("panel pads", bez plocice za sada): ESP32-S3 touch na golim padovima
 * (driver esp_driver_touch_sens). Prag = benchmark * prag_%; konzola "touch" ispisuje
 * benchmark i smooth vrijednosti, "thr <promil>" postavlja prag (cuva se u NVS-u).
 *
 * key_led_set(): GPIO38-42 HIGH = upaljen prsten (low-side MOSFET na glavnoj ploci).
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
