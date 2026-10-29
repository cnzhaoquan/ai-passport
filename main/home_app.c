// main/home_app.c —— launcher: three cards, each with a live miniature of its
// simulation running inside.
//
// Keys: UP / DOWN choose a card, OK opens it. Holding OK in any app returns here.
#include "app.h"
#include "bsp_display.h"
#include "ca1d.h"
#include "esp_heap_caps.h"
#include "kaleido.h"
#include "life.h"
#include "pixmap.h"
#include "ui_chrome.h"

#define CARD_COUNT   3
#define CARD_X       14
#define CARD_W       212
#define CARD_H       72
#define CARD_Y0      46
#define CARD_STEP    78
#define THUMB        60
#define THUMB_CELLS  30                      // 2 px cells for Life and the rule
#define FRAME_MS     100
#define LVGL_LOCK_MS 500

typedef struct {
    const app_t *app;
    const char *name;
    const char *subtitle;
    uint32_t accent;
} card_def_t;

static const card_def_t CARDS[CARD_COUNT] = {
    { &APP_LIFE,     "Game of Life",  "Conway, 1970",          UI_MINT },
    { &APP_EXPLORER, "Rule Explorer", "Wolfram, 2002",         UI_AMBER },
    { &APP_KALEIDO,  "Kaleidoscope",  "no fixed rules",        UI_VIOLET },
};

// Rules the miniature cycles through.
static const uint16_t THUMB_RULES[] = { 30, 110, 90, 45, 150, 73 };

extern kal_t g_kal;                          // owned by kaleido_app.c, idle while here

static int s_selected;
static uint32_t s_frames;
static ui_chrome_t s_ui;
static lv_obj_t *s_cards[CARD_COUNT];
static lv_obj_t *s_thumbs[CARD_COUNT];
static uint16_t *s_thumb_px[CARD_COUNT];

static life_t s_life;
static ca_t s_ca;
static int s_rule_idx;
static pix_waterfall_t s_wf;
static uint16_t s_life_colors[LIFE_COLOR_LEVELS];
static uint16_t s_bg;

static void style_cards(void) {
    for (int i = 0; i < CARD_COUNT; i++) {
        const bool on = i == s_selected;
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(on ? CARDS[i].accent : 0x1A2233), 0);
        lv_obj_set_style_bg_color(s_cards[i], lv_color_hex(on ? 0x111A2B : 0x0B111C), 0);
    }
}

static lv_obj_t *make_card(int i) {
    lv_obj_t *card = lv_obj_create(s_ui.screen);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, CARD_X, CARD_Y0 + i * CARD_STEP);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *name = ui_label(card, &lv_font_montserrat_20, UI_TEXT);
    lv_label_set_text(name, CARDS[i].name);
    lv_obj_set_pos(name, 72, 13);
    lv_obj_t *sub = ui_label(card, &lv_font_montserrat_14, CARDS[i].accent);
    lv_label_set_text(sub, CARDS[i].subtitle);
    lv_obj_set_pos(sub, 72, 41);
    return card;
}

static void draw_life(void) {
    uint16_t *px = s_thumb_px[0];
    for (int r = 0; r < THUMB_CELLS; r++) {
        for (int c = 0; c < THUMB_CELLS; c++) {
            const uint8_t level = life_color_level(s_life.cells[r + 8][c + 4]);
            pix_fill_rect(px, THUMB, c * 2, r * 2, 2, 2, level ? s_life_colors[level] : s_bg);
        }
    }
}

static void draw_kaleido(void) {
    uint16_t *px = s_thumb_px[2];
    for (int y = 0; y < THUMB; y++) {
        for (int x = 0; x < THUMB; x++) {
            const int i = (y * KAL_GRID / THUMB) * KAL_GRID + x * KAL_GRID / THUMB;
            px[y * THUMB + x] = g_kal.radius[i] == KAL_OUTSIDE ? s_bg
                              : g_kal.palette[g_kal.index[i]];
        }
    }
}

static void push_rule_row(void) {
    static const uint32_t hue[] = { UI_AMBER, UI_CORAL, UI_VIOLET, UI_MINT };
    uint16_t colors[2] = { s_bg, pix_rgb565(hue[s_rule_idx % 4]) };
    if (ca_display_inverted(&s_ca)) {
        colors[0] = colors[1];
        colors[1] = s_bg;
    }
    pix_waterfall_push(&s_wf, ca_window(&s_ca, THUMB_CELLS), colors);
}

