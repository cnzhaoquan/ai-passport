// main/ui_chrome.c —— see ui_chrome.h.
#include "ui_chrome.h"

#define TOAST_MS 1400

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, "");
    return label;
}

static void toast_hide_cb(lv_timer_t *timer) {
    ui_chrome_t *c = lv_timer_get_user_data(timer);
    lv_obj_add_flag(c->toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

void ui_chrome_create(ui_chrome_t *c, const char *title, uint32_t accent, int toast_y) {
    c->screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(c->screen, lv_color_hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(c->screen, LV_OPA_COVER, 0);
    lv_obj_remove_flag(c->screen, LV_OBJ_FLAG_SCROLLABLE);

    c->title = ui_label(c->screen, &lv_font_montserrat_20, accent);
    lv_label_set_text(c->title, title);
    lv_obj_set_pos(c->title, 30, 11);

    c->battery = ui_label(c->screen, &lv_font_montserrat_14, UI_MUTED);
    lv_obj_align(c->battery, LV_ALIGN_TOP_RIGHT, -28, 15);
    lv_obj_add_flag(c->battery, LV_OBJ_FLAG_HIDDEN);

    c->hint = ui_label(c->screen, &lv_font_montserrat_14, UI_MUTED);
    lv_obj_align(c->hint, LV_ALIGN_BOTTOM_MID, 0, -3);

    // ui_chrome_toast() moves it in front of children the screen adds later.
    c->toast = ui_label(c->screen, &lv_font_montserrat_20, UI_BG);
    lv_obj_set_style_bg_color(c->toast, lv_color_hex(accent), 0);
    lv_obj_set_style_bg_opa(c->toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(c->toast, 10, 0);
    lv_obj_set_style_pad_hor(c->toast, 14, 0);
    lv_obj_set_style_pad_ver(c->toast, 8, 0);
    lv_obj_align(c->toast, LV_ALIGN_TOP_MID, 0, toast_y - 20);
    lv_obj_add_flag(c->toast, LV_OBJ_FLAG_HIDDEN);
    c->toast_timer = lv_timer_create(toast_hide_cb, TOAST_MS, c);
    lv_timer_pause(c->toast_timer);
}

void ui_chrome_destroy(ui_chrome_t *c) {
    if (c->toast_timer) {
        lv_timer_delete(c->toast_timer);
        c->toast_timer = NULL;
    }
    if (c->screen) {
        lv_obj_delete(c->screen);
        c->screen = NULL;
    }
}

void ui_chrome_set_battery(ui_chrome_t *c, int soc) {
    if (soc < 0) {
        lv_obj_add_flag(c->battery, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const char *icon = soc >= 90 ? LV_SYMBOL_BATTERY_FULL
                     : soc >= 65 ? LV_SYMBOL_BATTERY_3
                     : soc >= 40 ? LV_SYMBOL_BATTERY_2
                     : soc >= 15 ? LV_SYMBOL_BATTERY_1
                     : LV_SYMBOL_BATTERY_EMPTY;
    lv_label_set_text_fmt(c->battery, "%s %d%%", icon, soc);
    lv_obj_set_style_text_color(c->battery, lv_color_hex(soc < 15 ? 0xFF6B6B : UI_MUTED), 0);
    lv_obj_remove_flag(c->battery, LV_OBJ_FLAG_HIDDEN);
}

void ui_chrome_set_hint(ui_chrome_t *c, const char *text) {
    lv_label_set_text(c->hint, text);
}

void ui_chrome_toast(ui_chrome_t *c, const char *text) {
    lv_label_set_text(c->toast, text);
    lv_obj_move_foreground(c->toast);
    lv_obj_remove_flag(c->toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_reset(c->toast_timer);
    lv_timer_resume(c->toast_timer);
}
