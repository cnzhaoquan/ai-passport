// main/app.h —— the apps behind the launcher and the shell services they use.
//
// One shell task owns everything: it delivers keys and calls tick() when an
// app's deadline passes, so app code never races with itself. enter/leave/
// battery run with the LVGL lock held; key/tick take the lock themselves when
// they touch the screen.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "bsp_button.h"

typedef struct {
    const char *name;
    bool (*enter)(void);                          // build and load the screen
    void (*leave)(void);                          // stop timers, delete screen, free buffers
    void (*key)(bsp_btn_t btn, bsp_btn_ev_t ev);  // OK long press never arrives here
    uint32_t (*tick)(void);                       // returns ms until the next tick
    void (*battery)(int soc);                     // -1 when the gauge is unavailable
} app_t;

extern const app_t APP_HOME;
extern const app_t APP_LIFE;
extern const app_t APP_EXPLORER;
extern const app_t APP_KALEIDO;

// Shell services, for use from key() and tick() only.
void app_open(const app_t *app);        // switch after the current call returns
void app_schedule(uint32_t ms);         // move the current app's next tick
uint32_t app_random(void);
