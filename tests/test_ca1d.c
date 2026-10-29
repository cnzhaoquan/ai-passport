// 1D cellular automaton, pixel helper and kaleidoscope host tests: no SDK.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ca1d.h"
#include "kaleido.h"
#include "pixmap.h"

static ca_t g_ca;
static kal_t g_kal;

// Window of 2*half+1 cells around the centre.
static void centre(const ca_t *ca, int half, char *out) {
    const uint8_t *w = ca_window(ca, 2 * half + 1);
    for (int i = 0; i < 2 * half + 1; i++) out[i] = (char)('0' + w[i]);
    out[2 * half + 1] = 0;
}

static void test_rule30_rows(void) {
    ca_t *ca = &g_ca;
    char row[16];
    ca_init(ca, 1);
    ca_start(ca, CA_ELEMENTARY, 30, false);
    // The first rows of rule 30 from a single cell (NKS p. 27).
    const char *expect[] = {
        "000000010000000",
        "000000111000000",
        "000001100100000",
        "000011011110000",
        "000110010001000",
        "001101111011100",
    };
    for (int t = 0; t < 6; t++) {
        centre(ca, 7, row);
        assert(strcmp(row, expect[t]) == 0);
        ca_step(ca);
    }
    // The centre column of rule 30 starts 1,1,0,1,1,1,0,0,1,1,0,0,0,1 (OEIS A051023).
    const char centre_bits[] = "11011100110001";
    ca_start(ca, CA_ELEMENTARY, 30, false);
    for (int t = 0; centre_bits[t]; t++) {
        assert(ca_window(ca, 1)[0] == (uint8_t)(centre_bits[t] - '0'));
        ca_step(ca);
    }
}

static void test_rule90_is_pascal_mod_2(void) {
    ca_t *ca = &g_ca;
    ca_init(ca, 1);
    ca_start(ca, CA_ELEMENTARY, 90, false);
    for (int t = 0; t < 64; t++) {
        const uint8_t *w = ca_window(ca, 129);   // index 64 is the centre
        for (int x = -64; x <= 64; x++) {
            int expect = 0;
            if (((t + x) & 1) == 0 && x >= -t && x <= t) {
                const int k = (t + x) / 2;            // C(t, k) odd iff k & ~t == 0
                expect = (k & ~t) == 0;
            }
            assert(w[64 + x] == expect);
        }
        ca_step(ca);
    }
}

static void test_mirror_rule_is_mirror_image(void) {
    // Rule 86 is rule 30 reflected left-right.
    static ca_t a, b;
    ca_init(&a, 1);
    ca_init(&b, 1);
    ca_start(&a, CA_ELEMENTARY, 30, false);
    ca_start(&b, CA_ELEMENTARY, 86, false);
    for (int t = 0; t < 200; t++) {
        const uint8_t *wa = ca_window(&a, 301), *wb = ca_window(&b, 301);
        for (int i = 0; i < 301; i++) assert(wa[i] == wb[300 - i]);
        ca_step(&a);
        ca_step(&b);
    }
}

static void test_totalistic(void) {
    // Digits of a code are the new color for neighborhood sums 0..6.
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 0) == 0);   // 1599 = 2012020 in base 3
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 1) == 2);
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 2) == 0);
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 3) == 2);
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 4) == 1);
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 5) == 0);
    assert(ca_rule_output(CA_TOTALISTIC, 1599, 6) == 2);

    ca_t *ca = &g_ca;
    char row[16];
    ca_init(ca, 1);
    ca_start(ca, CA_TOTALISTIC, 1599, false);
    ca_step(ca);
    centre(ca, 7, row);
    assert(strcmp(row, "000000222000000") == 0);   // sum 1 -> 2
    ca_step(ca);
    centre(ca, 7, row);
    assert(strcmp(row, "000000121000000") == 0);   // sums 2,4,6,4,2 -> 0,1,2,1,0
}

