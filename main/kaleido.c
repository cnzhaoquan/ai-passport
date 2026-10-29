// main/kaleido.c —— see kaleido.h.
#include "kaleido.h"

#include <math.h>
#include <string.h>

#include "pixmap.h"

static const int FOLD_OPTIONS[] = { 3, 4, 5, 6, 8, 10, 12 };

typedef struct {
    const char *name;
    uint32_t colors[4];
} palette_def_t;

// Each palette has one dark anchor so the patterns keep contrast.
static const palette_def_t PALETTES[] = {
    { "Aurora",  { 0x07102A, 0x2A9FD6, 0x8CF2C6, 0xFFF4C2 } },
    { "Sunset",  { 0x1A0B2E, 0xC8467A, 0xFF8A5C, 0xFFD27A } },
    { "Ocean",   { 0x031B2E, 0x0F6E8C, 0x5CE1E6, 0xE8FFFB } },
    { "Ember",   { 0x140404, 0x8A1C1C, 0xFF6A2B, 0xFFE08A } },
    { "Orchid",  { 0x14081F, 0x6B2FA3, 0xE08AF0, 0xFFE3F7 } },
    { "Candy",   { 0x2B0F3A, 0xFF5E9C, 0xFFD166, 0x06D6A0 } },
    { "Jade",    { 0x03140F, 0x0B6E4F, 0x6FE3A5, 0xF2FFB0 } },
    { "Nebula",  { 0x080716, 0x3B2C85, 0xF25F9A, 0x7FE7FF } },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

// Ranges in 8.8 fixed point.
static const int32_t PARAM_RANGE[KAL_PARAM_COUNT][2] = {
    [KAL_FREQ_R]  = { 128, 640 },    // radial rings: 0.5..2.5 cycles across the radius
    [KAL_FREQ_A]  = { 64, 384 },     // petal waves per half-wedge
    [KAL_TWIST]   = { -320, 320 },   // angle shift with radius (spirals)
    [KAL_SWIRL]   = { 0, 384 },      // how much the third wave bends the rings
    [KAL_SPEED_1] = { -600, 600 },   // phase speeds per frame
    [KAL_SPEED_2] = { -450, 450 },
    [KAL_SPEED_3] = { -300, 300 },
    [KAL_CYCLE]   = { 48, 256 },     // palette rotation speed
};

static int8_t s_sin[256];
static bool s_sin_ready;

static void sin_table_init(void) {
    if (s_sin_ready) return;
    for (int i = 0; i < 256; i++) {
        s_sin[i] = (int8_t)lroundf(127.0f * sinf((float)i * 6.28318531f / 256.0f));
    }
    s_sin_ready = true;
}

static inline int wave(int x) {
    return s_sin[x & 0xFF];
}

static uint32_t rng_next(kal_t *k) {
    uint32_t x = k->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    k->rng = x;
    return x;
}

static int32_t rng_range(kal_t *k, int32_t lo, int32_t hi) {
    return lo + (int32_t)(rng_next(k) % (uint32_t)(hi - lo + 1));
}

void kal_set_folds(kal_t *k, int folds) {
    if (folds < 2) folds = 2;
    k->folds = folds;
    const float half = KAL_GRID / 2.0f;
    const float wedge = 6.28318531f / (float)folds;
    for (int y = 0; y < KAL_GRID; y++) {
        for (int x = 0; x < KAL_GRID; x++) {
            const float dx = (float)x + 0.5f - half;
            const float dy = (float)y + 0.5f - half;
            const float dist = sqrtf(dx * dx + dy * dy) / half;
            const int i = y * KAL_GRID + x;
            if (dist >= 1.0f) {
                k->radius[i] = KAL_OUTSIDE;
                k->angle[i] = 0;
                continue;
            }
            k->radius[i] = (uint8_t)(dist * 254.0f);
            // Fold the angle into one wedge, then mirror within it.
            float theta = atan2f(fabsf(dy), dx);   // mirror across the horizontal axis
            float m = fmodf(theta, wedge);
            if (m > wedge * 0.5f) m = wedge - m;
            k->angle[i] = (uint8_t)(m / (wedge * 0.5f) * 255.0f);
        }
    }
}

static void param_init(kal_t *k, int id, bool snap) {
    kal_param_t *p = &k->params[id];
    p->min = PARAM_RANGE[id][0];
    p->max = PARAM_RANGE[id][1];
    p->target = rng_range(k, p->min, p->max);
    if (snap) p->value = p->target;
}

static void palette_build(kal_t *k) {
    uint32_t anchors[4];
    for (int i = 0; i < 4; i++) anchors[i] = pix_mix(k->from[i], k->to[i], k->fade);
    pix_gradient(k->palette, KAL_PALETTE, anchors, 4);
}

void kal_next_palette(kal_t *k) {
    int id = k->palette_id;
    while (id == k->palette_id) id = (int)(rng_next(k) % (uint32_t)COUNT(PALETTES));
    // Start the fade from whatever is on screen now.
    for (int i = 0; i < 4; i++) k->from[i] = pix_mix(k->from[i], k->to[i], k->fade);
    memcpy(k->to, PALETTES[id].colors, sizeof(k->to));
    k->palette_id = id;
    k->fade = 0;
}

void kal_init(kal_t *k, uint32_t seed, int folds) {
    memset(k, 0, sizeof(*k));
    sin_table_init();
    k->rng = seed ? seed : 0x9E3779B9u;
    kal_set_folds(k, folds);
    for (int id = 0; id < KAL_PARAM_COUNT; id++) param_init(k, id, true);
    k->palette_id = (int)(rng_next(k) % (uint32_t)COUNT(PALETTES));
    memcpy(k->from, PALETTES[k->palette_id].colors, sizeof(k->from));
    memcpy(k->to, k->from, sizeof(k->to));
    k->fade = 256;
    palette_build(k);
    kal_step(k);
}

void kal_shuffle(kal_t *k) {
    for (int id = 0; id < KAL_PARAM_COUNT; id++) param_init(k, id, false);
    kal_next_palette(k);
}

static void drift(kal_t *k) {
    for (int id = 0; id < KAL_PARAM_COUNT; id++) {
        kal_param_t *p = &k->params[id];
        const int32_t diff = p->target - p->value;
        // Ease 1/64 of the way per frame, at least one step.
        int32_t step = diff / 64;
        if (step == 0 && diff != 0) step = diff > 0 ? 1 : -1;
        p->value += step;
        if (p->value == p->target) p->target = rng_range(k, p->min, p->max);
    }
}

void kal_step(kal_t *k) {
    drift(k);
    k->phase[0] += (uint32_t)k->params[KAL_SPEED_1].value;
    k->phase[1] += (uint32_t)k->params[KAL_SPEED_2].value;
    k->phase[2] += (uint32_t)k->params[KAL_SPEED_3].value;
    k->phase[3] += (uint32_t)k->params[KAL_CYCLE].value;
    k->frames++;

    if (k->fade < 256) {
        k->fade += 4;            // about 64 frames per cross-fade
        if (k->fade > 256) k->fade = 256;
        palette_build(k);
    }

    const int fr = k->params[KAL_FREQ_R].value;
    const int fa = k->params[KAL_FREQ_A].value;
    const int tw = k->params[KAL_TWIST].value;
    const int sw = k->params[KAL_SWIRL].value;
    const int p0 = (int)(k->phase[0] >> 8), p1 = (int)(k->phase[1] >> 8);
    const int p2 = (int)(k->phase[2] >> 8), p3 = (int)(k->phase[3] >> 8);

    for (int i = 0; i < KAL_GRID * KAL_GRID; i++) {
        const int r = k->radius[i];
        if (r == KAL_OUTSIDE) {
            k->index[i] = 0;
            continue;
        }
        const int a = k->angle[i];
        const int bend = (wave(a * 2 + p2) * sw) >> 8;                // -255..255
        const int v1 = wave((((r + bend) * fr) >> 8) - p0);
        const int v2 = wave(((a * fa) >> 8) + ((r * tw) >> 8) + p1);
        const int v3 = wave(((r * a) >> 9) + p2);
        // The average is -127..127: about one palette loop across the field, so
        // colors form broad bands instead of speckle.
        k->index[i] = (uint8_t)((v1 + v2 + v3) / 3 + p3);
    }
}

int kal_fold_option_count(void) {
    return COUNT(FOLD_OPTIONS);
}

int kal_fold_option(int index) {
    const int n = COUNT(FOLD_OPTIONS);
    index %= n;
    if (index < 0) index += n;
    return FOLD_OPTIONS[index];
}

int kal_palette_count(void) {
    return COUNT(PALETTES);
}

const char *kal_palette_name(int id) {
    return id >= 0 && id < COUNT(PALETTES) ? PALETTES[id].name : "?";
}
