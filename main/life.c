// main/life.c —— Game of Life engine. See life.h.
#include "life.h"

#include <stddef.h>
#include <string.h>

static const int SPEED_GPS[LIFE_SPEED_LEVELS] = { 1, 2, 5, 10, 20, 30 };

typedef struct {
    const char *name;
    const char *const *rows;
    int row_count;
    bool periodic;
} pattern_def_t;

static const char *const GLIDER_GUN[] = {
    "........................O...........",
    "......................O.O...........",
    "............OO......OO............OO",
    "...........O...O....OO............OO",
    "OO........O.....O...OO..............",
    "OO........O...O.OO....O.O...........",
    "..........O.....O.......O...........",
    "...........O...O....................",
    "............OO......................",
};

// The world is only 40 cells wide, so on the torus the gun's own gliders would wrap
// around and wreck it within ~90 generations. An eater at the bottom absorbs each
// glider after its trip down the screen (placement found by simulation, see
// tests/test_life.c), keeping the whole system periodic.
static const char *const GUN_EATER[] = {
    "OO..",
    "O.O.",
    "..O.",
    "..OO",
};
#define GUN_TOP       4
#define GUN_EATER_ROW 44
#define GUN_EATER_COL 16

static const char *const ACORN[] = {
    ".O.....",
    "...O...",
    "OO..OOO",
};

static const char *const R_PENTOMINO[] = {
    ".OO",
    "OO.",
    ".O.",
};

static const char *const PULSAR[] = {
    "..OOO...OOO..",
    ".............",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    "..OOO...OOO..",
    ".............",
    "..OOO...OOO..",
    "O....O.O....O",
    "O....O.O....O",
    "O....O.O....O",
    ".............",
    "..OOO...OOO..",
};

#define ROWS_OF(a) (a), (int)(sizeof(a) / sizeof((a)[0]))

static const pattern_def_t PATTERNS[LIFE_PATTERN_COUNT] = {
    [LIFE_PATTERN_RANDOM]      = { "Random soup", NULL, 0, false },
    [LIFE_PATTERN_GLIDER_GUN]  = { "Glider gun", ROWS_OF(GLIDER_GUN), true },
    [LIFE_PATTERN_ACORN]       = { "Acorn", ROWS_OF(ACORN), false },
    [LIFE_PATTERN_R_PENTOMINO] = { "R-pentomino", ROWS_OF(R_PENTOMINO), false },
    [LIFE_PATTERN_PULSAR]      = { "Pulsar", ROWS_OF(PULSAR), true },
};

static uint32_t rng_next(life_t *life) {
    uint32_t x = life->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    life->rng = x;
    return x;
}

static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

static uint32_t world_hash(const life_t *life) {
    uint32_t h = 2166136261u;
    for (int r = 0; r < LIFE_ROWS; r++) {
        for (int c = 0; c < LIFE_COLS; c++) {
            h ^= life->cells[r][c] ? 1u : 0u;
            h *= 16777619u;
        }
    }
    return h;
}

static void history_reset(life_t *life) {
    life->history_len = 0;
    life->history_pos = 0;
    life->stagnant_gens = 0;
}

void life_init(life_t *life, uint32_t seed) {
    memset(life, 0, sizeof(*life));
    life->rng = seed ? seed : 0x9E3779B9u;   // xorshift must not start at zero
}

void life_clear(life_t *life) {
    memset(life->cells, 0, sizeof(life->cells));
    life->generation = 0;
    life->population = 0;
    history_reset(life);
}

void life_set(life_t *life, int row, int col, bool alive) {
    row = wrap(row, LIFE_ROWS);
    col = wrap(col, LIFE_COLS);
    uint8_t *cell = &life->cells[row][col];
    if (alive && !*cell) {
        *cell = 1;
        life->population++;
    } else if (!alive && *cell) {
        *cell = 0;
        life->population--;
    }
}

bool life_alive(const life_t *life, int row, int col) {
    return life->cells[wrap(row, LIFE_ROWS)][wrap(col, LIFE_COLS)] != 0;
}

const char *life_pattern_name(life_pattern_t pattern) {
    return pattern < LIFE_PATTERN_COUNT ? PATTERNS[pattern].name : "?";
}

bool life_pattern_is_periodic(life_pattern_t pattern) {
    return pattern < LIFE_PATTERN_COUNT && PATTERNS[pattern].periodic;
}

