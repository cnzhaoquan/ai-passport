// main/ca1d.c —— 1D cellular automata engine. See ca1d.h.
#include "ca1d.h"

#include <string.h>

// Ranked by how badly each pattern (grown from one cell) compresses: the
// random-looking ones first, then complex, nested and repetitive examples.
static const ca_featured_t ELEMENTARY[] = {
    { 30,  CA_TAG_RANDOM,     "Most random, class 3" },
    { 45,  CA_TAG_RANDOM,     "Random, class 3" },
    { 73,  CA_TAG_RANDOM,     "Random with walls" },
    { 110, CA_TAG_COMPLEX,    "Complex, universal" },
    { 54,  CA_TAG_COMPLEX,    "Complex, class 4" },
    { 90,  CA_TAG_NESTED,     "Nested, Sierpinski" },
    { 150, CA_TAG_NESTED,     "Nested" },
    { 22,  CA_TAG_NESTED,     "Nested" },
    { 126, CA_TAG_NESTED,     "Nested" },
    { 60,  CA_TAG_NESTED,     "Nested, Pascal mod 2" },
    { 18,  CA_TAG_NESTED,     "Nested" },
    { 184, CA_TAG_REPETITIVE, "Traffic flow" },
    { 250, CA_TAG_REPETITIVE, "Repetitive" },
    { 50,  CA_TAG_REPETITIVE, "Repetitive" },
    { 4,   CA_TAG_REPETITIVE, "Stable" },
};

