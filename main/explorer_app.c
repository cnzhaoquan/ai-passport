// main/explorer_app.c —— the "New Kind of Science" rule explorer: 1D cellular
// automata scrolling down the screen as a waterfall, newest row at the bottom.
//
// Keys:
//   UP / DOWN click   previous / next featured rule (most random first)
//   OK click          toggle start: single cell / random row
//   UP long press     random rule from the whole rule space
//   DOWN long press   switch elementary <-> 3-color totalistic
#include <stdio.h>

#include "app.h"
#include "bsp_display.h"
#include "ca1d.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pixmap.h"
#include "ui_chrome.h"

static const char *TAG = "explorer";

#define CELL        2
#define VIEW_COLS   120                     // 240 px, edge to edge
#define VIEW_ROWS   108                     // 216 px
#define CANVAS_W    (VIEW_COLS * CELL)
#define CANVAS_H    (VIEW_ROWS * CELL)
#define CANVAS_Y    74
#define STRIP_W     240
#define STRIP_H     11
#define STRIP_Y     58
#define STEP_MS     50
#define HINT_TICKS  80                      // alternate hints every 4 s
#define HUES        64
#define LVGL_LOCK_MS 500

#define COLOR_OFF   0x1E2838
#define COLOR_ON    0xC9D3E3

static const uint32_t HUE_ANCHORS[] = {
    0x8CF2C6, 0x3CD3B8, 0x2A9FD6, 0x5A5CE6, 0xC86DD7, 0xFF8A80, 0xFFC86B,
};

static ca_t s_ca;
static ca_kind_t s_kind = CA_ELEMENTARY;
static int s_featured[CA_KIND_COUNT];
static bool s_random_start;
static uint32_t s_ticks;

static ui_chrome_t s_ui;
static lv_obj_t *s_note;
static lv_obj_t *s_counter;
static lv_obj_t *s_canvas;
static lv_obj_t *s_strip;
static uint16_t *s_pixels;
static uint16_t *s_strip_px;
static pix_waterfall_t s_wf;
static uint16_t s_hues[HUES];
static uint16_t s_bg;

static uint32_t tag_color(ca_tag_t tag) {
    switch (tag) {
    case CA_TAG_RANDOM:  return UI_CORAL;
    case CA_TAG_COMPLEX: return UI_AMBER;
    case CA_TAG_NESTED:  return UI_MINT;
    default:             return UI_MUTED;
    }
}

static uint16_t hue_for(uint32_t generation, int offset) {
    return s_hues[((generation >> 2) + (uint32_t)offset) % HUES];
}

static void strip_cell(int x, int y, uint16_t color) {
    pix_fill_rect(s_strip_px, STRIP_W, x, y, 5, 5, color);
}

// The rule as the book draws it: each neighborhood case, its new cell beneath.
static void draw_strip(void) {
    const uint16_t off = pix_rgb565(COLOR_OFF);
    const uint16_t on = pix_rgb565(COLOR_ON);
    const uint16_t colors[3] = { off, hue_for(0, 0), hue_for(0, 24) };
    pix_fill_rect(s_strip_px, STRIP_W, 0, 0, STRIP_W, STRIP_H, s_bg);

    const int cases = ca_case_count(s_kind);
    const int gap = s_kind == CA_ELEMENTARY ? 8 : 10;
    int x = (STRIP_W - (cases * 17 + (cases - 1) * gap)) / 2;
    for (int i = 0; i < cases; i++, x += 17 + gap) {
        const int k = cases - 1 - i;            // highest case on the left, as in NKS
        uint8_t top[3];
        if (s_kind == CA_ELEMENTARY) {
            top[0] = (k >> 2) & 1;
            top[1] = (k >> 1) & 1;
            top[2] = k & 1;
        } else {
            // One arrangement of three cells with this color sum.
            top[0] = (uint8_t)(k > 2 ? 2 : k);
            top[1] = (uint8_t)(k - top[0] > 2 ? 2 : k - top[0]);
            top[2] = (uint8_t)(k - top[0] - top[1]);
        }
        for (int c = 0; c < 3; c++) {
            const uint16_t color = s_kind == CA_ELEMENTARY ? (top[c] ? on : off) : colors[top[c]];
            strip_cell(x + c * 6, 0, color);
        }
        strip_cell(x + 6, 6, colors[ca_rule_output(s_kind, s_ca.rule, k)]);
    }
    lv_obj_invalidate(s_strip);
}

static void push_row(void) {
    uint16_t colors[3] = { s_bg, hue_for(s_ca.generation, 0), hue_for(s_ca.generation, 24) };
    if (ca_display_inverted(&s_ca)) {
        colors[0] = colors[1];
        colors[1] = s_bg;
    }
    pix_waterfall_push(&s_wf, ca_window(&s_ca, VIEW_COLS), colors);
}

