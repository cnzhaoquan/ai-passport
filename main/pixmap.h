// main/pixmap.h —— small pixel helpers for RGB565 canvases: color conversion,
// gradient palettes and a scrolling "waterfall" of cell rows. Pure logic, no
// ESP-IDF or LVGL, so it is host-tested.
#pragma once

#include <stdint.h>

// 0xRRGGBB -> RGB565 in the same layout as lv_color_to_u16().
uint16_t pix_rgb565(uint32_t rgb);
// Blend two 0xRRGGBB colors, t in 0..256.
uint32_t pix_mix(uint32_t a, uint32_t b, int t);

// Fill out[n] with a looping gradient through anchors[count] (0xRRGGBB);
// the last entry blends back toward the first.
void pix_gradient(uint16_t *out, int n, const uint32_t *anchors, int count);

void pix_fill_rect(uint16_t *px, int stride, int x, int y, int w, int h, uint16_t color);
// Paint the pixels outside rounded corners of radius r with color. Cheaper than
// LVGL's clip_corner, which renders through an off-screen layer that the 24 KB
// LVGL heap cannot hold.
void pix_round_corners(uint16_t *px, int stride, int w, int h, int r, uint16_t color);

// A canvas filled top to bottom with rows of square cells; once full, every new
// row scrolls the picture up by one cell.
typedef struct {
    uint16_t *px;
    int stride;        // pixels per canvas line
    int cols;          // cells per row
    int rows;          // rows that fit
    int cell;          // cell size in pixels
    int filled;        // rows drawn so far (<= rows)
} pix_waterfall_t;

void pix_waterfall_init(pix_waterfall_t *wf, uint16_t *px, int stride, int cols, int rows, int cell);
void pix_waterfall_clear(pix_waterfall_t *wf, uint16_t bg);
// Append one row: states[cols] index into colors[].
void pix_waterfall_push(pix_waterfall_t *wf, const uint8_t *states, const uint16_t *colors);
