/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: parameter metadata, formatting and the default canvas. */
/* Control-task code: optimise for size (the FM-1 app area is about 444 KB). */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)
#include "tempura.h"
#include <string.h>

static const struct tp_param_info emitter_info[EP_COUNT] = {
    [EP_RATE]        = {"RATE",     0, 127,  60, PK_HZ},
    [EP_SIZE]        = {"SIZE",     0, 127,  72, PK_MS},
    [EP_SHAPE]       = {"SHAPE",    0,   5,   0, PK_LIST},
    [EP_REVERSE]     = {"REVERSE",  0, 100,   0, PK_PCT},
    [EP_POS]         = {"POSITION",-64,  63,   0, PK_SIGNED},
    [EP_SPRAY_Y]     = {"SPRAY Y",  0, 127,  24, PK_NUM},
    [EP_SPRAY_X]     = {"SPRAY X",  0, 127,   0, PK_NUM},
    [EP_SCAN]        = {"SCAN",   -64,  63,   0, PK_SIGNED},
    [EP_OCTAVE]      = {"OCTAVE",  -3,   3,   0, PK_OCT},
    [EP_SEMI]        = {"SEMI",   -12,  12,   0, PK_SEMI},
    [EP_FINE]        = {"FINE",   -50,  50,   0, PK_CENTS},
    [EP_PITCH_SPRAY] = {"P.SPRAY",  0, 127,   0, PK_NUM},
    [EP_LEVEL]       = {"LEVEL",    0, 127, 100, PK_NUM},
    [EP_PAN]         = {"PAN",    -64,  63,   0, PK_SIGNED},
    [EP_PAN_SPRAY]   = {"PAN SPRY", 0, 127,  48, PK_NUM},
    [EP_VELO]        = {"VELOCITY", 0, 127,  64, PK_NUM},
    [EP_MODE]        = {"MODE",     0,   2,   0, PK_LIST},
    [EP_QUANT]       = {"QUANTIZE", 0,   3,   0, PK_LIST},
    [EP_ALIGN_X]     = {"ALIGN X",  0,   1,   1, PK_LIST},
    [EP_GLIDE]       = {"GLIDE",    0, 127,   0, PK_NUM},
    [EP_ATTACK]      = {"ATTACK",   0, 127,  20, PK_NUM},
    [EP_DECAY]       = {"DECAY",    0, 127,  60, PK_NUM},
    [EP_SUSTAIN]     = {"SUSTAIN",  0, 127, 110, PK_NUM},
    [EP_RELEASE]     = {"RELEASE",  0, 127,  70, PK_NUM},
    [EP_FILTER]      = {"FILTER",   0,   3,   0, PK_LIST},
    [EP_CUTOFF]      = {"CUTOFF",   0, 127, 100, PK_NUM},
    [EP_RES]         = {"RESO",     0, 127,  20, PK_NUM},
    [EP_ENV_FILTER]  = {"ENV>CUT", -64,  63,   0, PK_SIGNED},
    [EP_LFO_SHAPE]   = {"LFO WAVE", 0,   5,   0, PK_LIST},
    [EP_LFO_RATE]    = {"LFO RATE", 0, 127,  40, PK_NUM},
    [EP_LFO_DEPTH]   = {"LFO AMT",  0, 127,   0, PK_NUM},
    [EP_LFO_DEST]    = {"LFO DEST", 0, LFO_DESTS - 1, 0, PK_LIST},
    [EP_SEND_MOD]    = {"MOD SEND", 0, 127,   0, PK_NUM},
    [EP_SEND_DELAY]  = {"DLY SEND", 0, 127,  20, PK_NUM},
    [EP_SEND_REVERB] = {"REV SEND", 0, 127,  40, PK_NUM},
    [EP_DRY]         = {"DRY",      0, 127, 127, PK_NUM},
};
static const struct tp_param_info track_info[TR_COUNT] = {
    [TR_SOURCE]  = {"SOURCE",   0, TP_SOURCE_LIVE, 0, PK_LIST},
    [TR_LEVEL]   = {"LEVEL",    0, 127, 110, PK_NUM},
    [TR_TUNE]    = {"TUNE",   -24,  24,   0, PK_SEMI},
    [TR_FINE]    = {"FINE",   -50,  50,   0, PK_CENTS},
    [TR_PAN]     = {"PAN",    -64,  63,   0, PK_SIGNED},
    [TR_REVERSE] = {"REVERSE",  0,   1,   0, PK_LIST},
    [TR_START]   = {"START",    0, 126,   0, PK_PCT},
    [TR_END]     = {"END",      1, 127, 127, PK_PCT},
};
static const struct tp_param_info global_info[G_COUNT] = {
    [G_MOD_TYPE]     = {"MOD TYPE", 0,   2,   0, PK_LIST},
    [G_MOD_RATE]     = {"MOD RATE", 0, 127,  30, PK_NUM},
    [G_MOD_DEPTH]    = {"DEPTH",    0, 127,  50, PK_NUM},
    [G_MOD_RETURN]   = {"RETURN",   0, 127,  90, PK_NUM},
    [G_MOD_FEEDBACK] = {"FEEDBACK", 0, 127,  30, PK_NUM},
    [G_DLY_SYNC]     = {"DLY SYNC", 0,   8,   0, PK_LIST},
    [G_BPM]          = {"TEMPO",   40, 240, 110, PK_BPM},
    [G_DLY_PING]     = {"PINGPONG", 0,   1,   1, PK_LIST},
    [G_DLY_TIME]     = {"DLY TIME", 0, 127,  80, PK_MS},
    [G_DLY_FEEDBACK] = {"FEEDBACK", 0, 127,  55, PK_NUM},
    [G_DLY_TONE]     = {"TONE",     0, 127,  80, PK_NUM},
    [G_DLY_RETURN]   = {"RETURN",   0, 127,  90, PK_NUM},
    [G_REV_SIZE]     = {"SIZE",     0, 127,  90, PK_NUM},
    [G_REV_DAMP]     = {"DAMPING",  0, 127,  50, PK_NUM},
    [G_REV_WIDTH]    = {"WIDTH",    0, 127, 110, PK_NUM},
    [G_REV_RETURN]   = {"RETURN",   0, 127,  90, PK_NUM},
    [G_KEY_MODE]     = {"KEYS",     0,   1,   0, PK_LIST},
    [G_KEY_LAYER]    = {"LAYER",    0,   1,   0, PK_LIST},
    [G_ROOT]         = {"TRANSPOS",-12,  12,   0, PK_SEMI},
    [G_VOLUME]       = {"VOLUME",   0, 127, 100, PK_NUM},
    [G_MIDI_MODE]    = {"MIDI IN",  0,   1,   0, PK_LIST},
    [G_CELL_CH]      = {"CELL CH",  0,  16,  16, PK_CH},
    [G_KEY_OUT]      = {"KEY OUT",  0,   3,   3, PK_LIST},
    [G_AT_DEST]      = {"PRESSURE", 0,   5,   1, PK_LIST},
    [G_LIVE_SRC]     = {"LIVE SRC", 0,   2,   0, PK_LIST},
    [G_LIVE_GAIN]    = {"IN GAIN",  0, 127,  90, PK_NUM},
    [G_LIVE_MONITOR] = {"MONITOR",  0, 127,  64, PK_NUM},
    [G_LIVE_FREEZE]  = {"FREEZE",   0,   1,   0, PK_LIST},
};

