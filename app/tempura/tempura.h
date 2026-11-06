/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura — a granular canvas instrument for the M-VAVE FM-1.
 * Inspired by the workflow of Beetlecrab Tempera; an independent
 * implementation that shares no code or assets with it.
 *
 * Threads:
 *   audio ISR     tempura_render(), tempura_live_feed()
 *   routing task  tempura_midi_in()  (lock-free queue only)
 *   control task  everything else (UI, cooking parameters, storage)
 */
#ifndef TEMPURA_H
#define TEMPURA_H
#include <stdint.h>
#include <stddef.h>
#include "fm1.h"

#define TEMPURA_NAME    "BEETROOT TEMPURA"
#define TEMPURA_VERSION "1.1"

enum {
    TP_TRACKS = 8, TP_ROWS = 8, TP_EMITTERS = 4, TP_VOICES = 16,
    TP_MAX_GRAINS = 40, TP_SLOTS = 8, TP_SOURCE_LIVE = 8,
    TP_SAMPLE_RATE = 32000, TP_LIVE_RATE = 22050
};

/* ---- Emitter parameters (stored as int16, ranges in tp_emitter_info) ---- */
enum {
    EP_RATE, EP_SIZE, EP_SHAPE, EP_REVERSE,
    EP_POS, EP_SPRAY_Y, EP_SPRAY_X, EP_SCAN,
    EP_OCTAVE, EP_SEMI, EP_FINE, EP_PITCH_SPRAY,
    EP_LEVEL, EP_PAN, EP_PAN_SPRAY, EP_VELO,
    EP_MODE, EP_QUANT, EP_ALIGN_X, EP_GLIDE,
    EP_ATTACK, EP_DECAY, EP_SUSTAIN, EP_RELEASE,
    EP_FILTER, EP_CUTOFF, EP_RES, EP_ENV_FILTER,
    EP_LFO_SHAPE, EP_LFO_RATE, EP_LFO_DEPTH, EP_LFO_DEST,
    EP_SEND_MOD, EP_SEND_DELAY, EP_SEND_REVERB, EP_DRY,
    EP_COUNT
};
/* ---- Track parameters ---- */
enum {
    TR_SOURCE, TR_LEVEL, TR_TUNE, TR_FINE,
    TR_PAN, TR_REVERSE, TR_START, TR_END,
    TR_COUNT
};
/* ---- Global parameters ---- */
enum {
    G_MOD_TYPE, G_MOD_RATE, G_MOD_DEPTH, G_MOD_RETURN,
    G_MOD_FEEDBACK, G_DLY_SYNC, G_BPM, G_DLY_PING,
    G_DLY_TIME, G_DLY_FEEDBACK, G_DLY_TONE, G_DLY_RETURN,
    G_REV_SIZE, G_REV_DAMP, G_REV_WIDTH, G_REV_RETURN,
    G_KEY_MODE, G_KEY_LAYER, G_ROOT, G_VOLUME,
    G_MIDI_MODE, G_CELL_CH, G_KEY_OUT, G_AT_DEST,
    G_LIVE_SRC, G_LIVE_GAIN, G_LIVE_MONITOR, G_LIVE_FREEZE,
    G_COUNT
};

enum { LFO_TO_POS, LFO_TO_SIZE, LFO_TO_RATE, LFO_TO_PITCH,
       LFO_TO_CUTOFF, LFO_TO_PAN, LFO_TO_LEVEL, LFO_TO_SPRAY, LFO_DESTS };
enum { MODE_POLY, MODE_MONO, MODE_LATCH };
enum { KEYS_PLAY, KEYS_CELLS };

struct tp_param_info {
    char name[10];
    int16_t min, max, def;
    uint8_t kind; /* display style, see tempura_format() */
};
enum { PK_NUM, PK_SIGNED, PK_LIST, PK_MS, PK_HZ, PK_PCT, PK_CENTS, PK_SEMI, PK_OCT, PK_BPM, PK_CH };

/* A canvas is the complete instrument state (Tempera "canvas"). */
#define TP_CANVAS_MAGIC 0x54525442U /* "BTRT" */
#define TP_CANVAS_VERSION 1U
struct tp_canvas {
    uint32_t magic;
    uint16_t version, bytes;
    char name[12];
    uint8_t cells[TP_EMITTERS][TP_TRACKS];  /* bit r = emitter placed on row r */
    int16_t ep[TP_EMITTERS][EP_COUNT];
    int16_t tr[TP_TRACKS][TR_COUNT];
    int16_t g[G_COUNT];
};

/* ------------------------------------------------------------------ */
/* Parameter metadata (tempura_params.c)                               */
const struct tp_param_info *tempura_emitter_info(unsigned p);
const struct tp_param_info *tempura_track_info(unsigned p);
const struct tp_param_info *tempura_global_info(unsigned p);
void tempura_format(const struct tp_param_info *info, int value, char out[12]);
const char *tempura_list_name(const struct tp_param_info *info, int value);
void tempura_canvas_default(struct tp_canvas *c);
void tempura_canvas_sanitize(struct tp_canvas *c);

