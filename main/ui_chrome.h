// main/ui_chrome.h —— the frame shared by the launcher, explorer and kaleidoscope
// screens: title, battery indicator, key hint and a transient toast. All calls
// touch LVGL; callers outside the LVGL task must hold bsp_lvgl_lock().
#pragma once

#include <stdint.h>

#include "lvgl.h"

#define UI_BG       0x05070D
#define UI_TEXT     0xE6EDF7
#define UI_MUTED    0x6B7A90
#define UI_MINT     0x8CF2C6
#define UI_AMBER    0xFFC86B
#define UI_CORAL    0xFF8A80
#define UI_VIOLET   0xB59CFF

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *title;
    lv_obj_t *battery;
    lv_obj_t *hint;
    lv_obj_t *toast;
    lv_timer_t *toast_timer;
} ui_chrome_t;

// Create (but do not load) a screen with the frame. toast_y is the toast's
// vertical centre.
void ui_chrome_create(ui_chrome_t *c, const char *title, uint32_t accent, int toast_y);
// Stop the toast timer and delete the screen. The screen must not be active.
void ui_chrome_destroy(ui_chrome_t *c);

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color);
void ui_chrome_set_battery(ui_chrome_t *c, int soc);   // -1 hides it
void ui_chrome_set_hint(ui_chrome_t *c, const char *text);
void ui_chrome_toast(ui_chrome_t *c, const char *text);
