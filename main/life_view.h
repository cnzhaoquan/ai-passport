// main/life_view.h —— the Game of Life screen. Every function touches LVGL
// objects: callers outside the LVGL task must hold bsp_lvgl_lock().
#pragma once

#include <stdbool.h>

#include "life.h"

// Build and load the screen. Returns false when the cell canvas could not be
// allocated; an error message is shown on screen in that case.
bool life_view_create(void);

// Stop the toast timer, delete the screen and free the canvas. The screen
// must not be the active one. Safe after a failed create.
void life_view_destroy(void);

// Repaint cells whose color changed since the last call and refresh counters.
void life_view_render(const life_t *life);

void life_view_set_status(bool running, int speed_level);

// soc 0..100, or -1 to hide the indicator when the fuel gauge is unavailable.
void life_view_set_battery(int soc);

// Briefly show a message over the grid.
void life_view_toast(const char *text);