static void test_featured_and_random(void) {
    for (int k = 0; k < CA_KIND_COUNT; k++) {
        const ca_kind_t kind = (ca_kind_t)k;
        assert(ca_featured_count(kind) > 5);
        assert(ca_featured(kind, 0)->tag == CA_TAG_RANDOM);
        for (int i = 0; i < ca_featured_count(kind); i++) {
            const ca_featured_t *f = ca_featured(kind, i);
            assert(f->rule < ca_rule_count(kind));
            if (kind == CA_TOTALISTIC) assert(ca_rule_keeps_background(kind, f->rule));
            assert(ca_featured_index(kind, f->rule) == i);
            // Notes are drawn with the built-in ASCII font.
            for (const char *c = f->note; *c; c++) assert((unsigned char)*c < 0x80);
        }
        assert(ca_featured(kind, -1) == ca_featured(kind, ca_featured_count(kind) - 1));
    }
    assert(ca_featured(CA_ELEMENTARY, 0)->rule == 30);
    assert(ca_featured_index(CA_ELEMENTARY, 31) == -1);

    // Random picks skip empty, solid and frozen patterns.
    assert(ca_rule_is_interesting(CA_ELEMENTARY, 30));
    assert(ca_rule_is_interesting(CA_ELEMENTARY, 45));
    assert(ca_rule_is_interesting(CA_ELEMENTARY, 110));
    assert(ca_rule_is_interesting(CA_ELEMENTARY, 90));
    assert(!ca_rule_is_interesting(CA_ELEMENTARY, 0));
    assert(!ca_rule_is_interesting(CA_ELEMENTARY, 4));     // a frozen single line
    assert(!ca_rule_is_interesting(CA_ELEMENTARY, 204));   // identity
    assert(!ca_rule_is_interesting(CA_ELEMENTARY, 255));   // solid
    assert(!ca_rule_is_interesting(CA_ELEMENTARY, 254));   // solid wedge
    assert(!ca_rule_is_interesting(CA_TOTALISTIC, 1830));  // solid wedge
    assert(ca_rule_is_interesting(CA_TOTALISTIC, 1599));
    assert(ca_rule_is_interesting(CA_TOTALISTIC, 1149));
    int lively = 0;
    for (int r = 0; r < 256; r++) lively += ca_rule_is_interesting(CA_ELEMENTARY, (uint16_t)r);
    printf("interesting elementary rules: %d of 256\n", lively);
    assert(lively > 20 && lively < 200);

    ca_t *ca = &g_ca;
    ca_init(ca, 99);
    for (int i = 0; i < 100; i++) {
        const uint16_t e = ca_random_rule(ca, CA_ELEMENTARY);
        const uint16_t t = ca_random_rule(ca, CA_TOTALISTIC);
        assert(e < 256 && ca_rule_is_interesting(CA_ELEMENTARY, e));
        assert(t < 2187 && t % 3 == 0 && ca_rule_is_interesting(CA_TOTALISTIC, t));
    }

    // Random start uses every color and blank rules keep a blank row blank.
    ca_start(ca, CA_TOTALISTIC, 1599, true);
    int seen[3] = { 0 };
    for (int i = 0; i < CA_CELLS; i++) seen[ca->cells[i]]++;
    assert(seen[0] && seen[1] && seen[2]);
    ca_start(ca, CA_ELEMENTARY, 0, false);
    ca_step(ca);
    for (int i = 0; i < CA_CELLS; i++) assert(ca->cells[i] == 0);
    assert(ca->generation == 1);
}

static void test_alternating_background_is_inverted_on_screen(void) {
    // Rule 45 turns a blank row solid and back every step; drawn with odd rows
    // inverted, the far background stays blank.
    ca_t *ca = &g_ca;
    ca_init(ca, 1);
    ca_start(ca, CA_ELEMENTARY, 45, false);
    for (int t = 0; t < 40; t++) {
        const int raw = ca->cells[10];   // far from the pattern
        const int shown = raw ^ (ca_display_inverted(ca) ? 1 : 0);
        assert(raw == (t & 1));
        assert(shown == 0);
        ca_step(ca);
    }
    ca_start(ca, CA_ELEMENTARY, 30, false);
    ca_step(ca);
    assert(!ca_display_inverted(ca));
}

