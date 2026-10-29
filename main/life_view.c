// main/life_view.c —— Game of Life screen: a dark field of cells drawn straight
// into an RGB565 canvas, a slim header (title, run state, battery) and a footer
// with counters and the key hints for the current mode.
#include "life_view.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "life_view";

#define CELL_PX     5                       // 4 px cell + 1 px gap
#define CELL_FILL   4
#define GRID_W      (LIFE_COLS * CELL_PX)   // 200
#define GRID_H      (LIFE_ROWS * CELL_PX)   // 240
#define GRID_X      20
#define GRID_Y      40
#define TOAST_MS    1400

#define COLOR_BG      0x05070D
#define COLOR_GAP     0x0A0E17
#define COLOR_TEXT    0xE6EDF7
#define COLOR_MUTED   0x6B7A90
#define COLOR_ACCENT  0x8CF2C6

// Dead, then newborn -> ancient (see life_color_level()).
static const uint32_t CELL_COLORS[LIFE_COLOR_LEVELS] = {
    0x131A28, 0xFFF4C2, 0x8CF2C6, 0x3CD3B8, 0x2A9FD6, 0x5A5CE6,
};

static lv_obj_t *s_screen;
static lv_obj_t *s_canvas;
static lv_obj_t *s_status;
static lv_obj_t *s_battery;
static lv_obj_t *s_gen;
static lv_obj_t *s_pop;
static lv_obj_t *s_hint;
static lv_obj_t *s_toast;
static lv_timer_t *s_toast_timer;

static uint16_t *s_pixels;
static uint32_t s_stride_px;
static uint16_t s_palette[LIFE_COLOR_LEVELS];
static uint8_t s_shown[LIFE_ROWS][LIFE_COLS];

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "");
    return label;
}

static void fill_cell(int row, int col, uint16_t color) {
    uint16_t *p = s_pixels + (uint32_t)(row * CELL_PX) * s_stride_px + (uint32_t)(col * CELL_PX);
    for (int y = 0; y < CELL_FILL; y++, p += s_stride_px) {
        for (int x = 0; x < CELL_FILL; x++) p[x] = color;
    }
}

static void toast_hide_cb(lv_timer_t *timer) {
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

static bool create_canvas(void) {
    s_stride_px = lv_draw_buf_width_to_stride(GRID_W, LV_COLOR_FORMAT_RGB565) / sizeof(uint16_t);
    const size_t bytes = (size_t)s_stride_px * GRID_H * sizeof(uint16_t);
    s_pixels = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_pixels) {
        ESP_LOGE(TAG, "canvas needs %u bytes, largest free block %u", (unsigned)bytes,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }

    for (int i = 0; i < LIFE_COLOR_LEVELS; i++) {
        s_palette[i] = lv_color_to_u16(lv_color_hex(CELL_COLORS[i]));
    }
    const uint16_t gap = lv_color_to_u16(lv_color_hex(COLOR_GAP));
    for (size_t i = 0; i < (size_t)s_stride_px * GRID_H; i++) s_pixels[i] = gap;
    memset(s_shown, 0xFF, sizeof(s_shown));   // force the first render to paint everything

    s_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas, s_pixels, GRID_W, GRID_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, GRID_X, GRID_Y);
    return true;
}

bool life_view_create(void) {
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = make_label(s_screen, &lv_font_montserrat_20, COLOR_ACCENT);
    lv_label_set_text(title, "LIFE");
    lv_obj_set_pos(title, 30, 11);

    s_status = make_label(s_screen, &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_pos(s_status, 92, 15);

    s_battery = make_label(s_screen, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -28, 15);

    s_gen = make_label(s_screen, &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_pos(s_gen, GRID_X + 2, GRID_Y + GRID_H + 4);
    s_pop = make_label(s_screen, &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_align(s_pop, LV_ALIGN_TOP_RIGHT, -(GRID_X + 2), GRID_Y + GRID_H + 4);

    s_hint = make_label(s_screen, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -3);

    const bool ok = create_canvas();
    if (!ok) {
        lv_obj_t *err = make_label(s_screen, &lv_font_montserrat_14, 0xFF6B6B);
        lv_label_set_text(err, "Not enough memory\nfor the cell grid");
        lv_obj_set_style_text_align(err, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(err);
    }

    s_toast = make_label(s_screen, &lv_font_montserrat_20, COLOR_BG);
    lv_obj_set_style_bg_color(s_toast, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(s_toast, 10, 0);
    lv_obj_set_style_pad_hor(s_toast, 14, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_align(s_toast, LV_ALIGN_TOP_MID, 0, GRID_Y + GRID_H / 2 - 20);
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toast_timer = lv_timer_create(toast_hide_cb, TOAST_MS, NULL);
    lv_timer_pause(s_toast_timer);

    lv_screen_load(s_screen);
    return ok;
}

void life_view_render(const life_t *life) {
    if (s_pixels) {
        bool dirty = false;
        for (int r = 0; r < LIFE_ROWS; r++) {
            for (int c = 0; c < LIFE_COLS; c++) {
                const uint8_t level = life_color_level(life->cells[r][c]);
                if (level == s_shown[r][c]) continue;
                s_shown[r][c] = level;
                fill_cell(r, c, s_palette[level]);
                dirty = true;
            }
        }
        if (dirty) lv_obj_invalidate(s_canvas);
    }
    lv_label_set_text_fmt(s_gen, "GEN %lu", (unsigned long)life->generation);
    lv_label_set_text_fmt(s_pop, "POP %u", (unsigned)life->population);
}

void life_view_set_status(bool running, int speed_level) {
    // Same width in both states so the label never reaches the battery indicator.
    lv_label_set_text_fmt(s_status, "%s  %d/s", running ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE,
                          life_speed_gps(speed_level));
    lv_obj_set_style_text_color(s_status, lv_color_hex(running ? COLOR_TEXT : COLOR_MUTED), 0);
    if (running) {
        lv_label_set_text(s_hint, LV_SYMBOL_UP LV_SYMBOL_DOWN " speed    OK pause");
    } else {
        lv_label_set_text(s_hint, LV_SYMBOL_UP " step   " LV_SYMBOL_DOWN " world   OK run");
    }
}

void life_view_set_battery(int soc) {
    if (soc < 0) {
        lv_obj_add_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char *icon = soc >= 90 ? LV_SYMBOL_BATTERY_FULL
                     : soc >= 65 ? LV_SYMBOL_BATTERY_3
                     : soc >= 40 ? LV_SYMBOL_BATTERY_2
                     : soc >= 15 ? LV_SYMBOL_BATTERY_1
                     : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(s_battery, "%s %d%%", icon, soc);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(soc < 15 ? 0xFF6B6B : COLOR_MUTED), 0);
    lv_obj_remove_flag(s_battery, LV_OBJ_FLAG_HIDDEN);
}

void life_view_toast(const char *text) {
    lv_label_set_text(s_toast, text);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(s_toast_timer);
    lv_timer_resume(s_toast_timer);
}
