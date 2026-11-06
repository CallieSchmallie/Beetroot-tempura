/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: modulation (chorus / flanger / vibrato), tempo delay and
 * reverb. All buffers are static (see tempura_engine.c for why). */
#include "tempura_internal.h"
#include <stdlib.h>
#include <string.h>

/* ---- modulation: one mono line, two quadrature taps ---- */
enum { MOD_LEN = 2048, MOD_MASK = MOD_LEN - 1 };
static float mod_line[MOD_LEN];
static unsigned mod_write;
static float mod_phase, mod_fb_l, mod_fb_r;

/* ---- delay: stereo at half rate (22.05 kHz), int16 storage, 0.74 s ---- */
enum { DLY_LEN = 16384, DLY_MASK = DLY_LEN - 1 };
static int16_t dly[DLY_LEN * 2];   /* interleaved L/R */
static uint32_t dly_write;
static float dly_time, dly_lp_l, dly_lp_r, dly_prev_l, dly_prev_r;

/* ---- reverb: Freeverb-style, 4 combs + 2 allpasses per side ---- */
enum { COMBS = 4, ALLPASSES = 2, SPREAD = 23 };
static const uint16_t comb_len[COMBS] = {1116, 1277, 1422, 1557};
static const uint16_t ap_len[ALLPASSES] = {556, 441};
enum { REV_SAMPLES = 2 * (1116 + 1277 + 1422 + 1557 + 556 + 441) + (COMBS + ALLPASSES) * SPREAD };
static int16_t rev_mem[REV_SAMPLES];
static struct { int16_t *buf; uint16_t len, pos; float store; } comb[2][COMBS];
static struct { int16_t *buf; uint16_t len, pos; } ap[2][ALLPASSES];

void tp_fx_init(void)
{
    memset(mod_line, 0, sizeof mod_line);
    mod_write = 0; mod_phase = 0;
    int16_t *p = rev_mem;
    for (unsigned s = 0; s < 2; ++s) {
        for (unsigned i = 0; i < COMBS; ++i) {
            comb[s][i].buf = p; comb[s][i].len = (uint16_t)(comb_len[i] + s * SPREAD);
            comb[s][i].pos = 0; comb[s][i].store = 0; p += comb[s][i].len;
        }
        for (unsigned i = 0; i < ALLPASSES; ++i) {
            ap[s][i].buf = p; ap[s][i].len = (uint16_t)(ap_len[i] + s * SPREAD);
            ap[s][i].pos = 0; p += ap[s][i].len;
        }
    }
}
void tp_fx_allocate(void) {}
void tp_fx_heap(uint32_t *delay_bytes, uint32_t *reverb_bytes)
{
    *delay_bytes = (uint32_t)sizeof dly;
    *reverb_bytes = (uint32_t)sizeof rev_mem;
}
TP_AUDIO void tp_fx_clear(void)
{
    for (unsigned i = 0; i < MOD_LEN; ++i) mod_line[i] = 0.0f;
    for (uint32_t i = 0; i < DLY_LEN * 2U; ++i) dly[i] = 0;
        for (unsigned s = 0; s < 2; ++s) {
            for (unsigned i = 0; i < COMBS; ++i) { for (unsigned k = 0; k < comb[s][i].len; ++k) comb[s][i].buf[k] = 0; comb[s][i].store = 0; }
            for (unsigned i = 0; i < ALLPASSES; ++i) for (unsigned k = 0; k < ap[s][i].len; ++k) ap[s][i].buf[k] = 0;
        }
    mod_fb_l = mod_fb_r = dly_lp_l = dly_lp_r = dly_prev_l = dly_prev_r = 0;
}

/* Reverb storage scale: float 1.0 == 8192 so tails have 4x headroom. */
#define RS 8192.0f
#define RI (1.0f / 8192.0f)
/* Feedback paths saturate smoothly below the int16 storage limit (4.0),
 * so a redlined send grows warm instead of hard-clipping into crackle. */
TP_INLINE float fb_sat(float x) { return 3.5f * tp_soft(x * (1.0f / 3.5f)); }

static TP_AUDIO void modulation(const float *in_l, const float *in_r, float *out_l, float *out_r, unsigned n)
{
    const struct tp_cooked_fx *f = &tp_cook.fx;
    if (f->mod_return <= 0.0f) return;
    float base, depth;
    if (f->mod_type == 1) { base = 0.0015f; depth = 0.004f; }        /* flanger */
    else if (f->mod_type == 2) { base = 0.004f; depth = 0.0035f; }   /* vibrato */
    else { base = 0.012f; depth = 0.008f; }                          /* chorus */
    depth *= f->mod_depth;
    float inc = f->mod_rate * (1.0f / 44100.0f);
    float fb = f->mod_type == 1 ? f->mod_feedback : f->mod_feedback * 0.5f;
    float ret = f->mod_return;
    for (unsigned i = 0; i < n; ++i) {
        float x = (in_l[i] + in_r[i]) * 0.5f;
        mod_line[mod_write & MOD_MASK] = fb_sat(x + (mod_fb_l + mod_fb_r) * 0.5f * fb);
        mod_phase += inc;
        if (mod_phase >= 1.0f) mod_phase -= 1.0f;
        float dl = (base + depth * (0.5f + 0.5f * tp_sin1(mod_phase))) * 44100.0f;
        float dr = (base + depth * (0.5f + 0.5f * tp_cos1(mod_phase))) * 44100.0f;
        int il = (int)dl, ir = (int)dr;
        float fl = dl - (float)il, fr = dr - (float)ir;
        float a0 = mod_line[(mod_write - (unsigned)il) & MOD_MASK], a1 = mod_line[(mod_write - (unsigned)il - 1U) & MOD_MASK];
        float b0 = mod_line[(mod_write - (unsigned)ir) & MOD_MASK], b1 = mod_line[(mod_write - (unsigned)ir - 1U) & MOD_MASK];
        float yl = a0 + (a1 - a0) * fl, yr = b0 + (b1 - b0) * fr;
        mod_fb_l = yl; mod_fb_r = yr;
        ++mod_write;
        out_l[i] += yl * ret;
        out_r[i] += (f->mod_type == 2 ? yl : yr) * ret;
    }
    mod_fb_l = tp_flush(mod_fb_l); mod_fb_r = tp_flush(mod_fb_r);
}

