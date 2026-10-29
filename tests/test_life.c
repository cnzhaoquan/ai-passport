// Game of Life engine host test: no SDK, exercises main/life.c directly.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "life.h"

static life_t g_life;

static void step_n(life_t *life, int n) {
    for (int i = 0; i < n; i++) life_step(life);
}

static void test_blinker_oscillates(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    life_set(l, 10, 9, true);
    life_set(l, 10, 10, true);
    life_set(l, 10, 11, true);
    assert(l->population == 3);

    life_step(l);
    assert(l->population == 3);
    assert(life_alive(l, 9, 10) && life_alive(l, 10, 10) && life_alive(l, 11, 10));
    assert(!life_alive(l, 10, 9) && !life_alive(l, 10, 11));
    // The centre cell survived, so it aged; the new cells are newborn.
    assert(l->cells[10][10] == 2);
    assert(l->cells[9][10] == 1);

    life_step(l);
    assert(life_alive(l, 10, 9) && life_alive(l, 10, 11) && !life_alive(l, 9, 10));
    assert(l->generation == 2);
}

static void test_block_is_still_and_ages(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    life_set(l, 0, 0, true);
    life_set(l, 0, 1, true);
    life_set(l, 1, 0, true);
    life_set(l, 1, 1, true);
    step_n(l, 20);
    assert(l->population == 4);
    assert(l->cells[0][0] == 21);
    assert(life_color_level(l->cells[0][0]) == 5);
}

static void test_edges_wrap(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    // A blinker straddling the left/right edge must still oscillate.
    life_set(l, 5, LIFE_COLS - 1, true);
    life_set(l, 5, 0, true);
    life_set(l, 5, 1, true);
    life_step(l);
    assert(l->population == 3);
    assert(life_alive(l, 4, 0) && life_alive(l, 6, 0));
    // Negative coordinates wrap too.
    assert(life_alive(l, -LIFE_ROWS + 4, LIFE_COLS));
}

static void test_glider_travels_around_torus(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    life_set(l, 0, 1, true);
    life_set(l, 1, 2, true);
    life_set(l, 2, 0, true);
    life_set(l, 2, 1, true);
    life_set(l, 2, 2, true);
    uint8_t start[LIFE_ROWS][LIFE_COLS];
    for (int r = 0; r < LIFE_ROWS; r++)
        for (int c = 0; c < LIFE_COLS; c++) start[r][c] = l->cells[r][c] != 0;
    // A glider moves one cell diagonally every 4 generations; it returns home after
    // lcm(ROWS, COLS) cells, and must never look stagnant on the way.
    const int period = 4 * 240;   // lcm(48, 40) = 240
    for (int i = 0; i < period; i++) {
        life_step(l);
        assert(l->population == 5);
        assert(!life_should_reseed(l, LIFE_PATTERN_ACORN));
    }
    for (int r = 0; r < LIFE_ROWS; r++)
        for (int c = 0; c < LIFE_COLS; c++) assert(start[r][c] == (l->cells[r][c] != 0));
}

static void test_stagnation_and_reseed_policy(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    life_set(l, 3, 3, true);
    life_set(l, 3, 4, true);
    life_set(l, 4, 3, true);
    life_set(l, 4, 4, true);
    life_step(l);                         // first state: nothing to repeat yet
    assert(l->stagnant_gens == 0);
    step_n(l, LIFE_RESEED_AFTER - 1);
    assert(!life_should_reseed(l, LIFE_PATTERN_RANDOM));
    life_step(l);
    assert(l->stagnant_gens == LIFE_RESEED_AFTER);
    assert(life_should_reseed(l, LIFE_PATTERN_RANDOM));
    // An oscillator preset is meant to repeat and is left alone...
    assert(!life_should_reseed(l, LIFE_PATTERN_PULSAR));

    // ...unless it died out.
    life_clear(l);
    step_n(l, LIFE_RESEED_AFTER);
    assert(l->population == 0);
    assert(life_should_reseed(l, LIFE_PATTERN_PULSAR));

    // Seeding resets the counters.
    life_seed(l, LIFE_PATTERN_ACORN);
    assert(l->generation == 0 && l->stagnant_gens == 0 && l->history_len == 0);
}