const struct tp_param_info *tempura_emitter_info(unsigned p) { return p < EP_COUNT ? &emitter_info[p] : NULL; }
const struct tp_param_info *tempura_track_info(unsigned p) { return p < TR_COUNT ? &track_info[p] : NULL; }
const struct tp_param_info *tempura_global_info(unsigned p) { return p < G_COUNT ? &global_info[p] : NULL; }

static const char *const shape_names[] = {"HANN", "TRIANGL", "TUKEY", "PERC", "SWELL", "GAUSS"};
static const char *const mode_names[] = {"POLY", "MONO", "LATCH"};
static const char *const quant_names[] = {"OFF", "SEMI", "OCTAVE", "OCT+5TH"};
static const char *const align_names[] = {"BLEND", "SNAP"};
static const char *const filter_names[] = {"OFF", "LOWPASS", "BANDPASS", "HIGHPASS"};
static const char *const lfo_shape_names[] = {"SINE", "TRIANGL", "SAW", "SQUARE", "S+H", "DRIFT"};
static const char *const lfo_dest_names[] = {"POSITION", "SIZE", "RATE", "PITCH", "CUTOFF", "PAN", "LEVEL", "SPRAY"};
static const char *const source_names[] = {"SLOT 1", "SLOT 2", "SLOT 3", "SLOT 4", "SLOT 5", "SLOT 6", "SLOT 7", "SLOT 8", "LIVE IN"};
static const char *const onoff_names[] = {"OFF", "ON"};
static const char *const mod_type_names[] = {"CHORUS", "FLANGER", "VIBRATO"};
static const char *const sync_names[] = {"FREE", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/4D", "1/2", "1 BAR"};
static const char *const keys_names[] = {"PLAY", "CELLS"};
static const char *const layer_names[] = {"SELECTED", "ALL"};
static const char *const midi_mode_names[] = {"OMNI", "CH 1-4"};
static const char *const key_out_names[] = {"OFF", "USB", "BLE", "USB+BLE"};
static const char *const at_names[] = {"OFF", "CUTOFF", "LEVEL", "SIZE", "SPRAY", "LFO AMT"};
static const char *const live_names[] = {"BT AUDIO", "USB", "BT+USB"};

const char *tempura_list_name(const struct tp_param_info *info, int v)
{
#define PICK(table) return (unsigned)v < sizeof table / sizeof table[0] ? table[v] : "?"
    if (info == &emitter_info[EP_SHAPE]) PICK(shape_names);
    if (info == &emitter_info[EP_MODE]) PICK(mode_names);
    if (info == &emitter_info[EP_QUANT]) PICK(quant_names);
    if (info == &emitter_info[EP_ALIGN_X]) PICK(align_names);
    if (info == &emitter_info[EP_FILTER]) PICK(filter_names);
    if (info == &emitter_info[EP_LFO_SHAPE]) PICK(lfo_shape_names);
    if (info == &emitter_info[EP_LFO_DEST]) PICK(lfo_dest_names);
    if (info == &track_info[TR_SOURCE]) PICK(source_names);
    if (info == &track_info[TR_REVERSE]) PICK(onoff_names);
    if (info == &global_info[G_MOD_TYPE]) PICK(mod_type_names);
    if (info == &global_info[G_DLY_SYNC]) PICK(sync_names);
    if (info == &global_info[G_DLY_PING]) PICK(onoff_names);
    if (info == &global_info[G_KEY_MODE]) PICK(keys_names);
    if (info == &global_info[G_KEY_LAYER]) PICK(layer_names);
    if (info == &global_info[G_MIDI_MODE]) PICK(midi_mode_names);
    if (info == &global_info[G_KEY_OUT]) PICK(key_out_names);
    if (info == &global_info[G_AT_DEST]) PICK(at_names);
    if (info == &global_info[G_LIVE_SRC]) PICK(live_names);
    if (info == &global_info[G_LIVE_FREEZE]) PICK(onoff_names);
#undef PICK
    return "?";
}

static void put_int(char *out, int v, int sign)
{
    char tmp[12];
    unsigned n = 0, u = (unsigned)(v < 0 ? -v : v);
    do { tmp[n++] = (char)('0' + u % 10U); u /= 10U; } while (u && n < 10);
    unsigned k = 0;
    if (v < 0) out[k++] = '-';
    else if (sign && v > 0) out[k++] = '+';
    while (n) out[k++] = tmp[--n];
    out[k] = 0;
}
static void append(char *out, const char *s)
{
    size_t n = strlen(out);
    while (*s && n < 11) out[n++] = *s++;
    out[n] = 0;
}
/* Display mappings mirror the engine's curves in tempura_engine.c. */
void tempura_format(const struct tp_param_info *info, int v, char out[12])
{
    out[0] = 0;
    if (!info) return;
    switch (info->kind) {
    case PK_LIST: {
        const char *s = tempura_list_name(info, v);
        size_t n = strlen(s); if (n > 11) n = 11;
        memcpy(out, s, n); out[n] = 0;
        return;
    }
    case PK_SIGNED: put_int(out, v, 1); return;
    case PK_SEMI: put_int(out, v, 1); append(out, "ST"); return;
    case PK_OCT: put_int(out, v, 1); append(out, "OC"); return;
    case PK_CENTS: put_int(out, v, 1); append(out, "CT"); return;
    case PK_PCT: put_int(out, info->max == 100 ? v : (v * 100 + 63) / 127, 0); append(out, "%"); return;
    case PK_BPM: put_int(out, v, 0); append(out, "BPM"); return;
    case PK_CH: if (!v) { memcpy(out, "OFF", 4); return; } put_int(out, v, 0); return;
    case PK_HZ: {
        /* 0.5 Hz * 2^(v * 8.6 / 127): 0.5 .. ~194 grains per second */
        uint32_t milli = 500;
        for (int i = 0; i < v; ++i) milli = milli * 1048U / 1000U; /* 2^(8.6/127) ~ 1.0480 */
        if (milli < 10000) { put_int(out, (int)(milli / 1000U), 0); append(out, "."); char d[4]; put_int(d, (int)(milli / 100U % 10U), 0); append(out, d); }
        else put_int(out, (int)((milli + 500U) / 1000U), 0);
        append(out, "HZ");
        return;
    }
    case PK_MS: {
        /* Grain size 4 ms * 2^(v * 9 / 127): 4 .. 2048 ms. Delay time 20 ms * 2^(v*6/127). */
        uint32_t micro = info->name[0] == 'D' ? 20000U : 4000U;
        unsigned factor = info->name[0] == 'D' ? 1033U : 1050U;
        for (int i = 0; i < v; ++i) micro = micro * factor / 1000U;
        if (micro >= 1000000U) { put_int(out, (int)(micro / 1000000U), 0); append(out, "."); char d[4]; put_int(d, (int)(micro / 100000U % 10U), 0); append(out, d); append(out, "S"); }
        else { put_int(out, (int)(micro / 1000U), 0); append(out, "MS"); }
        return;
    }
    default: put_int(out, v, 0); return;
    }
}

static void set_defaults(int16_t *values, const struct tp_param_info *info, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) values[i] = info[i].def;
}
static void place(struct tp_canvas *c, unsigned e, unsigned track, unsigned first, unsigned last)
{
    for (unsigned r = first; r <= last && r < TP_ROWS; ++r) c->cells[e][track] |= (uint8_t)(1U << r);
}
/* Four contrasting emitters on a fresh canvas: a soft cloud, a glitchy
 * scatter, a slow octave-down drone and a reversed shimmer. */
