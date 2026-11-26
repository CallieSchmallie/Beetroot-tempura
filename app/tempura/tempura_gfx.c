/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: LCD band renderer and 5x7 font. */
/* Control-task code: optimise for size (the FM-1 app area is about 444 KB). */
/* Size-optimised: TP_SMALL on each function (the FM-1's clang 4 has no
 * "#pragma clang attribute", which earlier builds relied on). */
#include "tempura_gfx.h"
#include "fm1.h"
#include <string.h>

/* Classic 5x7 font, ASCII 32..95, one byte per column, bit 0 at the top. */
static const uint8_t font[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},
    {0x24,0x2a,0x7f,0x2a,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1c,0x22,0x41,0x00},{0x00,0x41,0x22,0x1c,0x00},{0x08,0x2a,0x1c,0x2a,0x08},{0x08,0x08,0x3e,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3e,0x51,0x49,0x45,0x3e},{0x00,0x42,0x7f,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
    {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},{0x3e,0x41,0x41,0x41,0x22},
    {0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x01,0x01},{0x3e,0x41,0x41,0x51,0x32},
    {0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},
    {0x7f,0x40,0x40,0x40,0x40},{0x7f,0x02,0x04,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
    {0x7f,0x09,0x09,0x09,0x06},{0x3e,0x41,0x51,0x21,0x5e},{0x7f,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7f,0x01,0x01},{0x3f,0x40,0x40,0x40,0x3f},{0x1f,0x20,0x40,0x20,0x1f},{0x7f,0x20,0x18,0x20,0x7f},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7f,0x41,0x41},
    {0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7f,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
};

uint16_t *gfx_px;
int gfx_top;

TP_SMALL void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    int y0 = y < gfx_top ? gfx_top : y, y1 = y + h > gfx_top + GFX_BAND ? gfx_top + GFX_BAND : y + h;
    int x0 = x < 0 ? 0 : x, x1 = x + w > GFX_W ? GFX_W : x + w;
    for (int r = y0; r < y1; ++r) {
        uint16_t *p = gfx_px + (r - gfx_top) * GFX_W;
        for (int k = x0; k < x1; ++k) p[k] = c;
    }
}
TP_SMALL void gfx_hline(int x, int y, int w, uint16_t c) { gfx_fill(x, y, w, 1, c); }
TP_SMALL void gfx_vline(int x, int y, int h, uint16_t c) { gfx_fill(x, y, 1, h, c); }
TP_SMALL void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(x, y, w, c); gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c); gfx_vline(x + w - 1, y, h, c);
}
TP_SMALL uint16_t gfx_mix(uint16_t a, uint16_t b, unsigned t)
{
    unsigned ra = a >> 11, ga = (a >> 5) & 63U, ba = a & 31U;
    unsigned rb = b >> 11, gb = (b >> 5) & 63U, bb = b & 31U;
    unsigned r = (ra * (255U - t) + rb * t) / 255U, g = (ga * (255U - t) + gb * t) / 255U, bl = (ba * (255U - t) + bb * t) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
TP_SMALL void gfx_vgrad(int x, int y, int w, int h, uint16_t top, uint16_t bottom)
{
    int y0 = y < gfx_top ? gfx_top : y, y1 = y + h < gfx_top + GFX_BAND ? y + h : gfx_top + GFX_BAND;
    for (int r = y0; r < y1; ++r) gfx_fill(x, r, w, 1, gfx_mix(top, bottom, h > 1 ? (unsigned)((r - y) * 255 / (h - 1)) : 0U));
}
TP_SMALL void gfx_rfill(int x, int y, int w, int h, int r, uint16_t c)
{
    static const uint8_t inset[7][6] = {{0}, {1}, {2, 1}, {3, 1, 1}, {4, 2, 1, 1}, {5, 3, 2, 1, 1}, {6, 4, 3, 2, 1, 1}};
    if (r > 6) r = 6;
    for (int k = 0; k < h; ++k) {
        int e = k < r ? inset[r][k] : h - 1 - k < r ? inset[r][h - 1 - k] : 0;
        gfx_fill(x + e, y + k, w - 2 * e, 1, c);
    }
}
TP_SMALL void gfx_blend(int x, int y, uint16_t c, unsigned a)
{
    if (y < gfx_top || y >= gfx_top + GFX_BAND || x < 0 || x >= GFX_W) return;
    uint16_t *p = gfx_px + (y - gfx_top) * GFX_W + x;
    *p = gfx_mix(*p, c, a);
}
static const uint8_t *glyph(int ch)
{
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) ch = '?';
    return font[ch - 32];
}
/* Columns a glyph uses; digits keep full width so numbers don't jiggle. */
TP_SMALL static void glyph_span(int ch, const uint8_t *g, int *first, int *count)
{
    int a = 0, b = 4;
    if (!(ch >= '0' && ch <= '9')) { while (a < 5 && !g[a]) ++a; while (b >= 0 && !g[b]) --b; }
    if (a > b) { *first = 0; *count = 2; return; }      /* space */
    *first = a; *count = b - a + 1;
}
TP_SMALL static int src_px(const uint8_t *g, int c, int r) { return c >= 0 && c < 5 && r >= 0 && r < 7 && (g[c] >> r) & 1U; }
TP_SMALL int gfx_label_width(const char *s)
{
    int w = 0;
    for (; *s; ++s) { int f, n; glyph_span((unsigned char)*s, glyph((unsigned char)*s), &f, &n); w += n + 1; }
    return w ? w - 1 : 0;
}
TP_SMALL int gfx_label(int x, int y, const char *s, uint16_t c)
{
    if (y + 8 <= gfx_top || y >= gfx_top + GFX_BAND) return x + gfx_label_width(s);
    for (; *s; ++s) {
        const uint8_t *g = glyph((unsigned char)*s);
        int f, n; glyph_span((unsigned char)*s, g, &f, &n);
        for (int col = 0; col < n; ++col)
            for (int row = 0; row < 7; ++row) if (src_px(g, f + col, row)) gfx_fill(x + col, y + row, 1, 1, c);
        x += n + 1;
    }
    return x - 1;
}
TP_SMALL int gfx_title_width(const char *s, unsigned style)
{
    int w = 0;
    for (; *s; ++s) {
        int f = 0, n = 5;
        if (!(style & GFX_MONO)) glyph_span((unsigned char)*s, glyph((unsigned char)*s), &f, &n);
        w += 2 * n + 2 + (style & GFX_BOLD ? 1 : 0);
    }
    return w ? w - 2 + (style & GFX_ITALIC ? 3 : 0) : 0;
}
/* Doubles a glyph with EPX smoothing: diagonal steps become slopes and
 * square corners round off, so large text reads as a drawn letterform. */
