// main/main.c —— FoloToy AI Passport "Emergence": a launcher for three visual
// simulations (Game of Life, the Wolfram rule explorer and a kaleidoscope).
//
// Keys: each app defines UP / DOWN / OK clicks and UP / DOWN long presses.
// Holding OK always returns to the launcher.
//
// One shell task owns the current app: it waits on the key queue with a timeout
// equal to the time left until the app's next tick, so key handling and
// animation never race. Button callbacks only enqueue.
#include "app.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "ui_chrome.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "shell";

#define INPUT_QUEUE_DEPTH  8
#define BATTERY_PERIOD_MS  15000
#define LVGL_LOCK_MS       1000

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;

static lv_obj_t *s_blank;            // shown while one app is torn down and the next built
static const app_t *s_app;
static const app_t *s_pending;
static TickType_t s_next_tick;
static bool s_battery_ok;
static int s_soc = -1;

void app_open(const app_t *app) {
    s_pending = app;
}

void app_schedule(uint32_t ms) {
    s_next_tick = xTaskGetTickCount() + pdMS_TO_TICKS(ms);
}

uint32_t app_random(void) {
    return esp_random();
}

static void switch_to(const app_t *next) {
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) {
        ESP_LOGE(TAG, "LVGL busy; cannot open %s", next->name);
        return;
    }
    // Free the old app's buffers before the next one allocates its own.
    if (s_app) {
        lv_screen_load(s_blank);
        s_app->leave();
    }
    if (!next->enter()) {
        ESP_LOGE(TAG, "%s failed to start; back to the launcher", next->name);
        lv_screen_load(s_blank);
        next->leave();
        next = &APP_HOME;
        next->enter();
    }
    next->battery(s_soc);
    bsp_lvgl_unlock();
    s_app = next;
    s_next_tick = xTaskGetTickCount();
    ESP_LOGI(TAG, "opened %s, free heap %u", next->name,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

static void refresh_battery(void) {
    s_soc = s_battery_ok ? bsp_battery_soc() : -1;
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    s_app->battery(s_soc);
    bsp_lvgl_unlock();
}

static TickType_t ticks_until(TickType_t deadline, TickType_t now) {
    const int32_t left = (int32_t)(deadline - now);
    return left > 0 ? (TickType_t)left : 0;
}

static void shell_task(void *arg) {
    (void)arg;
    switch_to(&APP_HOME);
    TickType_t next_battery = xTaskGetTickCount() + pdMS_TO_TICKS(BATTERY_PERIOD_MS);

    for (;;) {
        TickType_t now = xTaskGetTickCount();
        TickType_t wait = ticks_until(next_battery, now);
        const TickType_t tick_wait = ticks_until(s_next_tick, now);
        if (tick_wait < wait) wait = tick_wait;

        input_event_t in;
        if (xQueueReceive(s_input_queue, &in, wait) == pdTRUE) {
            if (in.btn == BSP_BTN_OK && in.event == BSP_BTN_LONG) {
                if (s_app != &APP_HOME) app_open(&APP_HOME);
            } else {
                s_app->key(in.btn, in.event);
            }
        }

        now = xTaskGetTickCount();
        if (!s_pending && ticks_until(s_next_tick, now) == 0) {
            const uint32_t ms = s_app->tick();
            s_next_tick = xTaskGetTickCount() + pdMS_TO_TICKS(ms ? ms : 1);
        }
        if (s_pending) {
            const app_t *next = s_pending;
            s_pending = NULL;
            switch_to(next);
        }
        if (ticks_until(next_battery, now) == 0) {
            refresh_battery();
            next_battery = now + pdMS_TO_TICKS(BATTERY_PERIOD_MS);
        }
    }
}

// Waking from deep sleep by key reboots the chip while that key is still held; drop
// key actions until it comes up (see key_guard.h). The deadline is a safety net.
#define KEY_GUARD_MAX_US (10LL * 1000 * 1000)
static key_guard_t s_key_guard;

// Runs on the shared esp_timer task: enqueue only.
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user) {
    (void)user;
    // Consume the guard before the readiness check so an early release still disarms it.
    if (key_guard_consume(&s_key_guard, ev == BSP_BTN_RELEASE, esp_timer_get_time())) {
        return;
    }
    if (!s_input_ready || !s_input_queue) return;
    const input_event_t input = { .btn = btn, .event = ev };
    (void)xQueueSend(s_input_queue, &input, 0);
}

void app_main(void) {
    ESP_LOGI(TAG, "Emergence starting");
    if (esp_sleep_get_wakeup_causes() & (1u << ESP_SLEEP_WAKEUP_GPIO)) {
        key_guard_arm(&s_key_guard, esp_timer_get_time(), KEY_GUARD_MAX_US);
    }

    bsp_i2c_init();
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "display/LVGL init failed (MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(80);

    s_battery_ok = bsp_battery_init() == ESP_OK;
    if (!s_battery_ok) ESP_LOGW(TAG, "fuel gauge unavailable; battery indicator hidden");
    s_soc = s_battery_ok ? bsp_battery_soc() : -1;

    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    s_blank = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_blank, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(s_blank, LV_OPA_COVER, 0);
    lv_screen_load(s_blank);
    bsp_lvgl_unlock();

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue || xTaskCreate(shell_task, "shell", 6144, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start the shell task");
        return;
    }

    esp_err_t e = bsp_button_init(on_key, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s (apps run without input)", esp_err_to_name(e));
    } else if (s_key_guard.armed) {
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }
    s_input_ready = true;
}
