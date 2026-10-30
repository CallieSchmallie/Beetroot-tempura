/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: band renderer for the 240x240 LCD. A frame is drawn as
 * 8-row bands into a small RAM buffer; unchanged bands are not transferred
 * and transfers use the non-blocking DMA path so play never waits on the LCD. */
#ifndef TEMPURA_GFX_H
#define TEMPURA_GFX_H
#include <stdint.h>

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
enum { GFX_W = 240, GFX_H = 240, GFX_BAND = 8, GFX_BANDS = GFX_H / GFX_BAND };

/* Current band being composed: pixels for rows [gfx_top, gfx_top + GFX_BAND). */
extern uint16_t *gfx_px;
extern int gfx_top;

void gfx_fill(int x, int y, int w, int h, uint16_t c);
void gfx_hline(int x, int y, int w, uint16_t c);
void gfx_vline(int x, int y, int h, uint16_t c);
void gfx_rect(int x, int y, int w, int h, uint16_t c);
/* scale 1: 6x8 cell, scale 2: 12x16 cell. Returns the x after the text. */
int gfx_text(int x, int y, const char *s, int scale, uint16_t c);
int gfx_text_width(const char *s, int scale);
uint16_t gfx_mix(uint16_t a, uint16_t b, unsigned t255); /* t=0 -> a, 255 -> b */

/* Frame pump: call every control poll. draw(band) composes one band. */
typedef void (*gfx_draw_fn)(void);
void gfx_init(void);
void gfx_request_frame(void);
int gfx_frame_busy(void);
void gfx_pump(gfx_draw_fn draw, unsigned budget);
#endif
