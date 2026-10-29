// main/kaleido.h —— generative kaleidoscope: drifting wave fields folded into
// N-fold mirror symmetry, colored through slowly cross-fading palettes. There
// is no fixed rule: every parameter wanders toward randomly chosen targets.
// Pure logic, no ESP-IDF or LVGL.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KAL_GRID 80          // cells per side
#define KAL_OUTSIDE 0xFF     // radius marker for cells outside the circle
#define KAL_PALETTE 256

typedef struct {
    int32_t value;           // 8.8 fixed point
    int32_t target;
    int32_t min, max;
} kal_param_t;

enum { KAL_FREQ_R, KAL_FREQ_A, KAL_TWIST, KAL_SWIRL, KAL_SPEED_1, KAL_SPEED_2,
       KAL_SPEED_3, KAL_CYCLE, KAL_PARAM_COUNT };

typedef struct {
    uint8_t radius[KAL_GRID * KAL_GRID];   // 0..254 inside, KAL_OUTSIDE outside
    uint8_t angle[KAL_GRID * KAL_GRID];    // folded angle 0..255 within a half-wedge
    uint8_t index[KAL_GRID * KAL_GRID];    // palette index per cell after kal_step()
    int folds;
    kal_param_t params[KAL_PARAM_COUNT];
    uint32_t phase[4];                      // 8.8 fixed-point time phases
    uint32_t rng;
    uint32_t frames;
    // Palette cross-fade.
    uint32_t from[4], to[4];
    int fade;                               // 0..256, 256 = settled on `to`
    int palette_id;
    uint16_t palette[KAL_PALETTE];
} kal_t;

void kal_init(kal_t *k, uint32_t seed, int folds);
void kal_set_folds(kal_t *k, int folds);
// Pick new random targets for every parameter and start a palette fade.
void kal_shuffle(kal_t *k);
void kal_next_palette(kal_t *k);
// Advance one frame: drift parameters and phases, fade palette, fill index[].
void kal_step(kal_t *k);

// Symmetry orders offered by the UI, in order.
int kal_fold_option_count(void);
int kal_fold_option(int index);
int kal_palette_count(void);
const char *kal_palette_name(int id);