void tempura_canvas_default(struct tp_canvas *c)
{
    memset(c, 0, sizeof *c);
    c->magic = TP_CANVAS_MAGIC;
    c->version = TP_CANVAS_VERSION;
    c->bytes = sizeof *c;
    memcpy(c->name, "FIRST CANVAS", 12);
    for (unsigned e = 0; e < TP_EMITTERS; ++e) set_defaults(c->ep[e], emitter_info, EP_COUNT);
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        set_defaults(c->tr[t], track_info, TR_COUNT);
        c->tr[t][TR_SOURCE] = (int16_t)t;
    }
    set_defaults(c->g, global_info, G_COUNT);

    /* E1 cloud */
    c->ep[0][EP_RATE] = 62; c->ep[0][EP_SIZE] = 78; c->ep[0][EP_SPRAY_Y] = 30;
    c->ep[0][EP_SEND_REVERB] = 50;
    place(c, 0, 0, 2, 3); place(c, 0, 1, 5, 5);
    /* E2 glitch */
    c->ep[1][EP_RATE] = 92; c->ep[1][EP_SIZE] = 38; c->ep[1][EP_SHAPE] = 3;
    c->ep[1][EP_REVERSE] = 30; c->ep[1][EP_SPRAY_Y] = 60; c->ep[1][EP_SPRAY_X] = 30;
    c->ep[1][EP_PAN_SPRAY] = 110; c->ep[1][EP_SEND_DELAY] = 60; c->ep[1][EP_LEVEL] = 85;
    place(c, 1, 3, 1, 1); place(c, 1, 4, 6, 6);
    /* E3 drone */
    c->ep[2][EP_RATE] = 40; c->ep[2][EP_SIZE] = 108; c->ep[2][EP_OCTAVE] = -1;
    c->ep[2][EP_ATTACK] = 80; c->ep[2][EP_RELEASE] = 100; c->ep[2][EP_SCAN] = 6;
    c->ep[2][EP_FILTER] = 1; c->ep[2][EP_CUTOFF] = 82; c->ep[2][EP_LFO_DEPTH] = 40;
    c->ep[2][EP_LFO_DEST] = LFO_TO_CUTOFF; c->ep[2][EP_LFO_RATE] = 20;
    c->ep[2][EP_SEND_REVERB] = 70;
    place(c, 2, 2, 0, 1); place(c, 2, 6, 4, 4);
    /* E4 shimmer */
    c->ep[3][EP_RATE] = 70; c->ep[3][EP_SIZE] = 64; c->ep[3][EP_SHAPE] = 4;
    c->ep[3][EP_OCTAVE] = 1; c->ep[3][EP_PITCH_SPRAY] = 127; c->ep[3][EP_QUANT] = 2;
    c->ep[3][EP_REVERSE] = 50; c->ep[3][EP_SEND_REVERB] = 110; c->ep[3][EP_SEND_MOD] = 70;
    c->ep[3][EP_LEVEL] = 70; c->ep[3][EP_RELEASE] = 100;
    place(c, 3, 5, 2, 3); place(c, 3, 7, 6, 7);
}

static void clamp_values(int16_t *values, const struct tp_param_info *info, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        if (values[i] < info[i].min) values[i] = info[i].min;
        if (values[i] > info[i].max) values[i] = info[i].max;
    }
}
void tempura_canvas_sanitize(struct tp_canvas *c)
{
    c->magic = TP_CANVAS_MAGIC; c->version = TP_CANVAS_VERSION; c->bytes = sizeof *c;
    for (unsigned i = 0; i < 12; ++i)
        if (c->name[i] < 32 || c->name[i] > 95) c->name[i] = ' ';
    for (unsigned e = 0; e < TP_EMITTERS; ++e) clamp_values(c->ep[e], emitter_info, EP_COUNT);
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        clamp_values(c->tr[t], track_info, TR_COUNT);
        if (c->tr[t][TR_END] <= c->tr[t][TR_START]) c->tr[t][TR_END] = (int16_t)(c->tr[t][TR_START] + 1);
    }
    clamp_values(c->g, global_info, G_COUNT);
}

uint32_t tempura_crc32(uint32_t crc, const void *data, unsigned bytes)
{
    const uint8_t *p = data;
    crc = ~crc;
    while (bytes--) {
        crc ^= *p++;
        for (unsigned k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

#pragma clang attribute pop
