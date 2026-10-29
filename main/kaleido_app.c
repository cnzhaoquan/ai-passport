// main/kaleido_app.c —— Kaleidoscope: no fixed rules, just drifting symmetric
// color. Every parameter wanders on its own; palettes cross-fade every half
// minute.
//
// Keys:
//   UP / DOWN click   fewer / more mirror folds
//   OK click          new scene (new targets and palette)
//   UP long press     next palette only
//   DOWN long press   pause / resume
#include "app.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "kaleido.h"
#include "pixmap.h"
#include "ui_chrome.h"

static const char *TAG = "kaleido";

#define CELL          3
#define CANVAS_SIZE   (KAL_GRID * CELL)       // 240
#define CANVAS_Y      42
#define FRAME_MS      50
#define AUTO_PALETTE  600                      // frames between palette changes
#define HINT_TICKS    80
#define DEFAULT_FOLD  3                        // index into kal_fold_option(): 6-fold
#define LVGL_LOCK_MS  500

kal_t g_kal;                                   // shared with the launcher thumbnail

static int s_fold = DEFAULT_FOLD;
static bool s_paused;
static uint32_t s_ticks;

static ui_chrome_t s_ui;
static lv_obj_t *s_canvas;
static uint16_t *s_pixels;

static void paint(void) {
    for (int y = 0; y < KAL_GRID; y++) {
        for (int x = 0; x < KAL_GRID; x++) {
            const int i = y * KAL_GRID + x;
            if (g_kal.radius[i] == KAL_OUTSIDE) continue;
            pix_fill_rect(s_pixels, CANVAS_SIZE, x * CELL, y * CELL, CELL, CELL,
                          g_kal.palette[g_kal.index[i]]);
        }
    }
    lv_obj_invalidate(s_canvas);
}

static void set_hint(void) {
    ui_chrome_set_hint(&s_ui, (s_ticks / HINT_TICKS) % 2 == 0
        ? LV_SYMBOL_UP LV_SYMBOL_DOWN " symmetry    OK new"
        : "hold " LV_SYMBOL_UP " colors    hold " LV_SYMBOL_DOWN " pause");
}

static void toast_folds(void) {
    char msg[16];
    lv_snprintf(msg, sizeof(msg), "%d-fold", g_kal.folds);
    ui_chrome_toast(&s_ui, msg);
}

static bool kaleido_enter(void) {
    kal_init(&g_kal, app_random(), kal_fold_option(s_fold));
    s_paused = false;
    s_ticks = 0;

    ui_chrome_create(&s_ui, "Kaleido", UI_VIOLET, CANVAS_Y + CANVAS_SIZE / 2);
    set_hint();
    lv_screen_load(s_ui.screen);

    const size_t bytes = (size_t)CANVAS_SIZE * CANVAS_SIZE * sizeof(uint16_t);
    s_pixels = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_pixels) {
        ESP_LOGE(TAG, "canvas needs %u bytes, largest free block %u", (unsigned)bytes,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    pix_fill_rect(s_pixels, CANVAS_SIZE, 0, 0, CANVAS_SIZE, CANVAS_SIZE, pix_rgb565(UI_BG));
    s_canvas = lv_canvas_create(s_ui.screen);
    lv_canvas_set_buffer(s_canvas, s_pixels, CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, CANVAS_Y);
    paint();
    ui_chrome_toast(&s_ui, kal_palette_name(g_kal.palette_id));
    return true;
}

static void kaleido_leave(void) {
    ui_chrome_destroy(&s_ui);
    s_canvas = NULL;
    heap_caps_free(s_pixels);
    s_pixels = NULL;
}

static void kaleido_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
            s_fold += btn == BSP_BTN_UP ? -1 : 1;
            const int n = kal_fold_option_count();
            s_fold = ((s_fold % n) + n) % n;
            kal_set_folds(&g_kal, kal_fold_option(s_fold));
            kal_step(&g_kal);
            paint();
            toast_folds();
        } else if (btn == BSP_BTN_OK) {
            kal_shuffle(&g_kal);
            ui_chrome_toast(&s_ui, kal_palette_name(g_kal.palette_id));
        }
    } else if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP) {
            kal_next_palette(&g_kal);
            ui_chrome_toast(&s_ui, kal_palette_name(g_kal.palette_id));
        } else if (btn == BSP_BTN_DOWN) {
            s_paused = !s_paused;
            ui_chrome_toast(&s_ui, s_paused ? "Paused" : "Flowing");
        }
    }
    bsp_lvgl_unlock();
}

static uint32_t kaleido_tick(void) {
    ++s_ticks;
    if (!s_paused) {
        // The field only lives in this task; compute it before taking the lock.
        if (s_ticks % AUTO_PALETTE == 0) kal_next_palette(&g_kal);
        kal_step(&g_kal);
    }
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return FRAME_MS;
    if (s_ticks % HINT_TICKS == 0) set_hint();
    if (!s_paused) paint();
    bsp_lvgl_unlock();
    return FRAME_MS;
}

static void kaleido_battery(int soc) {
    ui_chrome_set_battery(&s_ui, soc);
}

const app_t APP_KALEIDO = {
    .name = "Kaleidoscope",
    .enter = kaleido_enter,
    .leave = kaleido_leave,
    .key = kaleido_key,
    .tick = kaleido_tick,
    .battery = kaleido_battery,
};
