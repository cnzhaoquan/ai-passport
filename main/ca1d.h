// main/ca1d.h —— one-dimensional cellular automata from Wolfram's "A New Kind
// of Science": the 256 elementary rules (2 colors, 3 neighbors) and the 2187
// 3-color totalistic codes. Pure logic, no ESP-IDF or LVGL.
//
// The row is a ring of CA_CELLS cells, much wider than what is displayed, so a
// pattern grown from one cell runs for hundreds of steps before its edges meet
// around the back and disturb the visible centre.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CA_CELLS 1024

typedef enum {
    CA_ELEMENTARY = 0,
    CA_TOTALISTIC,
    CA_KIND_COUNT,
} ca_kind_t;

typedef enum {
    CA_TAG_NONE = 0,
    CA_TAG_RANDOM,
    CA_TAG_COMPLEX,
    CA_TAG_NESTED,
    CA_TAG_REPETITIVE,
} ca_tag_t;

typedef struct {
    uint16_t rule;
    ca_tag_t tag;
    const char *note;
} ca_featured_t;

typedef struct {
    ca_kind_t kind;
    uint16_t rule;
    bool random_start;
    uint32_t generation;
    uint32_t rng;
    uint8_t cells[CA_CELLS];
    uint8_t next[CA_CELLS];
} ca_t;

void ca_init(ca_t *ca, uint32_t seed);
// Start over with a rule: one centre cell set, or a random row.
void ca_start(ca_t *ca, ca_kind_t kind, uint16_t rule, bool random_start);
void ca_step(ca_t *ca);
// The width cells centred on the middle of the ring.
const uint8_t *ca_window(const ca_t *ca, int width);

uint16_t ca_rule_count(ca_kind_t kind);
int ca_color_count(ca_kind_t kind);
// Number of rule cases: 8 neighborhoods (elementary) or 7 sums (totalistic).
int ca_case_count(ca_kind_t kind);
// New cell value for case index: neighborhood bits l*4+c*2+r, or the sum 0..6.
uint8_t ca_rule_output(ca_kind_t kind, uint16_t rule, int case_index);

// Rules that keep a blank background blank. Others change the background every
// row, which flickers on a scrolling display.
bool ca_rule_keeps_background(ca_kind_t kind, uint16_t rule);
// Elementary rules mapping 000->1 and 111->0 make a blank background alternate
// every row; inverting odd rows on screen keeps it steady (the colors still say
// which cells are which in each row). True when the current row should be drawn
// inverted.
bool ca_display_inverted(const ca_t *ca);
// Quick probe from a single cell: true when the pattern keeps a blank background,
// has many color boundaries and keeps changing (not empty, solid or frozen).
bool ca_rule_is_interesting(ca_kind_t kind, uint16_t rule);
// A random interesting rule that displays steadily: any elementary rule (see above), or a
// totalistic code that keeps the background blank.
uint16_t ca_random_rule(ca_t *ca, ca_kind_t kind);

// Curated rules, most random-looking first.
int ca_featured_count(ca_kind_t kind);
const ca_featured_t *ca_featured(ca_kind_t kind, int index);
// Index into the featured list, or -1.
int ca_featured_index(ca_kind_t kind, uint16_t rule);