TP_SMALL int gfx_title(int x, int y, const char *s, uint16_t top, uint16_t bottom, unsigned style)
{
    if (y + 16 <= gfx_top || y >= gfx_top + GFX_BAND) return x + gfx_title_width(s, style);
    for (; *s; ++s) {
        const uint8_t *g = glyph((unsigned char)*s);
        int f = 0, n = 5;
        if (!(style & GFX_MONO)) glyph_span((unsigned char)*s, g, &f, &n);
        for (int pass = style & GFX_SHADOW ? 0 : 1; pass < 2; ++pass)
            for (int r = 0; r < 14; ++r) {
                int sr = r >> 1, dy = r & 1, ox = x + (style & GFX_ITALIC ? (13 - r) / 4 : 0);
                if (y + r + (pass ? 0 : 2) < gfx_top || y + r >= gfx_top + GFX_BAND) continue;
                uint16_t c = pass ? gfx_mix(top, bottom, (unsigned)r * 255U / 13U) : 0;
                for (int k = 0; k < 2 * n; ++k) {
                    int sc = f + (k >> 1), dx = k & 1;
                    int P = src_px(g, sc, sr), A = src_px(g, sc, sr - 1), B = src_px(g, sc + 1, sr);
                    int C = src_px(g, sc - 1, sr), D = src_px(g, sc, sr + 1), on = P;
                    if (!dy && !dx && C == A && C != D && A != B) on = A;
                    else if (!dy && dx && A == B && A != C && B != D) on = B;
                    else if (dy && !dx && D == C && D != B && C != A) on = C;
                    else if (dy && dx && B == D && B != A && D != C) on = D;
                    if (!on) continue;
                    int px = ox + k + (pass ? 0 : 1), py = y + r + (pass ? 0 : 2);
                    gfx_fill(px, py, style & GFX_BOLD ? 2 : 1, 1, c);
                }
            }
        x += 2 * n + 2 + (style & GFX_BOLD ? 1 : 0);
    }
    return x;
}
TP_SMALL int gfx_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
TP_SMALL int gfx_text(int x, int y, const char *s, int scale, uint16_t c)
{
    if (y + 8 * scale <= gfx_top || y >= gfx_top + GFX_BAND) return x + gfx_text_width(s, scale);
    if (scale == 2) { char one[2] = {0, 0}; for (; *s; ++s, x += 12) { one[0] = *s; gfx_title(x, y, one, c, c, GFX_MONO); } return x; }
    for (; *s; ++s, x += 6) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) ch = '?';
        const uint8_t *g = font[ch - 32];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1U << row)) {
                    int py = y + row;
                    if (py >= gfx_top && py < gfx_top + GFX_BAND && x + col >= 0 && x + col < GFX_W)
                        gfx_px[(py - gfx_top) * GFX_W + x + col] = c;
                }
    }
    return x;
}

