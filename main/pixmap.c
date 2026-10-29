// main/pixmap.c —— see pixmap.h.
#include "pixmap.h"

#include <string.h>

uint16_t pix_rgb565(uint32_t rgb) {
    const uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint32_t pix_mix(uint32_t a, uint32_t b, int t) {
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    uint32_t out = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const int ca = (int)((a >> shift) & 0xFF), cb = (int)((b >> shift) & 0xFF);
        out |= (uint32_t)((ca * (256 - t) + cb * t) >> 8) << shift;
    }
    return out;
}

void pix_gradient(uint16_t *out, int n, const uint32_t *anchors, int count) {
    for (int i = 0; i < n; i++) {
        // Position along the loop in 1/256ths of an anchor segment.
        const int pos = i * count * 256 / n;
        const int seg = pos >> 8;
        out[i] = pix_rgb565(pix_mix(anchors[seg], anchors[(seg + 1) % count], pos & 0xFF));
    }
}

void pix_fill_rect(uint16_t *px, int stride, int x, int y, int w, int h, uint16_t color) {
    uint16_t *p = px + y * stride + x;
    for (int j = 0; j < h; j++, p += stride) {
        for (int i = 0; i < w; i++) p[i] = color;
    }
}

void pix_waterfall_init(pix_waterfall_t *wf, uint16_t *px, int stride, int cols, int rows, int cell) {
    wf->px = px;
    wf->stride = stride;
    wf->cols = cols;
    wf->rows = rows;
    wf->cell = cell;
    wf->filled = 0;
}

void pix_waterfall_clear(pix_waterfall_t *wf, uint16_t bg) {
    pix_fill_rect(wf->px, wf->stride, 0, 0, wf->cols * wf->cell, wf->rows * wf->cell, bg);
    wf->filled = 0;
}

void pix_waterfall_push(pix_waterfall_t *wf, const uint8_t *states, const uint16_t *colors) {
    int row = wf->filled;
    if (row == wf->rows) {
        const int line_px = wf->cell * wf->stride;
        memmove(wf->px, wf->px + line_px,
                (size_t)(wf->rows - 1) * (size_t)line_px * sizeof(uint16_t));
        row = wf->rows - 1;
    } else {
        wf->filled++;
    }
    const int y = row * wf->cell;
    for (int c = 0; c < wf->cols; c++) {
        pix_fill_rect(wf->px, wf->stride, c * wf->cell, y, wf->cell, wf->cell, colors[states[c]]);
    }
}
