// main/life_app.c —— Game of Life as a launcher app.
//
// Keys:
//   running   UP / DOWN click = faster / slower      OK click = pause
//   paused    UP click = one step, DOWN click = next world, OK click = run
//   any time  DOWN long press = restart the current world
#include <stdio.h>

#include "app.h"
#include "bsp_display.h"
#include "life.h"
#include "life_view.h"

#define LVGL_LOCK_MS  500
#define IDLE_TICK_MS  1000

static life_t s_life;
static life_pattern_t s_pattern = LIFE_PATTERN_RANDOM;
static int s_speed = LIFE_SPEED_DEFAULT;
static bool s_running = true;

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
    app_schedule(life_speed_period_ms(s_speed));
}

static bool life_enter(void) {
    life_init(&s_life, app_random());
    s_pattern = LIFE_PATTERN_RANDOM;
    s_running = true;
    life_seed(&s_life, s_pattern);
    if (!life_view_create()) return false;
    life_view_render(&s_life);
    life_view_set_status(s_running, s_speed);
    life_view_toast(life_pattern_name(s_pattern));
    return true;
}

static void life_leave(void) {
    life_view_destroy();
}

static void life_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG) {
        seed_world();
        return;
    }
    if (ev != BSP_BTN_CLICK) return;

    switch (btn) {
    case BSP_BTN_OK:
        s_running = !s_running;
        present(NULL);
        app_schedule(s_running ? life_speed_period_ms(s_speed) : IDLE_TICK_MS);
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

static uint32_t life_tick(void) {
    if (!s_running) return IDLE_TICK_MS;
    life_step(&s_life);
    if (life_should_reseed(&s_life, s_pattern)) {
        seed_world();
    } else {
        present(NULL);
    }
    return life_speed_period_ms(s_speed);
}

static void life_battery(int soc) {
    life_view_set_battery(soc);
}

const app_t APP_LIFE = {
    .name = "Game of Life",
    .enter = life_enter,
    .leave = life_leave,
    .key = life_key,
    .tick = life_tick,
    .battery = life_battery,
};