static const ca_featured_t TOTALISTIC[] = {
    { 1149, CA_TAG_RANDOM,  "Random" },
    { 1596, CA_TAG_RANDOM,  "Random" },
    { 906,  CA_TAG_RANDOM,  "Random" },
    { 1266, CA_TAG_RANDOM,  "Random" },
    { 1317, CA_TAG_RANDOM,  "Random" },
    { 1560, CA_TAG_RANDOM,  "Random" },
    { 1599, CA_TAG_COMPLEX, "Complex, class 4" },
    { 1635, CA_TAG_COMPLEX, "Complex" },
    { 357,  CA_TAG_COMPLEX, "Complex" },
    { 348,  CA_TAG_NESTED,  "Nested" },
    { 1308, CA_TAG_NESTED,  "Nested" },
    { 1023, CA_TAG_NESTED,  "Nested" },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static uint32_t rng_next(ca_t *ca) {
    uint32_t x = ca->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ca->rng = x;
    return x;
}

void ca_init(ca_t *ca, uint32_t seed) {
    memset(ca, 0, sizeof(*ca));
    ca->rng = seed ? seed : 0x9E3779B9u;
    ca_start(ca, CA_ELEMENTARY, 30, false);
}

void ca_start(ca_t *ca, ca_kind_t kind, uint16_t rule, bool random_start) {
    if (kind >= CA_KIND_COUNT) kind = CA_ELEMENTARY;
    ca->kind = kind;
    ca->rule = rule % ca_rule_count(kind);
    ca->random_start = random_start;
    ca->generation = 0;
    memset(ca->cells, 0, sizeof(ca->cells));
    if (random_start) {
        const uint32_t colors = (uint32_t)ca_color_count(kind);
        for (int i = 0; i < CA_CELLS; i++) ca->cells[i] = (uint8_t)(rng_next(ca) % colors);
    } else {
        ca->cells[CA_CELLS / 2] = 1;
    }
}

uint16_t ca_rule_count(ca_kind_t kind) {
    return kind == CA_TOTALISTIC ? 2187 : 256;   // 3^7 codes, 2^8 rules
}

int ca_color_count(ca_kind_t kind) {
    return kind == CA_TOTALISTIC ? 3 : 2;
}

int ca_case_count(ca_kind_t kind) {
    return kind == CA_TOTALISTIC ? 7 : 8;
}

uint8_t ca_rule_output(ca_kind_t kind, uint16_t rule, int case_index) {
    if (kind == CA_TOTALISTIC) {
        // Base-3 digit case_index of the code.
        for (int i = 0; i < case_index; i++) rule /= 3;
        return (uint8_t)(rule % 3);
    }
    return (uint8_t)((rule >> case_index) & 1);
}

void ca_step(ca_t *ca) {
    uint8_t table[8];
    const int cases = ca_case_count(ca->kind);
    for (int i = 0; i < cases; i++) table[i] = ca_rule_output(ca->kind, ca->rule, i);

    const uint8_t *c = ca->cells;
    if (ca->kind == CA_TOTALISTIC) {
        for (int i = 0; i < CA_CELLS; i++) {
            const int l = c[i == 0 ? CA_CELLS - 1 : i - 1];
            const int r = c[i == CA_CELLS - 1 ? 0 : i + 1];
            ca->next[i] = table[l + c[i] + r];
        }
    } else {
        for (int i = 0; i < CA_CELLS; i++) {
            const int l = c[i == 0 ? CA_CELLS - 1 : i - 1];
            const int r = c[i == CA_CELLS - 1 ? 0 : i + 1];
            ca->next[i] = table[(l << 2) | (c[i] << 1) | r];
        }
    }
    memcpy(ca->cells, ca->next, sizeof(ca->cells));
    ca->generation++;
}

const uint8_t *ca_window(const ca_t *ca, int width) {
    if (width > CA_CELLS) width = CA_CELLS;
    return ca->cells + CA_CELLS / 2 - width / 2;   // the seed cell is CA_CELLS / 2
}

bool ca_rule_keeps_background(ca_kind_t kind, uint16_t rule) {
    return ca_rule_output(kind, rule, 0) == 0;
}

bool ca_display_inverted(const ca_t *ca) {
    return ca->kind == CA_ELEMENTARY && (ca->generation & 1)
        && ca_rule_output(CA_ELEMENTARY, ca->rule, 0) == 1
        && ca_rule_output(CA_ELEMENTARY, ca->rule, 7) == 0;
}

#define PROBE_STEPS 72
#define PROBE_WIDTH 160

// Displayed value of a cell, after the alternate-row inversion.
static uint8_t shown(const ca_t *ca, int i) {
    return (uint8_t)(ca->cells[i] ^ (ca_display_inverted(ca) ? 1 : 0));
}

bool ca_rule_is_interesting(ca_kind_t kind, uint16_t rule) {
    static ca_t probe;   // single-threaded callers; keeps 2 KB off the task stack
    const int base = CA_CELLS / 2 - PROBE_WIDTH / 2;
    uint8_t prev[PROBE_WIDTH];
    int changed_rows = 0;
    int edges = 0;
    probe.rng = 1;
    ca_start(&probe, kind, rule, false);
    for (int t = 0; t < PROBE_STEPS; t++) {
        bool changed = false;
        for (int i = 0; i < PROBE_WIDTH; i++) {
            const uint8_t v = shown(&probe, base + i);
            changed |= t > 0 && v != prev[i];
            // Color boundaries, counted over the second half of the run.
            if (t >= PROBE_STEPS / 2 && i > 0) edges += v != prev[i - 1];
            prev[i] = v;
        }
        changed_rows += changed;
        ca_step(&probe);
    }
    // The background far from the seed must stay blank on screen.
    if (shown(&probe, 0) != 0) return false;
    // A lively pattern averages many boundaries per row (a solid wedge has two)
    // and keeps changing from row to row (not a frozen line or still stripe).
    return edges >= 8 * (PROBE_STEPS / 2) && changed_rows >= PROBE_STEPS / 2;
}

uint16_t ca_random_rule(ca_t *ca, ca_kind_t kind) {
    const uint16_t n = ca_rule_count(kind);
    for (;;) {
        const uint16_t rule = (uint16_t)(rng_next(ca) % n);
        if (kind == CA_TOTALISTIC && !ca_rule_keeps_background(kind, rule)) continue;
        if (ca_rule_is_interesting(kind, rule)) return rule;
    }
}

int ca_featured_count(ca_kind_t kind) {
    return kind == CA_TOTALISTIC ? COUNT(TOTALISTIC) : COUNT(ELEMENTARY);
}

const ca_featured_t *ca_featured(ca_kind_t kind, int index) {
    const int n = ca_featured_count(kind);
    index %= n;
    if (index < 0) index += n;
    return kind == CA_TOTALISTIC ? &TOTALISTIC[index] : &ELEMENTARY[index];
}

int ca_featured_index(ca_kind_t kind, uint16_t rule) {
    for (int i = 0; i < ca_featured_count(kind); i++) {
        if (ca_featured(kind, i)->rule == rule) return i;
    }
    return -1;
}
