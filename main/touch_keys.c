/*
 * touch_keys.c — vidi touch_keys.h. Inicijalizacija po IDF primjeru touch_sens_basic.
 */
#include "touch_keys.h"
#include "board.h"

#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/touch_sens.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "touch";

#define DEFAULT_PERMILLE   20      /* 2 % benchmark-a — kalibrisati kroz pleksi */
#define REPEAT_GUARD_MS    250     /* jedan dodir = jedan pritisak */

static const int chan_ids[KEY_COUNT] = KEY_TOUCH_CHANS;
static const int led_gpios[KEY_COUNT] = KEY_LED_GPIOS;

static touch_sensor_handle_t  sens;
static touch_channel_handle_t chans[KEY_COUNT];
#if TOUCH_USE_SHIELD
static touch_channel_handle_t shield;
#endif
static QueueHandle_t key_q;
static uint32_t permille = DEFAULT_PERMILLE;
static int64_t  last_press_ms[KEY_COUNT];

static bool IRAM_ATTR on_active(touch_sensor_handle_t s, const touch_active_event_data_t *ev, void *ctx)
{
    BaseType_t woken = pdFALSE;
    for (int i = 0; i < KEY_COUNT; i++) {
        if (chans[i] == ev->chan) {
            key_id_t k = (key_id_t)i;
            xQueueSendFromISR(key_q, &k, &woken);
            break;
        }
    }
    return woken == pdTRUE;
}

static void apply_thresholds(void)
{
    for (int i = 0; i < KEY_COUNT; i++) {
        uint32_t bm[TOUCH_SAMPLE_CFG_NUM] = {0};
        touch_channel_config_t cfg = {
            .active_thresh = {2000},
            .charge_speed = TOUCH_CHARGE_SPEED_7,
            .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
        };
        ESP_ERROR_CHECK(touch_channel_read_data(chans[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, bm));
        cfg.active_thresh[0] = bm[0] * permille / 1000u;
        if (cfg.active_thresh[0] == 0) {
            cfg.active_thresh[0] = 1;
        }
        ESP_ERROR_CHECK(touch_sensor_reconfig_channel(chans[i], &cfg));
        ESP_LOGI(TAG, "taster %d (GPIO%d): benchmark %" PRIu32 ", prag %" PRIu32,
                 i, chan_ids[i], bm[0], cfg.active_thresh[0]);
    }
}

void touch_keys_set_threshold(uint32_t pm)
{
    nvs_handle_t h;

    if (!sens) {
        printf("[touch] touch tasteri nisu aktivni (front panel je displej)\n");
        return;
    }
    if (pm < 1 || pm > 500) {
        printf("[touch] prag mora biti 1..500 promila\n");
        return;
    }
    permille = pm;
    if (nvs_open("touch", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "permille", pm);
        nvs_commit(h);
        nvs_close(h);
    }
    /* reconfig radi samo dok je kontroler ugasen */
    ESP_ERROR_CHECK(touch_sensor_stop_continuous_scanning(sens));
    ESP_ERROR_CHECK(touch_sensor_disable(sens));
    apply_thresholds();
    ESP_ERROR_CHECK(touch_sensor_enable(sens));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens));
    printf("[touch] prag %" PRIu32 " promila\n", pm);
}

bool touch_keys_get(key_id_t *key)
{
    int64_t now = esp_timer_get_time() / 1000;

    if (!key_q) {
        return false;                           /* varijanta sa displejom — tasteri nisu aktivni */
    }

    while (xQueueReceive(key_q, key, 0) == pdTRUE) {
        if (now - last_press_ms[*key] >= REPEAT_GUARD_MS) {
            last_press_ms[*key] = now;
            return true;
        }
    }
    return false;
}

