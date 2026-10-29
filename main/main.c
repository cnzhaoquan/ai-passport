// main/main.c —— Game of Life for the FoloToy AI Passport.
//
// Keys:
//   running   UP / DOWN click = faster / slower      OK click = pause
//   paused    UP click = one step, DOWN click = next world, OK click = run
//   any time  OK long press   = restart the current world
//
// One app task owns the simulation: it waits on the key queue with a timeout
// equal to the time left until the next generation, so key handling and
// stepping never race. Button callbacks only enqueue.
#include <stdio.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "key_guard.h"
#include "life.h"
#include "life_view.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "life";

#define INPUT_QUEUE_DEPTH  8
#define BATTERY_PERIOD_MS  15000
#define LVGL_LOCK_MS       500

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static QueueHandle_t s_input_queue;
static volatile bool s_input_ready;

static life_t s_life;
static life_pattern_t s_pattern = LIFE_PATTERN_RANDOM;
static int s_speed = LIFE_SPEED_DEFAULT;
static bool s_running = true;
static bool s_battery_ok;

// Push state to the screen. toast may be NULL.
static void present(const char *toast) {
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    life_view_render(&s_life);
    life_view_set_status(s_running, s_speed);
    if (toast) life_view_toast(toast);
    bsp_lvgl_unlock();
}

static void seed_world(void) {
    life_seed(&s_life, s_pattern);
    present(life_pattern_name(s_pattern));
}

static void change_speed(int delta) {
    char msg[16];
    s_speed = life_speed_clamp(s_speed + delta);
    snprintf(msg, sizeof(msg), "%d gen/s", life_speed_gps(s_speed));
    present(msg);
}

static void handle_input(const input_event_t *in) {
    if (in->btn == BSP_BTN_OK && in->event == BSP_BTN_LONG) {
        seed_world();
        return;
    }
    if (in->event != BSP_BTN_CLICK) return;

    switch (in->btn) {
    case BSP_BTN_OK:
        s_running = !s_running;
        present(NULL);
        break;
    case BSP_BTN_UP:
        if (s_running) {
            change_speed(+1);
        } else {
            life_step(&s_life);
            present(NULL);
        }
        break;
    case BSP_BTN_DOWN:
        if (s_running) {
            change_speed(-1);
        } else {
            s_pattern = (life_pattern_t)((s_pattern + 1) % LIFE_PATTERN_COUNT);
            seed_world();
        }
        break;
    }
}

static void refresh_battery(void) {
    const int soc = s_battery_ok ? bsp_battery_soc() : -1;
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    life_view_set_battery(soc);
    bsp_lvgl_unlock();
}

static TickType_t ticks_until(TickType_t deadline, TickType_t now) {
    const int32_t left = (int32_t)(deadline - now);
    return left > 0 ? (TickType_t)left : 0;
}

static void life_task(void *arg) {
    (void)arg;
    TickType_t now = xTaskGetTickCount();
    TickType_t next_step = now + pdMS_TO_TICKS(life_speed_period_ms(s_speed));
    TickType_t next_battery = now + pdMS_TO_TICKS(BATTERY_PERIOD_MS);

    for (;;) {
        now = xTaskGetTickCount();
        TickType_t wait = ticks_until(next_battery, now);
        if (s_running) {
            const TickType_t step_wait = ticks_until(next_step, now);
            if (step_wait < wait) wait = step_wait;
        }

        input_event_t in;
        if (xQueueReceive(s_input_queue, &in, wait) == pdTRUE) {
            handle_input(&in);
        }

        now = xTaskGetTickCount();
        if (!s_running) {
            // Resume on a fresh period instead of catching up.
            next_step = now + pdMS_TO_TICKS(life_speed_period_ms(s_speed));
        } else if (ticks_until(next_step, now) == 0) {
            life_step(&s_life);
            if (life_should_reseed(&s_life, s_pattern)) {
                seed_world();
            } else {
                present(NULL);
            }
            next_step = now + pdMS_TO_TICKS(life_speed_period_ms(s_speed));
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
    ESP_LOGI(TAG, "Game of Life starting");
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

    life_init(&s_life, esp_random());
    life_seed(&s_life, s_pattern);

    bool view_ok = false;
    if (bsp_lvgl_lock(1000)) {
        view_ok = life_view_create();
        life_view_set_battery(s_battery_ok ? bsp_battery_soc() : -1);
        life_view_render(&s_life);
        life_view_set_status(s_running, s_speed);
        life_view_toast(life_pattern_name(s_pattern));
        bsp_lvgl_unlock();
    }
    if (!view_ok) {
        ESP_LOGE(TAG, "view unavailable; simulation not started");
        return;
    }

    s_input_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_input_queue || xTaskCreate(life_task, "life", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to start the simulation task");
        return;
    }

    esp_err_t e = bsp_button_init(on_key, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "button init failed: %s (simulation runs without input)", esp_err_to_name(e));
    } else if (s_key_guard.armed) {
        key_guard_sync_released(&s_key_guard,
                                bsp_button_read_mv() >= BSP_BTN_MV_RELEASED_MIN);
    }
    s_input_ready = true;
    ESP_LOGI(TAG, "ready: %dx%d world, battery=%d", LIFE_COLS, LIFE_ROWS, s_battery_ok);
}