static TP_AUDIO void delay(const float *in_l, const float *in_r, float *out_l, float *out_r, unsigned n)
{
    const struct tp_cooked_fx *f = &tp_cook.fx;
    if (f->dly_return <= 0.0f) return;
    float target = tp_clampf(f->dly_seconds * 22050.0f, 4.0f, (float)(DLY_LEN - 4U));
    float fb = f->dly_feedback, ret = f->dly_return;
    float lp = 1.0f - tp_exp(-2.0f * TP_PI * f->dly_tone * (1.0f / 22050.0f));
    for (unsigned i = 0; i + 1 < n; i += 2) {
        /* Tape-like glide towards a new delay time. */
        dly_time += (target - dly_time) * 0.0015f;
        int it = (int)dly_time;
        float fr = dly_time - (float)it;
        uint32_t r0 = (dly_write - (uint32_t)it) & DLY_MASK, r1 = (r0 - 1U) & DLY_MASK;
        float yl = ((float)dly[2 * r0] + ((float)dly[2 * r1] - (float)dly[2 * r0]) * fr) * RI;
        float yr = ((float)dly[2 * r0 + 1] + ((float)dly[2 * r1 + 1] - (float)dly[2 * r0 + 1]) * fr) * RI;
        dly_lp_l += (yl - dly_lp_l) * lp;
        dly_lp_r += (yr - dly_lp_r) * lp;
        float xl = (in_l[i] + in_l[i + 1]) * 0.5f, xr = (in_r[i] + in_r[i + 1]) * 0.5f;
        float wl, wr;
        if (f->ping) { wl = (xl + xr) * 0.5f + dly_lp_r * fb; wr = dly_lp_l * fb; }
        else { wl = xl + dly_lp_l * fb; wr = xr + dly_lp_r * fb; }
        dly[2 * dly_write] = tp_sat16(fb_sat(wl) * RS);
        dly[2 * dly_write + 1] = tp_sat16(fb_sat(wr) * RS);
        dly_write = (dly_write + 1U) & DLY_MASK;
        out_l[i] += (dly_prev_l + yl) * 0.5f * ret; out_l[i + 1] += yl * ret;
        out_r[i] += (dly_prev_r + yr) * 0.5f * ret; out_r[i + 1] += yr * ret;
        dly_prev_l = yl; dly_prev_r = yr;
    }
    dly_lp_l = tp_flush(dly_lp_l); dly_lp_r = tp_flush(dly_lp_r);
}

static TP_AUDIO void reverb(const float *in_l, const float *in_r, float *out_l, float *out_r, unsigned n)
{
    const struct tp_cooked_fx *f = &tp_cook.fx;
    if (f->rev_return <= 0.0f) return;
    float feedback = f->rev_feedback, damp = f->rev_damp, keep = 1.0f - damp;
    float wet1 = f->rev_return * (0.5f + f->rev_width * 0.5f), wet2 = f->rev_return * (0.5f - f->rev_width * 0.5f);
    for (unsigned i = 0; i < n; ++i) {
        float input = (in_l[i] + in_r[i]) * 0.03f;
        float side[2];
        for (unsigned s = 0; s < 2; ++s) {
            float acc = 0.0f;
            for (unsigned c = 0; c < COMBS; ++c) {
                int16_t *b = comb[s][c].buf;
                uint16_t p = comb[s][c].pos;
                float y = (float)b[p] * RI;
                comb[s][c].store = y * keep + comb[s][c].store * damp;
                b[p] = tp_sat16(fb_sat(input + comb[s][c].store * feedback) * RS);
                if (++p >= comb[s][c].len) p = 0;
                comb[s][c].pos = p;
                acc += y;
            }
            for (unsigned a = 0; a < ALLPASSES; ++a) {
                int16_t *b = ap[s][a].buf;
                uint16_t p = ap[s][a].pos;
                float bo = (float)b[p] * RI;
                b[p] = tp_sat16(fb_sat(acc + bo * 0.5f) * RS);
                acc = bo - acc;
                if (++p >= ap[s][a].len) p = 0;
                ap[s][a].pos = p;
            }
            side[s] = acc;
        }
        out_l[i] += side[0] * wet1 + side[1] * wet2;
        out_r[i] += side[1] * wet1 + side[0] * wet2;
    }
    for (unsigned s = 0; s < 2; ++s)
        for (unsigned c = 0; c < COMBS; ++c) comb[s][c].store = tp_flush(comb[s][c].store);
}

TP_AUDIO void tp_fx_render(float *mod_l, float *mod_r, float *dly_l, float *dly_r,
                           float *rev_l, float *rev_r, float *out_l, float *out_r, unsigned frames)
{
    /* Parallel sends, as on Tempera: each emitter feeds each effect directly. */
    modulation(mod_l, mod_r, out_l, out_r, frames);
    delay(dly_l, dly_r, out_l, out_r, frames);
    reverb(rev_l, rev_r, out_l, out_r, frames);
}