static void test_pixmap(void) {
    assert(pix_rgb565(0xFFFFFF) == 0xFFFF);
    assert(pix_rgb565(0xFF0000) == 0xF800);
    assert(pix_rgb565(0x00FF00) == 0x07E0);
    assert(pix_rgb565(0x0000FF) == 0x001F);
    assert(pix_mix(0x000000, 0xFFFFFF, 0) == 0x000000);
    assert(pix_mix(0x000000, 0xFFFFFF, 256) == 0xFFFFFF);
    assert(pix_mix(0x000000, 0xFF8000, 128) == 0x7F4000);

    uint16_t grad[64];
    const uint32_t anchors[] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF };
    pix_gradient(grad, 64, anchors, 4);
    assert(grad[0] == pix_rgb565(0xFF0000));
    assert(grad[16] == pix_rgb565(0x00FF00));
    assert(grad[48] == pix_rgb565(0xFFFFFF));

    // Waterfall: 4 columns x 3 rows of 2 px cells on a 10 px wide canvas.
    static uint16_t px[10 * 6];
    pix_waterfall_t wf;
    const uint16_t colors[] = { 0, 7, 9 };
    pix_waterfall_init(&wf, px, 10, 4, 3, 2);
    pix_waterfall_clear(&wf, 0);
    const uint8_t rows[5][4] = { {1,0,0,0}, {0,1,0,0}, {0,0,1,0}, {0,0,0,1}, {2,2,2,2} };
    for (int r = 0; r < 5; r++) pix_waterfall_push(&wf, rows[r], colors);
    assert(wf.filled == 3);
    // Rows 2, 3, 4 remain, top to bottom; both pixel lines of each cell match.
    for (int line = 0; line < 2; line++) {
        assert(px[(0 + line) * 10 + 4] == 7 && px[(0 + line) * 10 + 0] == 0);
        assert(px[(2 + line) * 10 + 6] == 7 && px[(2 + line) * 10 + 7] == 7);
        assert(px[(4 + line) * 10 + 0] == 9 && px[(4 + line) * 10 + 7] == 9);
    }
    assert(px[8] == 0 && px[9] == 0);   // the area beside the cells is untouched

    // Rounded corners: only pixels outside the arc change, symmetrically.
    static uint16_t sq[12 * 12];
    for (int i = 0; i < 12 * 12; i++) sq[i] = 1;
    pix_round_corners(sq, 12, 12, 12, 4, 5);
    assert(sq[0] == 5 && sq[11] == 5 && sq[11 * 12] == 5 && sq[12 * 12 - 1] == 5);
    assert(sq[1] == 5 && sq[12] == 5);                  // still outside the arc
    assert(sq[3] == 1 && sq[3 * 12] == 1);              // on the edge past the curve
    assert(sq[6 * 12 + 6] == 1 && sq[4] == 1);          // interior and straight edge
    int changed = 0;
    for (int i = 0; i < 12 * 12; i++) changed += sq[i] == 5;
    assert(changed % 4 == 0 && changed > 4 && changed < 4 * 16);
}

static int abs_diff(int a, int b) {
    return a > b ? a - b : b - a;
}

static void test_kaleidoscope_symmetry(void) {
    kal_t *k = &g_kal;
    for (int o = 0; o < kal_fold_option_count(); o++) {
        const int folds = kal_fold_option(o);
        kal_init(k, 1234u + (uint32_t)o, folds);
        const int g = KAL_GRID;
        int inside = 0;
        for (int y = 0; y < g; y++) {
            for (int x = 0; x < g; x++) {
                const int i = y * g + x;
                // Mirror across both axes always holds: geometry depends on |dy| and
                // the circle is centred.
                const int my = (g - 1 - y) * g + x;
                assert(k->radius[i] == k->radius[my]);
                assert(abs_diff(k->angle[i], k->angle[my]) <= 1);
                if (k->radius[i] != KAL_OUTSIDE) inside++;
                // Even fold counts are also symmetric left-right.
                if (folds % 2 == 0) {
                    const int mx = y * g + (g - 1 - x);
                    assert(abs_diff(k->angle[i], k->angle[mx]) <= 1);
                }
                // Multiples of 4 survive a quarter turn.
                if (folds % 4 == 0) {
                    const int rot = x * g + (g - 1 - y);
                    assert(k->radius[i] == k->radius[rot]);
                    assert(abs_diff(k->angle[i], k->angle[rot]) <= 1);
                }
            }
        }
        // Circle area ~ pi/4 of the square.
        assert(inside > g * g * 3 / 4 && inside < g * g * 4 / 5);
    }

    // Frames keep evolving and parameters stay in range for a long run.
    kal_init(k, 42, 6);
    static uint8_t before[KAL_GRID * KAL_GRID];
    memcpy(before, k->index, sizeof(before));
    for (int f = 0; f < 5000; f++) {
        kal_step(k);
        if (f % 700 == 0) kal_shuffle(k);
        for (int p = 0; p < KAL_PARAM_COUNT; p++) {
            assert(k->params[p].value >= k->params[p].min);
            assert(k->params[p].value <= k->params[p].max);
        }
    }
    assert(memcmp(before, k->index, sizeof(before)) != 0);
    assert(k->fade == 256);
    for (int i = 0; i < KAL_GRID * KAL_GRID; i++) {
        if (k->radius[i] == KAL_OUTSIDE) assert(k->index[i] == 0);
    }

    // A palette change fades in and lands on a different named palette.
    const int old = k->palette_id;
    kal_next_palette(k);
    assert(k->palette_id != old && k->fade == 0);
    for (int f = 0; f < 70; f++) kal_step(k);
    assert(k->fade == 256);
    assert(strcmp(kal_palette_name(k->palette_id), "?") != 0);
    assert(kal_fold_option(-1) == kal_fold_option(kal_fold_option_count() - 1));
}

int main(void) {
    test_rule30_rows();
    test_rule90_is_pascal_mod_2();
    test_mirror_rule_is_mirror_image();
    test_totalistic();
    test_featured_and_random();
    test_alternating_background_is_inverted_on_screen();
    test_pixmap();
    test_kaleidoscope_symmetry();
    puts("test_ca1d: OK");
    return 0;
}
