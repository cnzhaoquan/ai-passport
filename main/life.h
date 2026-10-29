// main/life.h —— Conway's Game of Life engine: pure logic, no ESP-IDF or LVGL.
//
// The world is a torus (edges wrap). Each cell stores its age in generations
// (0 = dead) so the view can color long-lived cells differently.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define LIFE_COLS 40
#define LIFE_ROWS 48
#define LIFE_HISTORY 8          // states remembered for stagnation detection
#define LIFE_RESEED_AFTER 30    // stagnant generations before an automatic reseed
#define LIFE_COLOR_LEVELS 6     // 0 = dead, 1..5 = young..old
#define LIFE_SPEED_LEVELS 6
#define LIFE_SPEED_DEFAULT 3

typedef enum {
    LIFE_PATTERN_RANDOM = 0,
    LIFE_PATTERN_GLIDER_GUN,
    LIFE_PATTERN_ACORN,
    LIFE_PATTERN_R_PENTOMINO,
    LIFE_PATTERN_PULSAR,
    LIFE_PATTERN_COUNT,
} life_pattern_t;

typedef struct {
    uint8_t cells[LIFE_ROWS][LIFE_COLS];
    uint8_t next[LIFE_ROWS][LIFE_COLS];
    uint32_t generation;
    uint16_t population;
    uint16_t stagnant_gens;      // consecutive generations that repeated a recent state
    uint32_t history[LIFE_HISTORY];
    uint8_t history_len;
    uint8_t history_pos;
    uint32_t rng;
} life_t;

void life_init(life_t *life, uint32_t seed);
void life_clear(life_t *life);
void life_set(life_t *life, int row, int col, bool alive);
bool life_alive(const life_t *life, int row, int col);

// Replace the world with a pattern and reset generation and history.
void life_seed(life_t *life, life_pattern_t pattern);
const char *life_pattern_name(life_pattern_t pattern);
// Oscillators are meant to repeat, so they never trigger an automatic reseed.
bool life_pattern_is_periodic(life_pattern_t pattern);

void life_step(life_t *life);
bool life_should_reseed(const life_t *life, life_pattern_t pattern);

// Age -> color bucket 0..LIFE_COLOR_LEVELS-1.
uint8_t life_color_level(uint8_t age);

// Speed level 0..LIFE_SPEED_LEVELS-1 -> generations per second and step period.
int life_speed_gps(int level);
uint32_t life_speed_period_ms(int level);
int life_speed_clamp(int level);