static void stamp(life_t *life, const char *const *rows, int height, int top, int left) {
    for (int r = 0; r < height; r++) {
        for (int c = 0; rows[r][c]; c++) {
            if (rows[r][c] == 'O') life_set(life, top + r, left + c, true);
        }
    }
}

void life_seed(life_t *life, life_pattern_t pattern) {
    life_clear(life);
    if (pattern >= LIFE_PATTERN_COUNT) pattern = LIFE_PATTERN_RANDOM;

    if (pattern == LIFE_PATTERN_RANDOM) {
        for (int r = 0; r < LIFE_ROWS; r++) {
            for (int c = 0; c < LIFE_COLS; c++) {
                if (rng_next(life) % 100 < 30) life_set(life, r, c, true);
            }
        }
        return;
    }

    const pattern_def_t *def = &PATTERNS[pattern];
    const int height = def->row_count;
    const int width = (int)strlen(def->rows[0]);
    // The gun sits near the top so its gliders have room to travel; the others centre.
    if (pattern == LIFE_PATTERN_GLIDER_GUN) {
        stamp(life, def->rows, height, GUN_TOP, (LIFE_COLS - width) / 2);
        stamp(life, GUN_EATER, (int)(sizeof(GUN_EATER) / sizeof(GUN_EATER[0])),
              GUN_EATER_ROW, GUN_EATER_COL);
    } else {
        stamp(life, def->rows, height, (LIFE_ROWS - height) / 2, (LIFE_COLS - width) / 2);
    }
}

void life_step(life_t *life) {
    uint16_t population = 0;
    for (int r = 0; r < LIFE_ROWS; r++) {
        const int up = r == 0 ? LIFE_ROWS - 1 : r - 1;
        const int down = r == LIFE_ROWS - 1 ? 0 : r + 1;
        for (int c = 0; c < LIFE_COLS; c++) {
            const int left = c == 0 ? LIFE_COLS - 1 : c - 1;
            const int right = c == LIFE_COLS - 1 ? 0 : c + 1;
            const int n = (life->cells[up][left] != 0) + (life->cells[up][c] != 0)
                        + (life->cells[up][right] != 0) + (life->cells[r][left] != 0)
                        + (life->cells[r][right] != 0) + (life->cells[down][left] != 0)
                        + (life->cells[down][c] != 0) + (life->cells[down][right] != 0);
            const uint8_t age = life->cells[r][c];
            uint8_t next = 0;
            if (age && (n == 2 || n == 3)) {
                next = age == UINT8_MAX ? age : (uint8_t)(age + 1);
            } else if (!age && n == 3) {
                next = 1;
            }
            life->next[r][c] = next;
            population += next != 0;
        }
    }
    memcpy(life->cells, life->next, sizeof(life->cells));
    life->population = population;
    life->generation++;

    const uint32_t h = world_hash(life);
    bool repeated = population == 0;
    for (int i = 0; i < life->history_len && !repeated; i++) {
        repeated = life->history[i] == h;
    }
    if (repeated) {
        if (life->stagnant_gens < UINT16_MAX) life->stagnant_gens++;
    } else {
        life->stagnant_gens = 0;
    }
    life->history[life->history_pos] = h;
    life->history_pos = (uint8_t)((life->history_pos + 1) % LIFE_HISTORY);
    if (life->history_len < LIFE_HISTORY) life->history_len++;
}

bool life_should_reseed(const life_t *life, life_pattern_t pattern) {
    if (life->population == 0) return life->stagnant_gens >= LIFE_RESEED_AFTER;
    return !life_pattern_is_periodic(pattern) && life->stagnant_gens >= LIFE_RESEED_AFTER;
}

uint8_t life_color_level(uint8_t age) {
    if (age == 0) return 0;
    if (age == 1) return 1;
    if (age < 4) return 2;
    if (age < 8) return 3;
    if (age < 16) return 4;
    return 5;
}

int life_speed_clamp(int level) {
    if (level < 0) return 0;
    if (level >= LIFE_SPEED_LEVELS) return LIFE_SPEED_LEVELS - 1;
    return level;
}

int life_speed_gps(int level) {
    return SPEED_GPS[life_speed_clamp(level)];
}

uint32_t life_speed_period_ms(int level) {
    return 1000u / (uint32_t)life_speed_gps(level);
}