void touch_keys_dump(void)
{
    if (!sens) {
        printf("[touch] touch tasteri nisu aktivni (front panel je displej)\n");
        return;
    }
    printf("[touch] prag %" PRIu32 " promila\n", permille);
    for (int i = 0; i < KEY_COUNT; i++) {
        uint32_t bm[TOUCH_SAMPLE_CFG_NUM] = {0}, sm[TOUCH_SAMPLE_CFG_NUM] = {0};
        touch_channel_read_data(chans[i], TOUCH_CHAN_DATA_TYPE_BENCHMARK, bm);
        touch_channel_read_data(chans[i], TOUCH_CHAN_DATA_TYPE_SMOOTH, sm);
        printf("  taster %d GPIO%-2d benchmark %6" PRIu32 "  smooth %6" PRIu32 "  razlika %+5ld (%ld promila)\n",
               i, chan_ids[i], bm[0], sm[0], (long)sm[0] - (long)bm[0],
               bm[0] ? ((long)sm[0] - (long)bm[0]) * 1000L / (long)bm[0] : 0L);
    }
}

void key_led_set(key_id_t key, bool on)
{
    if (!key_q) {
        return;                                 /* LED postoje samo na plocici sa tasterima */
    }
    gpio_set_level(led_gpios[key], on ? 1 : 0);
}

void touch_keys_init(void)
{
    nvs_handle_t h;
    touch_sensor_sample_config_t sample_cfg[TOUCH_SAMPLE_CFG_NUM] = {
        TOUCH_SENSOR_V2_DEFAULT_SAMPLE_CONFIG(500, TOUCH_VOLT_LIM_L_0V5, TOUCH_VOLT_LIM_H_2V2),
    };
    touch_sensor_config_t sens_cfg = TOUCH_SENSOR_DEFAULT_BASIC_CONFIG(1, sample_cfg);
    touch_channel_config_t chan_cfg = {
        .active_thresh = {2000},
        .charge_speed = TOUCH_CHARGE_SPEED_7,
        .init_charge_volt = TOUCH_INIT_CHARGE_VOLT_DEFAULT,
    };
    touch_sensor_filter_config_t filter_cfg = TOUCH_SENSOR_DEFAULT_FILTER_CONFIG();
    touch_event_callbacks_t cbs = { .on_active = on_active };

    for (int i = 0; i < KEY_COUNT; i++) {
        gpio_reset_pin(led_gpios[i]);
        gpio_set_direction(led_gpios[i], GPIO_MODE_OUTPUT);
        gpio_set_level(led_gpios[i], 0);
    }

    if (nvs_open("touch", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "permille", &permille);
        nvs_close(h);
    }

    key_q = xQueueCreate(16, sizeof(key_id_t));
    ESP_ERROR_CHECK(touch_sensor_new_controller(&sens_cfg, &sens));
    for (int i = 0; i < KEY_COUNT; i++) {
        ESP_ERROR_CHECK(touch_sensor_new_channel(sens, chan_ids[i], &chan_cfg, &chans[i]));
    }
    ESP_ERROR_CHECK(touch_sensor_config_filter(sens, &filter_cfg));

#if TOUCH_USE_SHIELD
    {
        touch_waterproof_config_t wp = { .guard_chan = NULL, .shield_drv = 2 };
        ESP_ERROR_CHECK(touch_sensor_new_channel(sens, TOUCH_SHIELD_CHAN_ID, &chan_cfg, &shield));
        wp.shield_chan = shield;
        ESP_ERROR_CHECK(touch_sensor_config_waterproof(sens, &wp));
    }
#endif

    /* pocetno skeniranje da benchmark bude validan, pa prag relativno na njega */
    ESP_ERROR_CHECK(touch_sensor_enable(sens));
    for (int i = 0; i < 3; i++) {
        ESP_ERROR_CHECK(touch_sensor_trigger_oneshot_scanning(sens, 2000));
    }
    ESP_ERROR_CHECK(touch_sensor_disable(sens));
    apply_thresholds();

    ESP_ERROR_CHECK(touch_sensor_register_callbacks(sens, &cbs, NULL));
    ESP_ERROR_CHECK(touch_sensor_enable(sens));
    ESP_ERROR_CHECK(touch_sensor_start_continuous_scanning(sens));
    ESP_LOGI(TAG, "%d tastera, prag %" PRIu32 " promila%s", KEY_COUNT, permille,
             TOUCH_USE_SHIELD ? ", shield GPIO14" : "");
}