static void test_patterns(void) {
    life_t *l = &g_life;
    life_init(l, 1);

    life_seed(l, LIFE_PATTERN_GLIDER_GUN);
    assert(l->population == 36 + 7);      // gun + eater
    step_n(l, 30);                        // the gun has period 30 and adds a glider
    assert(l->population == 48);
    // Long run: the eater must keep the gun alive on the 40-column torus, so the
    // world settles into an exact period-30 cycle with both gun blocks intact.
    step_n(l, 600);
    static uint8_t snap[LIFE_ROWS][LIFE_COLS];
    for (int r = 0; r < LIFE_ROWS; r++)
        for (int c = 0; c < LIFE_COLS; c++) snap[r][c] = l->cells[r][c] != 0;
    step_n(l, 30);
    for (int r = 0; r < LIFE_ROWS; r++)
        for (int c = 0; c < LIFE_COLS; c++) assert(snap[r][c] == (l->cells[r][c] != 0));
    assert(life_alive(l, 8, 2) && life_alive(l, 9, 3));     // left block
    assert(life_alive(l, 6, 36) && life_alive(l, 7, 37));   // right block
    assert(life_alive(l, 44, 16) && life_alive(l, 47, 19)); // eater
    assert(life_pattern_is_periodic(LIFE_PATTERN_GLIDER_GUN));

    life_seed(l, LIFE_PATTERN_ACORN);
    assert(l->population == 7);
    life_seed(l, LIFE_PATTERN_R_PENTOMINO);
    assert(l->population == 5);

    life_seed(l, LIFE_PATTERN_PULSAR);
    assert(l->population == 48);
    step_n(l, 3);                         // period-3 oscillator
    assert(l->population == 48);
    step_n(l, 60);
    assert(life_pattern_is_periodic(LIFE_PATTERN_PULSAR));
    assert(!life_should_reseed(l, LIFE_PATTERN_PULSAR));

    for (int p = 0; p < LIFE_PATTERN_COUNT; p++) {
        assert(strlen(life_pattern_name((life_pattern_t)p)) > 0);
    }
    assert(strcmp(life_pattern_name(LIFE_PATTERN_COUNT), "?") == 0);
}

static void test_random_soup_is_seeded(void) {
    life_t *a = &g_life;
    static life_t b;
    life_init(a, 42);
    life_init(&b, 42);
    life_seed(a, LIFE_PATTERN_RANDOM);
    life_seed(&b, LIFE_PATTERN_RANDOM);
    assert(memcmp(a->cells, b.cells, sizeof(a->cells)) == 0);
    // About 30 % density.
    const int total = LIFE_ROWS * LIFE_COLS;
    assert(a->population > total / 5 && a->population < total * 2 / 5);
    // A second soup from the same generator differs.
    life_seed(a, LIFE_PATTERN_RANDOM);
    assert(memcmp(a->cells, b.cells, sizeof(a->cells)) != 0);
    // Seed 0 must not lock xorshift at zero.
    life_init(a, 0);
    life_seed(a, LIFE_PATTERN_RANDOM);
    assert(a->population > 0);
}

static void test_speed_and_colors(void) {
    assert(life_speed_clamp(-1) == 0);
    assert(life_speed_clamp(LIFE_SPEED_LEVELS) == LIFE_SPEED_LEVELS - 1);
    assert(life_speed_gps(LIFE_SPEED_DEFAULT) == 10);
    assert(life_speed_period_ms(LIFE_SPEED_DEFAULT) == 100);
    for (int i = 1; i < LIFE_SPEED_LEVELS; i++) {
        assert(life_speed_period_ms(i) < life_speed_period_ms(i - 1));
    }
    assert(life_color_level(0) == 0);
    assert(life_color_level(1) == 1);
    assert(life_color_level(3) == 2);
    assert(life_color_level(7) == 3);
    assert(life_color_level(15) == 4);
    assert(life_color_level(255) == LIFE_COLOR_LEVELS - 1);
}

static void test_age_saturates(void) {
    life_t *l = &g_life;
    life_init(l, 1);
    life_set(l, 0, 0, true);
    life_set(l, 0, 1, true);
    life_set(l, 1, 0, true);
    life_set(l, 1, 1, true);
    step_n(l, 300);
    assert(l->cells[0][0] == UINT8_MAX);
}

int main(void) {
    test_blinker_oscillates();
    test_block_is_still_and_ages();
    test_edges_wrap();
    test_glider_travels_around_torus();
    test_stagnation_and_reseed_policy();
    test_patterns();
    test_random_soup_is_seeded();
    test_speed_and_colors();
    test_age_saturates();
    puts("test_life: OK");
    return 0;
}