static void start_rule(void) {
    ca_start(&s_ca, CA_ELEMENTARY, THUMB_RULES[s_rule_idx], false);
    pix_waterfall_clear(&s_wf, s_bg);
    push_rule_row();
}

static bool home_enter(void) {
    s_bg = pix_rgb565(0x0B111C);
    static const uint32_t life_hex[LIFE_COLOR_LEVELS] = {
        0x0B111C, 0xFFF4C2, 0x8CF2C6, 0x3CD3B8, 0x2A9FD6, 0x5A5CE6,
    };
    for (int i = 0; i < LIFE_COLOR_LEVELS; i++) s_life_colors[i] = pix_rgb565(life_hex[i]);

    ui_chrome_create(&s_ui, "Emergence", UI_MINT, 160);
    ui_chrome_set_hint(&s_ui, LV_SYMBOL_UP LV_SYMBOL_DOWN " choose    OK open");
    lv_obj_t *tip = ui_label(s_ui.screen, &lv_font_montserrat_14, 0x46536A);
    lv_label_set_text(tip, "hold OK in an app to come back");
    lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -21);

    for (int i = 0; i < CARD_COUNT; i++) {
        s_cards[i] = make_card(i);
        s_thumb_px[i] = heap_caps_malloc(THUMB * THUMB * sizeof(uint16_t),
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!s_thumb_px[i]) continue;   // the card still works without its miniature
        pix_fill_rect(s_thumb_px[i], THUMB, 0, 0, THUMB, THUMB, s_bg);
        s_thumbs[i] = lv_canvas_create(s_cards[i]);
        lv_canvas_set_buffer(s_thumbs[i], s_thumb_px[i], THUMB, THUMB, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_pos(s_thumbs[i], 4, 4);
        lv_obj_set_style_radius(s_thumbs[i], 11, 0);
        lv_obj_set_style_clip_corner(s_thumbs[i], true, 0);
    }
    style_cards();

    life_init(&s_life, app_random());
    life_seed(&s_life, LIFE_PATTERN_RANDOM);
    ca_init(&s_ca, app_random());
    if (s_thumb_px[1]) pix_waterfall_init(&s_wf, s_thumb_px[1], THUMB, THUMB_CELLS, THUMB_CELLS, 2);
    if (s_thumb_px[1]) start_rule();
    kal_init(&g_kal, app_random(), 6);
    s_frames = 0;

    lv_screen_load(s_ui.screen);
    return true;
}

static void home_leave(void) {
    ui_chrome_destroy(&s_ui);
    for (int i = 0; i < CARD_COUNT; i++) {
        heap_caps_free(s_thumb_px[i]);
        s_thumb_px[i] = NULL;
        s_thumbs[i] = NULL;
        s_cards[i] = NULL;
    }
}

static void home_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        app_open(CARDS[s_selected].app);
        return;
    }
    s_selected = (s_selected + (btn == BSP_BTN_DOWN ? 1 : CARD_COUNT - 1)) % CARD_COUNT;
    if (bsp_lvgl_lock(LVGL_LOCK_MS)) {
        style_cards();
        bsp_lvgl_unlock();
    }
}

static uint32_t home_tick(void) {
    s_frames++;
    if (s_frames % 2 == 0) {
        life_step(&s_life);
        if (life_should_reseed(&s_life, LIFE_PATTERN_RANDOM) || s_life.generation > 400) {
            life_seed(&s_life, LIFE_PATTERN_RANDOM);
        }
    }
    ca_step(&s_ca);
    if (s_ca.generation >= 96) {
        s_rule_idx = (s_rule_idx + 1) % (int)(sizeof(THUMB_RULES) / sizeof(THUMB_RULES[0]));
    }
    kal_step(&g_kal);

    if (!bsp_lvgl_lock(LVGL_LOCK_MS)) return FRAME_MS;
    if (s_thumb_px[0]) draw_life();
    if (s_thumb_px[1]) {
        if (s_ca.generation >= 96) start_rule();
        else push_rule_row();
    }
    if (s_thumb_px[2]) draw_kaleido();
    for (int i = 0; i < CARD_COUNT; i++) {
        if (s_thumbs[i]) lv_obj_invalidate(s_thumbs[i]);
    }
    bsp_lvgl_unlock();
    return FRAME_MS;
}

static void home_battery(int soc) {
    ui_chrome_set_battery(&s_ui, soc);
}

const app_t APP_HOME = {
    .name = "Launcher",
    .enter = home_enter,
    .leave = home_leave,
    .key = home_key,
    .tick = home_tick,
    .battery = home_battery,
};
