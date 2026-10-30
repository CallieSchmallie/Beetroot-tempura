/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: state shared between the engine and effects units. */
#ifndef TEMPURA_INTERNAL_H
#define TEMPURA_INTERNAL_H
#include "tempura.h"
#include "tempura_dsp.h"

/* Audio-side values derived from the canvas by tempura_engine_cook(). The
 * control task writes single words; the audio ISR reads them each block. */
struct tp_cooked_emitter {
    float rate_hz, size_s, spray_y, spray_x, pos_offset, scan_cells_s;
    float pitch_semis, pitch_spray, reverse_p, level, pan, pan_spray, velo;
    float attack_inc, decay_coef, sustain, release_coef, glide_coef;
    float cutoff_oct, res_q, env_filter_oct;
    float lfo_rate, lfo_depth;
    float send_mod, send_delay, send_reverb, dry;
    uint8_t shape, quant, align, mode, filter, lfo_shape, lfo_dest, placed;
    uint8_t cells[TP_TRACKS];
};
struct tp_cooked_track {
    float level, pan, tune_semis;
    float start, end;            /* fraction of the source */
    uint8_t source, reverse;
};
struct tp_cooked_fx {
    uint8_t mod_type, ping, live_src, freeze;
    float mod_rate, mod_depth, mod_return, mod_feedback;
    float dly_seconds, dly_feedback, dly_tone, dly_return;
    float rev_feedback, rev_damp, rev_width, rev_return;
    float volume, live_gain, monitor;
    uint8_t at_dest;
};
struct tp_cooked {
    struct tp_cooked_emitter e[TP_EMITTERS];
    struct tp_cooked_track t[TP_TRACKS];
    struct tp_cooked_fx fx;
};
extern struct tp_cooked tp_cook;

/* Effects (tempura_fx.c). Buses are float, nominal full scale 1.0. */
void tp_fx_init(void);
void tp_fx_allocate(void);
void tp_fx_heap(uint32_t *delay_bytes, uint32_t *reverb_bytes);
TP_AUDIO void tp_fx_render(float *mod_l, float *mod_r, float *dly_l, float *dly_r,
                           float *rev_l, float *rev_r, float *out_l, float *out_r, unsigned frames);
void tp_fx_clear(void);

/* Allocation that always leaves a reserve for the Bluetooth/USB stacks. */
void *tp_alloc_reserved(size_t bytes);
#endif