/* ------------------------------------------------------------------ */
/* Engine (tempura_engine.c)                                           */
void tempura_engine_init(void);
/* Control task: allocate optional buffers after Bluetooth/USB start. */
void tempura_engine_allocate(void);
/* Control task: re-derive audio-side values after any parameter edit. */
void tempura_engine_cook(const struct tp_canvas *c);
/* Audio ISR: fill FM1_BLOCK_FRAMES stereo frames of signed Q23. */
FM1_AUDIO_CODE int tempura_render(int32_t *output, unsigned frames);
/* Audio ISR: live input from Bluetooth (int16 stereo) and/or USB (Q23). */
FM1_AUDIO_CODE void tempura_live_feed(const int16_t *bt, const int32_t *usb, unsigned frames);
FM1_AUDIO_CODE unsigned tempura_monitor_gain_q7(void);
/* Audio ISR: measured render time for the adaptive grain limit. */
FM1_AUDIO_CODE void tempura_cpu_report(uint32_t used_ticks, uint32_t block_ticks);
/* Audio start: how to read the time used so far in the current block, and
 * the block period in the same ticks. Lets the engine stop before overrun. */
void tempura_block_clock(uint32_t (*used_ticks)(void), uint32_t block_ticks);
/* Safe mode (HOME held at power-on): render silence, do no other work. */
void tempura_set_safe_mode(unsigned on);
unsigned tempura_safe_mode(void);

/* Bind slot audio (SEA5 mono, 32 kHz). Control task; data stays mapped. */
void tempura_bind_slot(unsigned slot, const uint8_t *data, unsigned bytes, unsigned frames, const char *name);
const char *tempura_slot_name(unsigned slot);
unsigned tempura_slot_frames(unsigned slot);

/* Events from control to audio. note 0..127, velocity 0..127. cell 0..63 or
 * 255 for "all placed cells of this emitter". Returns 0 when queued. */
int tempura_note_on(unsigned emitter, unsigned note, unsigned velocity, unsigned cell, unsigned tag);
int tempura_note_off(unsigned emitter, unsigned note, unsigned cell, unsigned tag);
void tempura_all_off(void);
void tempura_panic(void);
void tempura_set_bend(unsigned emitter, int semitones_q8);
void tempura_set_pressure(unsigned emitter, unsigned value);
void tempura_set_sustain(unsigned emitter, unsigned on);
void tempura_set_bpm_q8(uint32_t bpm_q8);

/* Visualisation snapshot for the UI (control task). */
struct tp_grain_view { uint8_t x, y, emitter, bright; };
unsigned tempura_grain_snapshot(struct tp_grain_view *out, unsigned max);
uint8_t tempura_track_peak(unsigned track, unsigned pixel); /* 0..255 per 1/160 of the track */
unsigned tempura_live_head(void);                             /* 0..159 */
unsigned tempura_voice_mask(void);                            /* active voices per emitter, 4 bits each */
struct tp_stats { uint8_t cpu, grains, grain_limit, voices, limiting, overloads; uint32_t heap_cache, heap_live, heap_delay, heap_reverb, decodes, shed, starved; };
void tempura_stats(struct tp_stats *s);
void tempura_peaks_poll(const struct tp_canvas *c); /* control task: incremental waveform overview */

/* ------------------------------------------------------------------ */
/* MIDI (tempura_midi.c)                                               */
void tempura_midi_in(unsigned source, const uint8_t *m, size_t n); /* routing task */
void tempura_midi_poll(struct tp_canvas *c);                      /* control task */
void tempura_midi_key_out(const uint8_t *m, unsigned n);          /* control task */
unsigned tempura_midi_clock_bpm(void); /* 0 when no external clock */
unsigned tempura_midi_activity(void);

/* ------------------------------------------------------------------ */
/* Storage (tempura_store.c)                                           */
int tempura_store_load(unsigned slot, struct tp_canvas *c);   /* 0 ok, 1 empty, <0 error */
int tempura_store_save(unsigned slot, const struct tp_canvas *c);
int tempura_store_name(unsigned slot, char name[13]);          /* 0 ok, 1 empty */

/* ------------------------------------------------------------------ */
/* UI (tempura_ui.c) */
void tempura_ui_init(void);
void tempura_ui_control(void);
struct tp_canvas *tempura_canvas(void);
unsigned tempura_ui_program_change(unsigned slot);
unsigned tempura_ui_selected(void);
void tempura_ui_touch(void);

/* Shared helpers */
uint32_t tempura_crc32(uint32_t crc, const void *data, unsigned bytes);
void tempura_lock(void);
void tempura_unlock(void);
#endif
