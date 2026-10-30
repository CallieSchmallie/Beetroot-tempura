/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: LCD band renderer and 5x7 font. */
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

void gfx_fill(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) return;
    int y0 = y < gfx_top ? gfx_top : y, y1 = y + h > gfx_top + GFX_BAND ? gfx_top + GFX_BAND : y + h;
    int x0 = x < 0 ? 0 : x, x1 = x + w > GFX_W ? GFX_W : x + w;
    for (int r = y0; r < y1; ++r) {
        uint16_t *p = gfx_px + (r - gfx_top) * GFX_W;
        for (int k = x0; k < x1; ++k) p[k] = c;
    }
}
void gfx_hline(int x, int y, int w, uint16_t c) { gfx_fill(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, uint16_t c) { gfx_fill(x, y, 1, h, c); }
void gfx_rect(int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(x, y, w, c); gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c); gfx_vline(x + w - 1, y, h, c);
}
uint16_t gfx_mix(uint16_t a, uint16_t b, unsigned t)
{
    unsigned ra = a >> 11, ga = (a >> 5) & 63U, ba = a & 31U;
    unsigned rb = b >> 11, gb = (b >> 5) & 63U, bb = b & 31U;
    unsigned r = (ra * (255U - t) + rb * t) / 255U, g = (ga * (255U - t) + gb * t) / 255U, bl = (ba * (255U - t) + bb * t) / 255U;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
int gfx_text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }
int gfx_text(int x, int y, const char *s, int scale, uint16_t c)
{
    if (y + 8 * scale <= gfx_top || y >= gfx_top + GFX_BAND) return x + gfx_text_width(s, scale);
    for (; *s; ++s, x += 6 * scale) {
        int ch = (unsigned char)*s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 95) ch = '?';
        const uint8_t *g = font[ch - 32];
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (g[col] & (1U << row)) {
                    if (scale == 1) {
                        int py = y + row;
                        if (py >= gfx_top && py < gfx_top + GFX_BAND && x + col >= 0 && x + col < GFX_W)
                            gfx_px[(py - gfx_top) * GFX_W + x + col] = c;
                    } else gfx_fill(x + col * scale, y + row * scale, scale, scale, c);
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

void gfx_init(void)
{
    fm1_screen_init();
    memset(band_sum, 0xa5, sizeof band_sum);
    ready_band[0] = ready_band[1] = -1;
    sending = -1;
    next_band = GFX_BANDS; frame_active = 0;
}
void gfx_request_frame(void)
{
    if (frame_active) return;
    frame_active = 1;
    next_band = 0;
}
int gfx_frame_busy(void) { return frame_active; }

static uint32_t checksum(const uint16_t *p)
{
    uint32_t h = 2166136261U;
    for (unsigned i = 0; i < GFX_W * GFX_BAND; i += 2) h = (h ^ (p[i] | (uint32_t)p[i + 1] << 16)) * 16777619U;
    return h;
}
void gfx_pump(gfx_draw_fn draw, unsigned budget)
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