// Restart the current rule from the top. Call with the LVGL lock held.
static void restart(const char *toast) {
    ca_start(&s_ca, s_kind, s_ca.rule, s_random_start);
    pix_waterfall_clear(&s_wf, s_bg);
    push_row();
    lv_obj_invalidate(s_canvas);

    lv_label_set_text_fmt(s_ui.title, s_kind == CA_ELEMENTARY ? "Rule %u" : "Code %u",
                          (unsigned)s_ca.rule);
    const int idx = ca_featured_index(s_kind, s_ca.rule);
    if (idx >= 0) {
        const ca_featured_t *f = ca_featured(s_kind, idx);
        lv_label_set_text(s_note, f->note);
        lv_obj_set_style_text_color(s_note, lv_color_hex(tag_color(f->tag)), 0);
        lv_label_set_text_fmt(s_counter, "%d/%d", idx + 1, ca_featured_count(s_kind));
    } else {
        lv_label_set_text(s_note, "Random pick");
        lv_obj_set_style_text_color(s_note, lv_color_hex(UI_VIOLET), 0);
        lv_label_set_text(s_counter, s_kind == CA_ELEMENTARY ? "of 256" : "of 2187");
    }
    draw_strip();
    if (toast) ui_chrome_toast(&s_ui, toast);
}

static void show_featured(int delta, const char *toast) {
    s_featured[s_kind] += delta;
    const int n = ca_featured_count(s_kind);
    s_featured[s_kind] = ((s_featured[s_kind] % n) + n) % n;
    s_ca.rule = ca_featured(s_kind, s_featured[s_kind])->rule;
    restart(toast);
}

static void set_hint(void) {
    ui_chrome_set_hint(&s_ui, (s_ticks / HINT_TICKS) % 2 == 0
        ? LV_SYMBOL_UP LV_SYMBOL_DOWN " rule      OK start"
        : "hold " LV_SYMBOL_UP " random    hold " LV_SYMBOL_DOWN " mode");
}

static bool explorer_enter(void) {
    s_bg = pix_rgb565(UI_BG);
    pix_gradient(s_hues, HUES, HUE_ANCHORS, (int)(sizeof(HUE_ANCHORS) / sizeof(HUE_ANCHORS[0])));
    ca_init(&s_ca, app_random());
    s_ticks = 0;

    ui_chrome_create(&s_ui, "", UI_MINT, CANVAS_Y + CANVAS_H / 2);
    s_note = ui_label(s_ui.screen, &lv_font_montserrat_14, UI_MUTED);
    lv_obj_set_pos(s_note, 30, 37);
    s_counter = ui_label(s_ui.screen, &lv_font_montserrat_14, UI_MUTED);
    lv_obj_align(s_counter, LV_ALIGN_TOP_RIGHT, -28, 37);
    set_hint();
    lv_screen_load(s_ui.screen);

    const size_t bytes = (size_t)CANVAS_W * CANVAS_H * sizeof(uint16_t);
    s_pixels = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_strip_px = heap_caps_malloc((size_t)STRIP_W * STRIP_H * sizeof(uint16_t),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_pixels || !s_strip_px) {
        ESP_LOGE(TAG, "canvas needs %u bytes, largest free block %u", (unsigned)bytes,
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return false;
    }
    s_canvas = lv_canvas_create(s_ui.screen);
    lv_canvas_set_buffer(s_canvas, s_pixels, CANVAS_W, CANVAS_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_canvas, 0, CANVAS_Y);
    s_strip = lv_canvas_create(s_ui.screen);
    lv_canvas_set_buffer(s_strip, s_strip_px, STRIP_W, STRIP_H, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(s_strip, 0, STRIP_Y);
    pix_waterfall_init(&s_wf, s_pixels, CANVAS_W, VIEW_COLS, VIEW_ROWS, CELL);

    show_featured(0, s_kind == CA_ELEMENTARY ? "Elementary rules" : "3-color totalistic");
    return true;
}

static void explorer_leave(void) {
    ui_chrome_destroy(&s_ui);   // deletes both canvases with the screen
    s_canvas = NULL;
    s_strip = NULL;
    heap_caps_free(s_pixels);
    heap_caps_free(s_strip_px);
    s_pixels = NULL;
    s_strip_px = NULL;
}

static void explorer_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return;
    if (ev == BSP_BTN_CLICK) {
        if (btn == BSP_BTN_UP) show_featured(-1, NULL);
        if (btn == BSP_BTN_DOWN) show_featured(+1, NULL);
        if (btn == BSP_BTN_OK) {
            s_random_start = !s_random_start;
            restart(s_random_start ? "Random start" : "Single cell");
        }
    } else if (ev == BSP_BTN_LONG) {
        if (btn == BSP_BTN_UP) {
            s_ca.rule = ca_random_rule(&s_ca, s_kind);
            restart("Surprise!");
        } else if (btn == BSP_BTN_DOWN) {
            s_kind = s_kind == CA_ELEMENTARY ? CA_TOTALISTIC : CA_ELEMENTARY;
            show_featured(0, s_kind == CA_ELEMENTARY ? "Elementary rules" : "3-color totalistic");
        }
    }
    bsp_lvgl_unlock();
}

static uint32_t explorer_tick(void) {
    ca_step(&s_ca);
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        push_row();
        lv_obj_invalidate(s_canvas);
        if (++s_ticks % HINT_TICKS == 0) set_hint();
        bsp_lvgl_unlock();
    }
    return STEP_MS;
}

static void explorer_battery(int soc) {
    ui_chrome_set_battery(&s_ui, soc);
}

const app_t APP_EXPLORER = {
    .name = "Rule Explorer",
    .enter = explorer_enter,
    .leave = explorer_leave,
    .key = explorer_key,
    .tick = explorer_tick,
    .battery = explorer_battery,
};
