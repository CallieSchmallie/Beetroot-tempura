/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: small DSP helpers. Everything here is inline so it is
 * emitted into the RAM-resident audio functions that call it (no libm). */
#ifndef TEMPURA_DSP_H
#define TEMPURA_DSP_H
#include <stdint.h>
#include <string.h>
#include "fm1.h"

#define TP_AUDIO FM1_AUDIO_CODE
#define TP_INLINE static inline __attribute__((always_inline))
#define TP_PI 3.14159265f

#ifdef FM1_HOST_TEST
#include <stdatomic.h>
#define TP_BARRIER() atomic_thread_fence(memory_order_seq_cst)
#else
#define TP_BARRIER() __asm__ volatile("csync" ::: "memory")
#endif

TP_INLINE float tp_minf(float a, float b) { return a < b ? a : b; }
TP_INLINE float tp_maxf(float a, float b) { return a > b ? a : b; }
TP_INLINE float tp_clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
TP_INLINE int tp_floori(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }

/* 2^x, |x| < 100: integer part in the exponent, polynomial for the rest. */
TP_INLINE float tp_exp2(float x)
{
    x = tp_clampf(x, -100.0f, 100.0f);
    int n = tp_floori(x + 0.5f);
    float a = (x - (float)n) * 0.693147181f;
    float p = 1.0f + a * (1.0f + a * (0.5f + a * (0.166666667f + a * (0.041666667f + a * 0.008333333f))));
    uint32_t bits = (uint32_t)(n + 127) << 23;
    float s; memcpy(&s, &bits, 4);
    return p * s;
}
TP_INLINE float tp_exp(float x) { return tp_exp2(x * 1.442695041f); }
/* sin(2*pi*x) for any x (period 1). */
TP_INLINE float tp_sin1(float x)
{
    x -= (float)tp_floori(x);           /* 0..1 */
    float a = x < 0.5f ? x : x - 1.0f;  /* -0.5..0.5 */
    a *= 2.0f * TP_PI;                  /* -pi..pi */
    if (a > TP_PI * 0.5f) a = TP_PI - a;
    if (a < -TP_PI * 0.5f) a = -TP_PI - a;
    float q = a * a;
    return a * (1.0f + q * (-0.166666667f + q * (0.008333333f + q * (-0.000198413f + q * 0.0000027557f))));
}
TP_INLINE float tp_cos1(float x) { return tp_sin1(x + 0.25f); }
/* Pseudo random. */
TP_INLINE uint32_t tp_rand(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
TP_INLINE float tp_rand01(uint32_t *s) { return (float)(tp_rand(s) >> 8) * (1.0f / 16777216.0f); }
TP_INLINE float tp_randbi(uint32_t *s) { return tp_rand01(s) * 2.0f - 1.0f; }

/* Topology-preserving state variable filter (per-block coefficients). */
struct tp_svf { float ic1, ic2; };
struct tp_svf_coef { float g, k, a1, a2, a3; };
TP_INLINE void tp_svf_design(struct tp_svf_coef *c, float hz, float q)
{
    hz = tp_clampf(hz, 16.0f, 18000.0f);
    float w = hz * (1.0f / 44100.0f);
    float g = tp_sin1(w * 0.5f) / tp_cos1(w * 0.5f);   /* tan(pi*f/fs) */
    c->g = g; c->k = 1.0f / tp_maxf(q, 0.5f);
    c->a1 = 1.0f / (1.0f + g * (g + c->k));
    c->a2 = g * c->a1;
    c->a3 = g * c->a2;
}
/* mode: 1 low, 2 band, 3 high */
TP_INLINE float tp_svf_run(struct tp_svf *s, const struct tp_svf_coef *c, float v0, unsigned mode)
{
    float v3 = v0 - s->ic2;
    float v1 = c->a1 * s->ic1 + c->a2 * v3;
    float v2 = s->ic2 + c->a2 * s->ic1 + c->a3 * v3;
    s->ic1 = 2.0f * v1 - s->ic1;
    s->ic2 = 2.0f * v2 - s->ic2;
    if (mode == 1) return v2;
    if (mode == 2) return v1;
    return v0 - c->k * v1 - v2;
}
TP_INLINE int16_t tp_sat16(float x)
{
    if (x > 32767.0f) return 32767;
    if (x < -32768.0f) return -32768;
    return (int16_t)x;
}
#endif