/* ---- frame pump ---- */
static uint16_t buffers[2][GFX_W * GFX_BAND] __attribute__((aligned(32)));
static int ready_band[2] = {-1, -1};
static int sending = -1;
static unsigned next_band = GFX_BANDS, frame_active;
static uint32_t band_sum[GFX_BANDS];

TP_SMALL void gfx_init(void)
{
    fm1_screen_init();
    memset(band_sum, 0xa5, sizeof band_sum);
    ready_band[0] = ready_band[1] = -1;
    sending = -1;
    next_band = GFX_BANDS; frame_active = 0;
}
TP_SMALL void gfx_request_frame(void)
{
    if (frame_active) return;
    frame_active = 1;
    next_band = 0;
}
TP_SMALL int gfx_frame_busy(void) { return frame_active; }

TP_SMALL static uint32_t checksum(const uint16_t *p)
{
    uint32_t h = 2166136261U;
    for (unsigned i = 0; i < GFX_W * GFX_BAND; i += 2) h = (h ^ (p[i] | (uint32_t)p[i + 1] << 16)) * 16777619U;
    return h;
}
TP_SMALL void gfx_pump(gfx_draw_fn draw, unsigned budget)
{
    if (sending >= 0) {
        int pending = fm1_screen_pending();
        if (pending == 0 || pending < 0) sending = -1;
    }
    for (unsigned guard = 0; guard < 2U * GFX_BANDS; ++guard) {
        int ready = ready_band[0] >= 0 ? 0 : ready_band[1] >= 0 ? 1 : -1;
        if (ready >= 0 && sending < 0) {
            int r = fm1_screen_submit(0, (unsigned)ready_band[ready] * GFX_BAND, GFX_W, GFX_BAND,
                                      buffers[ready], GFX_W * GFX_BAND);
            if (r == 1) break;
            if (r == 0) sending = ready;
            else band_sum[ready_band[ready]] ^= 1U; /* force a retry next frame */
            ready_band[ready] = -1;
            if (fm1_screen_pending() <= 0) sending = -1;
            continue;
        }
        if (!frame_active || next_band >= GFX_BANDS || !budget) break;
        int free_buf = -1;
        for (int b = 0; b < 2; ++b) if (b != sending && ready_band[b] < 0) { free_buf = b; break; }
        if (free_buf < 0) break;
        --budget;
        gfx_px = buffers[free_buf];
        gfx_top = (int)(next_band * GFX_BAND);
        draw();
        uint32_t sum = checksum(gfx_px);
        if (sum != band_sum[next_band]) { band_sum[next_band] = sum; ready_band[free_buf] = (int)next_band; }
        ++next_band;
    }
    if (frame_active && next_band >= GFX_BANDS && ready_band[0] < 0 && ready_band[1] < 0) frame_active = 0;
}

