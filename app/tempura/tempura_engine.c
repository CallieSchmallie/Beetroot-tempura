/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: granular canvas engine.
 *
 * Eight tracks lie side by side like tape strips; each is cut into eight
 * cells. Emitters placed on cells spray grains read from the tracks below
 * them. Played notes become voices; every voice runs one grain stream per
 * placed cell of its emitter. Grains are decoded on demand from the SEA5
 * sample bank in Flash through a small shared chunk cache, or read from the
 * live input ring.
 */
#include "tempura_internal.h"
#include "fm1_sea.h"
#include <stdlib.h>
#include <string.h>
#include "tempura_sea_dqt.inc"

struct tp_cooked tp_cook;

/* ---------------------------------------------------------------- slots */
enum { CHUNK = 320, CHUNK_BYTES = 228 };
struct tp_slot { const uint8_t *data; uint32_t frames, chunks; char name[16]; };
static struct tp_slot slots[TP_SLOTS];

/* ---------------------------------------------------------------- cache */
struct tp_chunk { const uint8_t *tag; int32_t index; uint32_t used; int16_t pcm[CHUNK]; };
/* All large buffers are static so the SDK's link-time check (at least 48 KB
 * of runtime heap for Bluetooth, USB and updates) accounts for them. Earlier
 * builds took them from the heap at run time, which could starve the
 * Bluetooth stack on units that reconnect to a phone at power-on. */
enum { CACHE_SETS = 16 };
static struct tp_chunk cache[CACHE_SETS * 2];
static uint32_t cache_clock, decode_count;
static int16_t silent_chunk[CHUNK];

/* ----------------------------------------------------------------- live */
enum { LIVE_LEN = 16384, LIVE_MASK = LIVE_LEN - 1, LIVE_PX = LIVE_LEN / 160 };  /* 0.74 s at 22.05 kHz */
static int16_t live[LIVE_LEN];
#define live_len ((uint32_t)LIVE_LEN)
#define live_mask ((uint32_t)LIVE_MASK)
static volatile uint32_t live_write;
static int32_t live_pair;
static unsigned live_odd, live_px_count, live_px;
static uint16_t live_px_peak;
static uint8_t live_peaks[160];

/* ------------------------------------------------------------- voices */
struct tp_voice {
    uint8_t active, emitter, note, cell, tag, gen, stage, held, sustained, latched, rr;
    uint32_t age;
    float vel, env, env_prev, accum, scan, pitch, target, comp;
};
static struct tp_voice voices[TP_VOICES];
static uint32_t voice_age;
static uint8_t held_keys[TP_EMITTERS], sustain_on[TP_EMITTERS];
static float bend[TP_EMITTERS], pressure[TP_EMITTERS];
static float lfo_phase[TP_EMITTERS], lfo_value[TP_EMITTERS], lfo_from[TP_EMITTERS], lfo_to[TP_EMITTERS];
static struct tp_svf filter_state[TP_EMITTERS][2];
static float bus_gain_smooth[TP_EMITTERS];

/* ------------------------------------------------------------- grains */
struct tp_reader {
    uint8_t kind, slot;   /* kind: 0 dead, 1 slot, 2 live */
    int8_t dir;
    int32_t chunk, off, frac;
    uint32_t step;
    const int16_t *pcm;
};
struct tp_grain {
    uint8_t active, emitter, voice, gen, shape, col;
    uint16_t delay;
    uint32_t remain, total, phase, inc;
    float gl, gr, blend, env_last, vis_y0, vis_dy;
    struct tp_reader a, b;
};
static struct tp_grain grains[TP_MAX_GRAINS];
static unsigned grain_limit = 24, grain_count;

/* CPU budget. The audio interrupt must finish well inside its 2.9 ms block or
 * the sound crackles and the screen, panel and USB stop getting time. */
enum {
    DECODES_PER_BLOCK = 12,     /* sample chunk decodes allowed per block */
    SPAWN_DECODE_LIMIT = 6,     /* no new grains once this many decodes ran */
    SPAWN_STOP_PCT = 35,        /* no new grains past this share of the block */
    GRAIN_STOP_PCT = 58,        /* shed remaining grains past this share */
};
static uint32_t (*clock_used)(void);
static uint32_t clock_pct_ticks;            /* block_ticks / 100 */
static unsigned decodes_block, safe_mode, starved_block;
static volatile uint8_t stat_limiting, stat_overloads;
static volatile uint32_t stat_shed, stat_starved;
static float limiter_gain = 1.0f;
TP_INLINE int past(unsigned pct) { return clock_used && clock_used() > clock_pct_ticks * pct; }
static float window_table[6][257];
static uint32_t rng = 0x9e3779b9U;

/* --------------------------------------------------------------- buses */
static float bus_l[TP_EMITTERS][FM1_BLOCK_FRAMES], bus_r[TP_EMITTERS][FM1_BLOCK_FRAMES];
static float mod_l[FM1_BLOCK_FRAMES], mod_r[FM1_BLOCK_FRAMES];
static float dly_l[FM1_BLOCK_FRAMES], dly_r[FM1_BLOCK_FRAMES];
static float rev_l[FM1_BLOCK_FRAMES], rev_r[FM1_BLOCK_FRAMES];
static float out_l[FM1_BLOCK_FRAMES], out_r[FM1_BLOCK_FRAMES];

/* -------------------------------------------------------------- events */
enum { EV_ON = 1, EV_OFF, EV_ALL_OFF, EV_PANIC, EV_BEND, EV_PRESS, EV_SUST };
struct tp_event { uint8_t type, emitter, note, value, cell, tag; int16_t data; };
enum { EVENTS = 64 };
static struct tp_event events[EVENTS];
static volatile unsigned ev_write, ev_read;

/* --------------------------------------------------------------- stats */
static volatile uint8_t stat_cpu, stat_voices;
static float bpm = 110.0f;

/* Plain word loop: audio code must not depend on Flash-resident libc. */
typedef uint32_t __attribute__((may_alias)) tp_word;
TP_INLINE void tp_zero(void *p, unsigned bytes)
{
    tp_word *w = p;
    for (unsigned i = 0; i < bytes / 4U; ++i) w[i] = 0;
}

/* ================================================================ init */
static void build_windows(void)
{
    for (unsigned i = 0; i <= 256; ++i) {
        float x = (float)i / 256.0f;
        float hann = 0.5f - 0.5f * tp_cos1(x);
        float tri = x < 0.5f ? 2.0f * x : 2.0f - 2.0f * x;
        float tukey = x < 0.2f ? 0.5f - 0.5f * tp_cos1(x * 2.5f) : x > 0.8f ? 0.5f - 0.5f * tp_cos1((1.0f - x) * 2.5f) : 1.0f;
        float perc = (x < 0.02f ? x * 50.0f : tp_exp(-(x - 0.02f) * 5.0f)) * (x > 0.9f ? (1.0f - x) * 10.0f : 1.0f);
        float swell = (x > 0.98f ? (1.0f - x) * 50.0f : tp_exp(-(0.98f - x) * 5.0f)) * (x < 0.1f ? x * 10.0f : 1.0f);
        float d = (x - 0.5f) * 5.0f;
        float gauss = tp_exp(-d * d * 0.5f) * (x < 0.05f ? x * 20.0f : x > 0.95f ? (1.0f - x) * 20.0f : 1.0f);
        window_table[0][i] = hann;
        window_table[1][i] = tri;
        window_table[2][i] = tukey;
        window_table[3][i] = perc;
        window_table[4][i] = swell;
        window_table[5][i] = gauss;
    }
    for (unsigned s = 0; s < 6; ++s) window_table[s][0] = window_table[s][256] = 0.0f;
}
void tempura_engine_init(void)
{
    memset(voices, 0, sizeof voices);
    memset(grains, 0, sizeof grains);
    memset(slots, 0, sizeof slots);
    ev_write = ev_read = 0;
    build_windows();
    tp_fx_init();
}

void tempura_engine_allocate(void)
{
    /* Buffers are static now; kept so callers need not change. */
}
void tempura_block_clock(uint32_t (*used_ticks)(void), uint32_t block_ticks)
{
    clock_pct_ticks = block_ticks / 100U;
    clock_used = clock_pct_ticks ? used_ticks : NULL;
}
void tempura_set_safe_mode(unsigned on) { safe_mode = on != 0; }
unsigned tempura_safe_mode(void) { return safe_mode; }

/* ============================================================== slots */
void tempura_bind_slot(unsigned slot, const uint8_t *data, unsigned bytes, unsigned frames, const char *name)
{
    if (slot >= TP_SLOTS) return;
    if (data && (!frames || bytes != ((frames + CHUNK - 1U) / CHUNK) * CHUNK_BYTES)) data = NULL;
    tempura_lock();
    slots[slot].data = data;
    slots[slot].frames = data ? frames : 0;
    slots[slot].chunks = data ? (frames + CHUNK - 1U) / CHUNK : 0;
    /* Any cached chunk of a replaced sample must not be reused. */
    for (unsigned i = 0; i < CACHE_SETS * 2U; ++i) cache[i].tag = NULL;
    for (unsigned g = 0; g < TP_MAX_GRAINS; ++g) grains[g].active = 0;
    grain_count = 0;
    tempura_unlock();
    memset(slots[slot].name, 0, sizeof slots[slot].name);
    if (data && name) {
        for (unsigned i = 0; i < 15 && name[i]; ++i) {
            char ch = name[i];
            slots[slot].name[i] = ch >= 'a' && ch <= 'z' ? (char)(ch - 32) : ch;
        }
    }
}
const char *tempura_slot_name(unsigned slot)
{
    if (slot >= TP_SLOTS) return "";
    return slots[slot].data ? (slots[slot].name[0] ? slots[slot].name : "UNTITLED") : "EMPTY";
}
unsigned tempura_slot_frames(unsigned slot) { return slot < TP_SLOTS ? slots[slot].frames : 0; }

static TP_AUDIO int tp_sea5_decode(const uint8_t *c, int16_t *pcm);
/* Returns the decoded chunk, or NULL when this block's decode budget is used
 * up (the caller then keeps what it has rather than dropping to silence). */
static TP_AUDIO const int16_t *cache_try(unsigned slot, int32_t index)
{
    const struct tp_slot *s = &slots[slot];
    if (!s->data || index < 0 || (uint32_t)index >= s->chunks) return silent_chunk;
    uint32_t h = ((uint32_t)(uintptr_t)s->data >> 4) * 2654435761U ^ (uint32_t)index * 40503U;
    h ^= h >> 15;
    struct tp_chunk *way = &cache[(h % CACHE_SETS) * 2U];
    ++cache_clock;
    if (way[0].tag == s->data && way[0].index == index) { way[0].used = cache_clock; return way[0].pcm; }
    if (way[1].tag == s->data && way[1].index == index) { way[1].used = cache_clock; return way[1].pcm; }
    /* A miss costs a full chunk decode; cap them so one block stays bounded. */
    if (decodes_block >= DECODES_PER_BLOCK || past(GRAIN_STOP_PCT + 8U)) { ++stat_starved; starved_block = 1; return NULL; }
    ++decodes_block;
    struct tp_chunk *victim = (int32_t)(way[0].used - way[1].used) <= 0 ? &way[0] : &way[1];
    if (tp_sea5_decode(s->data + (uint32_t)index * CHUNK_BYTES, victim->pcm)) {
        victim->tag = NULL;
        return silent_chunk;
    }
    ++decode_count;
    victim->tag = s->data; victim->index = index; victim->used = cache_clock;
    return victim->pcm;
}

/* Fast SEA5 chunk decoder (bit-exact with fm1_sea_decode, see tests).
 * Unpacks eight 5-bit codes from every five bytes instead of one bit at a
 * time; this is the hottest path in the engine. */
static TP_AUDIO int tp_sea5_decode(const uint8_t *c, int16_t *pcm)
{
    if (c[0] != 1 || c[1] != 0x45 || c[2] != 20 || c[3] != 0x5a) return -1;
    uint8_t codes[CHUNK];
    const uint8_t *b = c + 28;
    for (unsigned k = 0; k < CHUNK; k += 8, b += 5) {
        unsigned b0 = b[0], b1 = b[1], b2 = b[2], b3 = b[3], b4 = b[4];
        codes[k]     = (uint8_t)(b0 >> 3);
        codes[k + 1] = (uint8_t)(((b0 & 7U) << 2) | (b1 >> 6));
        codes[k + 2] = (uint8_t)((b1 >> 1) & 31U);
        codes[k + 3] = (uint8_t)(((b1 & 1U) << 4) | (b2 >> 4));
        codes[k + 4] = (uint8_t)(((b2 & 15U) << 1) | (b3 >> 7));
        codes[k + 5] = (uint8_t)((b3 >> 2) & 31U);
        codes[k + 6] = (uint8_t)(((b3 & 3U) << 3) | (b4 >> 5));
        codes[k + 7] = (uint8_t)(b4 & 31U);
    }
    int32_t h0, h1, h2, h3, w0, w1, w2, w3;
#define SEED(i) ((int32_t)(int16_t)(uint16_t)(c[4 + (i) * 2] | ((unsigned)c[5 + (i) * 2] << 8)))
    h0 = SEED(0); h1 = SEED(1); h2 = SEED(2); h3 = SEED(3);
    w0 = SEED(4); w1 = SEED(5); w2 = SEED(6); w3 = SEED(7);
#undef SEED
#define CLAMP16(v) ((v) > 32767 ? 32767 : (v) < -32768 ? -32768 : (v))
    for (unsigned g = 0; g < 16; ++g) {
        unsigned scale = (c[20 + g / 2] >> ((g & 1) ? 0 : 4)) & 15U;
        const int16_t *dq = tp_sea_dqt + (scale << 5);
        const uint8_t *cd = codes + g * 20;
        int16_t *out = pcm + g * 20;
        for (unsigned i = 0; i < 20; ++i) {
            int64_t sum = (int64_t)h0 * w0 + (int64_t)h1 * w1 + (int64_t)h2 * w2 + (int64_t)h3 * w3;
            int32_t residual = dq[cd[i]];
            int32_t value = (int32_t)(sum >> 13) + residual;
            value = CLAMP16(value);
            int32_t delta = residual >> 4;
            w0 += h0 < 0 ? -delta : delta; w0 = CLAMP16(w0);
            w1 += h1 < 0 ? -delta : delta; w1 = CLAMP16(w1);
            w2 += h2 < 0 ? -delta : delta; w2 = CLAMP16(w2);
            w3 += h3 < 0 ? -delta : delta; w3 = CLAMP16(w3);
            h0 = h1; h1 = h2; h2 = h3; h3 = value;
            out[i] = (int16_t)value;
        }
    }
#undef CLAMP16
    return 0;
}
#ifdef FM1_HOST_TEST
int tempura_test_decode(const uint8_t *chunk, int16_t *pcm) { return tp_sea5_decode(chunk, pcm); }
#endif

/* ============================================================== events */
static int push_event(const struct tp_event *e)
{
    unsigned w = ev_write, next = (w + 1U) % EVENTS;
    if (next == ev_read) return -1;
    events[w] = *e;
    TP_BARRIER();
    ev_write = next;
    return 0;
}
int tempura_note_on(unsigned emitter, unsigned note, unsigned velocity, unsigned cell, unsigned tag)
{
    if (emitter >= TP_EMITTERS || note > 127) return -1;
    struct tp_event e = {EV_ON, (uint8_t)emitter, (uint8_t)note, (uint8_t)(velocity ? velocity : 1), (uint8_t)cell, (uint8_t)tag, 0};
    return push_event(&e);
}
int tempura_note_off(unsigned emitter, unsigned note, unsigned cell, unsigned tag)
{
    if (emitter >= TP_EMITTERS || note > 127) return -1;
    struct tp_event e = {EV_OFF, (uint8_t)emitter, (uint8_t)note, 0, (uint8_t)cell, (uint8_t)tag, 0};
    return push_event(&e);
}
void tempura_all_off(void) { struct tp_event e = {EV_ALL_OFF, 0, 0, 0, 0, 0, 0}; (void)push_event(&e); }
void tempura_panic(void) { struct tp_event e = {EV_PANIC, 0, 0, 0, 0, 0, 0}; (void)push_event(&e); }
void tempura_set_bend(unsigned emitter, int semis_q8)
{ struct tp_event e = {EV_BEND, (uint8_t)emitter, 0, 0, 0, 0, (int16_t)semis_q8}; (void)push_event(&e); }
void tempura_set_pressure(unsigned emitter, unsigned value)
{ struct tp_event e = {EV_PRESS, (uint8_t)emitter, 0, (uint8_t)value, 0, 0, 0}; (void)push_event(&e); }
void tempura_set_sustain(unsigned emitter, unsigned on)
{ struct tp_event e = {EV_SUST, (uint8_t)emitter, 0, (uint8_t)(on != 0), 0, 0, 0}; (void)push_event(&e); }
void tempura_set_bpm_q8(uint32_t q8) { bpm = (float)q8 * (1.0f / 256.0f); }

static TP_AUDIO void voice_release(struct tp_voice *v)
{
    v->stage = 2;
    v->held = 0;
}
static TP_AUDIO void note_on(const struct tp_event *e)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[e->emitter];
    float vel = (float)e->value * (1.0f / 127.0f);
    float vel_gain = 1.0f - ce->velo * (1.0f - vel * vel);
    if (ce->mode == MODE_MONO) {
        for (unsigned i = 0; i < TP_VOICES; ++i) {
            struct tp_voice *v = &voices[i];
            if (v->active && v->emitter == e->emitter && v->stage != 2) {
                if (v->held && held_keys[e->emitter]) --held_keys[e->emitter];
                v->note = e->note; v->cell = e->cell; v->tag = e->tag;
                v->target = (float)e->note - 60.0f;
                if (ce->glide_coef <= 0.0f) v->pitch = v->target;
                v->vel = vel_gain; v->held = 1; v->sustained = 0;
                ++held_keys[e->emitter];
                return;
            }
        }
    }
    if (ce->mode == MODE_LATCH && !held_keys[e->emitter]) {
        for (unsigned i = 0; i < TP_VOICES; ++i)
            if (voices[i].active && voices[i].emitter == e->emitter && voices[i].latched) voice_release(&voices[i]);
    }
    /* Free voice, else the quietest releasing voice, else the oldest. */
    struct tp_voice *v = NULL;
    for (unsigned i = 0; i < TP_VOICES && !v; ++i) if (!voices[i].active) v = &voices[i];
    if (!v) {
        float quiet = 2.0f;
        for (unsigned i = 0; i < TP_VOICES; ++i)
            if (voices[i].stage == 2 && voices[i].env < quiet) { quiet = voices[i].env; v = &voices[i]; }
    }
    if (!v) {
        uint32_t oldest = 0;
        for (unsigned i = 0; i < TP_VOICES; ++i)
            if (voice_age - voices[i].age >= oldest) { oldest = voice_age - voices[i].age; v = &voices[i]; }
        if (v->held && held_keys[v->emitter]) --held_keys[v->emitter];
    }
    uint8_t gen = (uint8_t)(v->gen + 1U);
    tp_zero(v, sizeof *v);
    v->gen = gen;
    v->active = 1; v->emitter = e->emitter; v->note = e->note; v->cell = e->cell; v->tag = e->tag;
    v->held = 1; v->latched = ce->mode == MODE_LATCH;
    v->vel = vel_gain; v->age = ++voice_age;
    v->target = v->pitch = (float)e->note - 60.0f;
    v->accum = 0.999f;
    ++held_keys[e->emitter];
}
static TP_AUDIO void note_off(const struct tp_event *e)
{
    for (unsigned i = 0; i < TP_VOICES; ++i) {
        struct tp_voice *v = &voices[i];
        if (!v->active || !v->held || v->emitter != e->emitter || v->note != e->note ||
            v->cell != e->cell || v->tag != e->tag) continue;
        v->held = 0;
        if (held_keys[e->emitter]) --held_keys[e->emitter];
        if (v->latched) return;
        if (sustain_on[e->emitter]) v->sustained = 1;
        else voice_release(v);
        return;
    }
}
static TP_AUDIO void process_events(void)
{
    while (ev_read != ev_write) {
        TP_BARRIER();
        const struct tp_event *e = &events[ev_read];
        if (e->type == EV_ON) note_on(e);
        else if (e->type == EV_OFF) note_off(e);
        else if (e->type == EV_ALL_OFF || e->type == EV_PANIC) {
            for (unsigned i = 0; i < TP_VOICES; ++i) {
                if (e->type == EV_PANIC) voices[i].active = 0;
                else if (voices[i].active) voice_release(&voices[i]);
            }
            if (e->type == EV_PANIC) {
                for (unsigned g = 0; g < TP_MAX_GRAINS; ++g) grains[g].active = 0;
                grain_count = 0;
                tp_fx_clear();
            }
            for (unsigned k = 0; k < TP_EMITTERS; ++k) held_keys[k] = sustain_on[k] = 0;
        } else if (e->type == EV_BEND && e->emitter < TP_EMITTERS) bend[e->emitter] = (float)e->data * (1.0f / 256.0f);
        else if (e->type == EV_PRESS && e->emitter < TP_EMITTERS) pressure[e->emitter] = (float)e->value * (1.0f / 127.0f);
        else if (e->type == EV_SUST && e->emitter < TP_EMITTERS) {
            sustain_on[e->emitter] = e->value;
            if (!e->value)
                for (unsigned i = 0; i < TP_VOICES; ++i)
                    if (voices[i].active && voices[i].emitter == e->emitter && voices[i].sustained) {
                        voices[i].sustained = 0; voice_release(&voices[i]);
                    }
        }
        TP_BARRIER();
        ev_read = (ev_read + 1U) % EVENTS;
    }
}

/* ============================================================== grains */
TP_INLINE unsigned bit_count8(unsigned x) { x = x - ((x >> 1) & 0x55U); x = (x & 0x33U) + ((x >> 2) & 0x33U); return (x + (x >> 4)) & 0x0fU; }

static TP_AUDIO int reader_start(struct tp_reader *r, unsigned col, float y01, float semis, int dir)
{
    const struct tp_cooked_track *t = &tp_cook.t[col];
    float pos01 = t->start + y01 * (t->end - t->start);
    float rate;
    r->dir = (int8_t)((t->reverse ? -dir : dir));
    if (t->source < TP_SLOTS) {
        const struct tp_slot *s = &slots[t->source];
        if (!s->data) { r->kind = 0; return -1; }
        uint32_t frame = (uint32_t)(pos01 * (float)(s->frames - 1U));
        r->kind = 1; r->slot = t->source;
        r->chunk = (int32_t)(frame / CHUNK); r->off = (int32_t)(frame % CHUNK);
        r->pcm = cache_try(r->slot, r->chunk);
        if (!r->pcm) { r->kind = 0; return -1; }
        rate = (float)TP_SAMPLE_RATE * (1.0f / 44100.0f);
    } else {
        r->kind = 2;
        r->off = (int32_t)(pos01 * (float)(live_len - 1U));
        rate = (float)TP_LIVE_RATE * (1.0f / 44100.0f);
    }
    r->frac = 0;
    float ratio = tp_exp2((semis + t->tune_semis) * (1.0f / 12.0f)) * rate;
    if (ratio > 16.0f) ratio = 16.0f;
    r->step = (uint32_t)(ratio * 65536.0f);
    return 0;
}
TP_INLINE float reader_next(struct tp_reader *r)
{
    float s0, s1;
    if (r->kind == 1) {
        s0 = (float)r->pcm[r->off];
        if (r->off + 1 < CHUNK) s1 = (float)r->pcm[r->off + 1];
        else { const int16_t *n = cache_try(r->slot, r->chunk + 1); s1 = n ? (float)n[0] : s0; }
    } else if (r->kind == 2) {
        s0 = (float)live[(uint32_t)r->off & live_mask];
        s1 = (float)live[(uint32_t)(r->off + 1) & live_mask];
    } else return 0.0f;
    float y = s0 + (s1 - s0) * (float)r->frac * (1.0f / 65536.0f);
    int32_t whole = (int32_t)(r->step >> 16), fr = (int32_t)(r->step & 0xffffU);
    if (r->dir >= 0) {
        r->frac += fr;
        if (r->frac >= 65536) { r->frac -= 65536; ++whole; }
        r->off += whole;
        if (r->kind == 1 && r->off >= CHUNK) {
            int32_t was = r->chunk;
            while (r->off >= CHUNK) { r->off -= CHUNK; ++r->chunk; }
            if ((uint32_t)r->chunk >= slots[r->slot].chunks) r->kind = 0;
            else {
                const int16_t *n = cache_try(r->slot, r->chunk);
                if (n) r->pcm = n; else r->chunk = was; /* no budget: replay this chunk */
            }
        }
    } else {
        r->frac -= fr;
        if (r->frac < 0) { r->frac += 65536; ++whole; }
        r->off -= whole;
        if (r->kind == 1 && r->off < 0) {
            int32_t was = r->chunk;
            while (r->off < 0) { r->off += CHUNK; --r->chunk; }
            if (r->chunk < 0) r->kind = 0;
            else {
                const int16_t *n = cache_try(r->slot, r->chunk);
                if (n) r->pcm = n; else r->chunk = was;
            }
        }
    }
    return y;
}

/* Pick the n-th placed cell of an emitter, scanning tracks then rows. */
static TP_AUDIO unsigned nth_cell(const struct tp_cooked_emitter *ce, unsigned n)
{
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        unsigned bits = ce->cells[t], count = bit_count8(bits);
        if (n >= count) { n -= count; continue; }
        for (unsigned r = 0; r < TP_ROWS; ++r)
            if (bits & (1U << r)) { if (!n) return t * 8U + r; --n; }
    }
    return 0;
}

static TP_AUDIO struct tp_grain *grain_slot(void)
{
    if (grain_count < grain_limit)
        for (unsigned i = 0; i < TP_MAX_GRAINS; ++i) if (!grains[i].active) return &grains[i];
    /* Pool full: steal the grain closest to its end, if it is nearly silent. */
    struct tp_grain *best = NULL; uint32_t phase = 0xd0000000U;
    for (unsigned i = 0; i < TP_MAX_GRAINS; ++i)
        if (grains[i].active && grains[i].phase > phase) { phase = grains[i].phase; best = &grains[i]; }
    if (best) { best->active = 0; --grain_count; }
    return best;
}

static TP_AUDIO void spawn(struct tp_voice *v, unsigned vi, unsigned delay)
{
    if (decodes_block >= SPAWN_DECODE_LIMIT) { ++stat_shed; return; }
    unsigned e = v->emitter;
    const struct tp_cooked_emitter *ce = &tp_cook.e[e];
    unsigned cell;
    if (v->cell < 64) cell = v->cell;
    else {
        unsigned placed = ce->placed;
        if (!placed) return;
        cell = nth_cell(ce, v->rr % placed);
        ++v->rr;
    }
    unsigned col = cell >> 3, row = cell & 7U;
    float lfo = lfo_value[e] * ce->lfo_depth;
    unsigned dest = ce->lfo_dest;
    float press = pressure[e];
    unsigned at = tp_cook.fx.at_dest;

    /* Horizontal spray: jump to, or blend between, neighbouring tracks. */
    float x = (float)col;
    if (ce->spray_x > 0.0f) x += tp_randbi(&rng) * ce->spray_x;
    if (x < 0.0f) x = -x;
    if (x > 7.0f) x = 14.0f - x;
    x = tp_clampf(x, 0.0f, 7.0f);
    unsigned col_a, col_b; float blend;
    if (ce->align) { col_a = col_b = (unsigned)(x + 0.5f); blend = 0.0f; }
    else { col_a = (unsigned)x; col_b = col_a < 7 ? col_a + 1U : col_a; blend = x - (float)col_a; }

    /* Vertical position inside the track: cell centre, offset, scan, spray. */
    float spray = ce->spray_y + (dest == LFO_TO_SPRAY ? lfo * 2.0f : 0.0f) + (at == 4 ? press * 2.0f : 0.0f);
    float y = (float)row + 0.5f + ce->pos_offset + v->scan + (dest == LFO_TO_POS ? lfo * 2.0f : 0.0f);
    if (spray > 0.0f) y += tp_randbi(&rng) * spray;
    y *= 0.125f;
    y -= (float)tp_floori(y);

    float size = ce->size_s;
    if (dest == LFO_TO_SIZE) size *= tp_exp2(lfo * 2.0f);
    if (at == 3) size *= tp_exp2(press * 2.0f);
    uint32_t frames = (uint32_t)tp_clampf(size * 44100.0f, 64.0f, 44100.0f * 4.0f);

    float semis = v->pitch + ce->pitch_semis + bend[e] + (dest == LFO_TO_PITCH ? lfo * 12.0f : 0.0f);
    if (ce->pitch_spray > 0.0f) {
        if (ce->quant <= 1) {
            float s = tp_randbi(&rng) * ce->pitch_spray;
            if (ce->quant == 1) s = (float)tp_floori(s + 0.5f);
            semis += s;
        } else if (tp_rand01(&rng) < ce->pitch_spray) {
            uint32_t r = tp_rand(&rng) >> 24;
            if (ce->quant == 2) semis += (r & 1U) ? 12.0f : -12.0f;
            else semis += (r % 3U) == 0 ? 7.0f : (r % 3U) == 1 ? 12.0f : 19.0f;
        }
    }
    int dir = tp_rand01(&rng) < ce->reverse_p ? -1 : 1;

    struct tp_grain *g = grain_slot();
    if (!g) return;
    if (reader_start(&g->a, col_a, y, semis, dir)) return;
    if (blend > 0.001f && col_b != col_a) {
        if (reader_start(&g->b, col_b, y, semis, dir)) blend = 0.0f;
    } else { g->b.kind = 0; blend = 0.0f; }

    float pan = ce->pan + tp_cook.t[col_a].pan + (dest == LFO_TO_PAN ? lfo : 0.0f);
    if (ce->pan_spray > 0.0f) pan += tp_randbi(&rng) * ce->pan_spray;
    pan = tp_clampf(pan, -1.0f, 1.0f);
    float level = v->vel * v->comp * tp_cook.t[col_a].level * (1.0f / 32768.0f);
    g->gl = level * tp_cos1((pan + 1.0f) * 0.125f);
    g->gr = level * tp_sin1((pan + 1.0f) * 0.125f);
    g->blend = blend;
    g->emitter = (uint8_t)e; g->voice = (uint8_t)vi; g->gen = v->gen;
    g->shape = ce->shape; g->col = (uint8_t)col_a;
    g->delay = (uint16_t)delay;
    g->remain = g->total = frames; g->phase = 0;
    g->inc = (uint32_t)(4294967295.0f / (float)frames);
    g->env_last = v->env;
    g->vis_y0 = y;
    g->vis_dy = (float)g->a.step * (1.0f / 65536.0f) * (float)g->a.dir;
    g->active = 1;
    ++grain_count;
}

static TP_AUDIO void render_grain(struct tp_grain *g)
{
    const struct tp_voice *v = &voices[g->voice];
    float e0, e1;
    if (v->active && v->gen == g->gen) { e0 = v->env_prev; e1 = v->env; g->env_last = e1; }
    else e0 = e1 = g->env_last;
    float *l = bus_l[g->emitter], *r = bus_r[g->emitter];
    /* Other grains may have evicted our chunk since the last block. */
    if (g->a.kind == 1) { const int16_t *p = cache_try(g->a.slot, g->a.chunk); if (p) g->a.pcm = p; }
    if (g->b.kind == 1) { const int16_t *p = cache_try(g->b.slot, g->b.chunk); if (p) g->b.pcm = p; }
    const float *win = window_table[g->shape];
    unsigned i = g->delay;
    g->delay = 0;
    float env = e0 + (e1 - e0) * (float)i * (1.0f / FM1_BLOCK_FRAMES);
    float env_step = (e1 - e0) * (1.0f / FM1_BLOCK_FRAMES);
    float wa = 1.0f - g->blend, wb = g->blend;
    /* Fast path: one sample reader moving forward (most grains). Runs in
     * registers between chunk boundaries; same arithmetic as below. */
#ifndef TP_NO_FASTPATH
    if (g->a.kind == 1 && g->a.dir >= 0 && wb <= 0.0f) {
        struct tp_reader *rd = &g->a;
        uint32_t phase = g->phase, inc = g->inc, remain = g->remain;
        int32_t off = rd->off, frac = rd->frac;
        const int32_t whole0 = (int32_t)(rd->step >> 16), fr0 = (int32_t)(rd->step & 0xffffU);
        const int16_t *pcm = rd->pcm;
        float gl = g->gl * wa, gr = g->gr * wa;
        for (; i < FM1_BLOCK_FRAMES && remain; ++i, --remain) {
            unsigned idx = phase >> 24;
            float wf = (float)((phase >> 8) & 0xffffU) * (1.0f / 65536.0f);
            float w = win[idx] + (win[idx + 1] - win[idx]) * wf;
            phase += inc;
            float s0 = (float)pcm[off];
            float s1;
            if (off + 1 < CHUNK) s1 = (float)pcm[off + 1];
            else { const int16_t *n = cache_try(rd->slot, rd->chunk + 1); s1 = n ? (float)n[0] : s0; }
            float y = (s0 + (s1 - s0) * (float)frac * (1.0f / 65536.0f)) * w * env;
            env += env_step;
            l[i] += y * gl;
            r[i] += y * gr;
            frac += fr0;
            int32_t adv = whole0;
            if (frac >= 65536) { frac -= 65536; ++adv; }
            off += adv;
            if (off >= CHUNK) {
                int32_t was = rd->chunk;
                while (off >= CHUNK) { off -= CHUNK; ++rd->chunk; }
                if ((uint32_t)rd->chunk >= slots[rd->slot].chunks) { rd->kind = 0; --remain; break; }
                const int16_t *n = cache_try(rd->slot, rd->chunk);
                if (n) pcm = n; else rd->chunk = was; /* no budget: replay this chunk */
            }
        }
        g->phase = phase; g->remain = remain; rd->off = off; rd->frac = frac; rd->pcm = pcm;
        if (!g->remain || g->a.kind == 0) { g->active = 0; --grain_count; }
        return;
    }
#endif
    for (; i < FM1_BLOCK_FRAMES && g->remain; ++i, --g->remain) {
        unsigned idx = g->phase >> 24;
        float fr = (float)((g->phase >> 8) & 0xffffU) * (1.0f / 65536.0f);
        float w = win[idx] + (win[idx + 1] - win[idx]) * fr;
        g->phase += g->inc;
        float s = reader_next(&g->a) * wa;
        if (wb > 0.0f) s += reader_next(&g->b) * wb;
        s *= w * env;
        env += env_step;
        l[i] += s * g->gl;
        r[i] += s * g->gr;
    }
    if (!g->remain || (g->a.kind == 0 && g->b.kind == 0)) { g->active = 0; --grain_count; }
}

/* ============================================================== render */
static TP_AUDIO float lfo_shape(unsigned e, unsigned shape, float phase, int wrapped)
{
    if (shape == 0) return tp_sin1(phase);
    if (shape == 1) return phase < 0.5f ? phase * 4.0f - 1.0f : 3.0f - phase * 4.0f;
    if (shape == 2) return 1.0f - phase * 2.0f;
    if (shape == 3) return phase < 0.5f ? 1.0f : -1.0f;
    if (wrapped) { lfo_from[e] = lfo_to[e]; lfo_to[e] = tp_randbi(&rng); }
    if (shape == 4) return lfo_to[e];
    float s = phase * phase * (3.0f - 2.0f * phase);
    return lfo_from[e] + (lfo_to[e] - lfo_from[e]) * s;
}

static TP_AUDIO void update_voices(void)
{
    const float block_s = (float)FM1_BLOCK_FRAMES / 44100.0f;
    unsigned active = 0;
    for (unsigned i = 0; i < TP_VOICES; ++i) {
        struct tp_voice *v = &voices[i];
        if (!v->active) continue;
        const struct tp_cooked_emitter *ce = &tp_cook.e[v->emitter];
        v->env_prev = v->env;
        if (v->stage == 0) {
            v->env += ce->attack_inc * FM1_BLOCK_FRAMES;
            if (v->env >= 1.0f) { v->env = 1.0f; v->stage = 1; }
        } else if (v->stage == 1) {
            v->env = ce->sustain + (v->env - ce->sustain) * ce->decay_coef;
        } else {
            v->env *= ce->release_coef;
            if (v->env < 0.0003f) { v->active = 0; continue; }
        }
        if (ce->glide_coef > 0.0f) v->pitch = v->target + (v->pitch - v->target) * ce->glide_coef;
        else v->pitch = v->target;
        v->scan += ce->scan_cells_s * block_s;
        if (v->scan > 8.0f) v->scan -= 8.0f;
        if (v->scan < -8.0f) v->scan += 8.0f;
        ++active;
    }
    stat_voices = (uint8_t)active;
}

static TP_AUDIO void spawn_grains(void)
{
    const float block_s = (float)FM1_BLOCK_FRAMES / 44100.0f;
    for (unsigned i = 0; i < TP_VOICES; ++i) {
        struct tp_voice *v = &voices[i];
        if (!v->active || v->env < 0.0005f) continue;
        const struct tp_cooked_emitter *ce = &tp_cook.e[v->emitter];
        unsigned streams = v->cell < 64 ? 1U : ce->placed;
        if (!streams) continue;
        float rate = ce->rate_hz;
        if (ce->lfo_dest == LFO_TO_RATE) rate *= tp_exp2(lfo_value[v->emitter] * ce->lfo_depth * 3.0f);
        /* Equal-loudness: dense clouds of long grains are scaled down. */
        float overlap = rate * ce->size_s;
        v->comp = overlap > 1.0f ? 1.0f / (1.0f + 0.35f * (overlap - 1.0f)) : 1.0f;
        if (v->comp < 0.12f) v->comp = 0.12f;
        float d = rate * (float)streams * block_s;
        float a0 = v->accum, a1 = a0 + d;
        int n = tp_floori(a0) + 1;
        for (unsigned spawned = 0; (float)n <= a1 && spawned < 8U; ++n, ++spawned) {
            float t = ((float)n - a0) / d * (float)FM1_BLOCK_FRAMES;
            spawn(v, i, t >= FM1_BLOCK_FRAMES ? FM1_BLOCK_FRAMES - 1U : (unsigned)t);
        }
        v->accum = a1 - (float)tp_floori(a1);
    }
}

static TP_AUDIO void finish_emitters(void)
{
    for (unsigned e = 0; e < TP_EMITTERS; ++e) {
        const struct tp_cooked_emitter *ce = &tp_cook.e[e];
        float *l = bus_l[e], *r = bus_r[e];
        float lfo = lfo_value[e] * ce->lfo_depth;
        float press = pressure[e];
        unsigned at = tp_cook.fx.at_dest;
        float gain = ce->level;
        if (ce->lfo_dest == LFO_TO_LEVEL) gain *= tp_clampf(1.0f + lfo, 0.0f, 2.0f);
        if (at == 2) gain *= 0.25f + 0.75f * press;
        float g0 = bus_gain_smooth[e];
        bus_gain_smooth[e] = gain;
        if (ce->filter) {
            float env = 0.0f;
            for (unsigned i = 0; i < TP_VOICES; ++i)
                if (voices[i].active && voices[i].emitter == e && voices[i].env > env) env = voices[i].env;
            float oct = ce->cutoff_oct + ce->env_filter_oct * env +
                        (ce->lfo_dest == LFO_TO_CUTOFF ? lfo * 4.0f : 0.0f) + (at == 1 ? press * 5.0f : 0.0f);
            struct tp_svf_coef c;
            tp_svf_design(&c, 20.0f * tp_exp2(oct), ce->res_q);
            for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
                l[i] = tp_svf_run(&filter_state[e][0], &c, l[i], ce->filter);
                r[i] = tp_svf_run(&filter_state[e][1], &c, r[i], ce->filter);
            }
            for (unsigned k = 0; k < 2; ++k) {
                filter_state[e][k].ic1 = tp_flush(filter_state[e][k].ic1);
                filter_state[e][k].ic2 = tp_flush(filter_state[e][k].ic2);
            }
        }
        float step = (gain - g0) * (1.0f / FM1_BLOCK_FRAMES), gg = g0;
        float dry = ce->dry, sm = ce->send_mod, sd = ce->send_delay, sr = ce->send_reverb;
        for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
            float a = l[i] * gg, b = r[i] * gg;
            gg += step;
            out_l[i] += a * dry; out_r[i] += b * dry;
            mod_l[i] += a * sm; mod_r[i] += b * sm;
            dly_l[i] += a * sd; dly_r[i] += b * sd;
            rev_l[i] += a * sr; rev_r[i] += b * sr;
        }
    }
}

TP_AUDIO int tempura_render(int32_t *output, unsigned frames)
{
    if (!output || frames != FM1_BLOCK_FRAMES) return -1;
    if (safe_mode) {
        for (unsigned i = 0; i < 2U * FM1_BLOCK_FRAMES; ++i) output[i] = 0;
        return 0;
    }
    decodes_block = 0; starved_block = 0;
    process_events();
    const float block_s = (float)FM1_BLOCK_FRAMES / 44100.0f;
    for (unsigned e = 0; e < TP_EMITTERS; ++e) {
        lfo_phase[e] += tp_cook.e[e].lfo_rate * block_s;
        int wrapped = lfo_phase[e] >= 1.0f;
        if (wrapped) lfo_phase[e] -= (float)tp_floori(lfo_phase[e]);
        lfo_value[e] = lfo_shape(e, tp_cook.e[e].lfo_shape, lfo_phase[e], wrapped);
    }
    update_voices();
    tp_zero(bus_l, sizeof bus_l); tp_zero(bus_r, sizeof bus_r);
    tp_zero(mod_l, sizeof mod_l); tp_zero(mod_r, sizeof mod_r);
    tp_zero(dly_l, sizeof dly_l); tp_zero(dly_r, sizeof dly_r);
    tp_zero(rev_l, sizeof rev_l); tp_zero(rev_r, sizeof rev_r);
    tp_zero(out_l, sizeof out_l); tp_zero(out_r, sizeof out_r);
    if (!past(SPAWN_STOP_PCT)) spawn_grains();
    else ++stat_shed;
    /* Render grains until the time budget runs out; shed the rest rather
     * than overrun the block. The grain limit then drops (see cpu_report). */
    unsigned rendered = 0, shed = 0;
    for (unsigned i = 0; i < TP_MAX_GRAINS; ++i) {
        struct tp_grain *g = &grains[i];
        if (!g->active) continue;
        if (shed || ((rendered & 1U) && past(GRAIN_STOP_PCT))) {
            g->active = 0; --grain_count; ++shed;
            continue;
        }
        render_grain(g);
        ++rendered;
    }
    /* Grains needed more decoding than the budget allows: fewer grains. */
    if (starved_block && grain_limit > 4U) --grain_limit;
    if (shed) {
        stat_shed += shed;
        if (stat_overloads < 255U) ++stat_overloads;
        unsigned keep = rendered > 6U ? rendered - 2U : 4U;
        if (keep < grain_limit) grain_limit = keep;
    }
    finish_emitters();
    tp_fx_render(mod_l, mod_r, dly_l, dly_r, rev_l, rev_r, out_l, out_r, FM1_BLOCK_FRAMES);

    /* Output limiter: instant attack, ~110 ms release, ceiling -0.5 dBFS.
     * Keeps a redlined mix clean instead of hard-clipping into crackle. */
    float vol = tp_cook.fx.volume, gain = limiter_gain, lowest = 1.0f;
    const float ceiling = 0.94f;
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        float l = out_l[i] * vol, r = out_r[i] * vol;
        float a = l < 0 ? -l : l, b = r < 0 ? -r : r, peak = a > b ? a : b;
        if (peak * gain > ceiling) gain = ceiling / peak;
        else gain += (1.0f - gain) * 0.0002f;
        if (gain < lowest) lowest = gain;
        output[2 * i] = (int32_t)(tp_soft(l * gain) * 8388000.0f);
        output[2 * i + 1] = (int32_t)(tp_soft(r * gain) * 8388000.0f);
    }
    if (gain != gain || gain <= 0.0f) {
        /* A NaN or Inf got in (should never happen): reset and go quiet. */
        gain = 1.0f;
        for (unsigned e = 0; e < TP_EMITTERS; ++e) filter_state[e][0].ic1 = filter_state[e][0].ic2 = filter_state[e][1].ic1 = filter_state[e][1].ic2 = 0.0f;
        tp_fx_clear();
        for (unsigned i = 0; i < 2U * FM1_BLOCK_FRAMES; ++i) output[i] = 0;
    }
    limiter_gain = gain;
    stat_limiting = (uint8_t)(lowest < 0.5f ? 2 : lowest < 0.89f ? 1 : 0); /* >6 dB, >1 dB */
    return 0;
}

/* Adaptive grain limit from the measured share of the block period. */
TP_AUDIO void tempura_cpu_report(uint32_t used_ticks, uint32_t block_ticks)
{
    static unsigned calm;
    if (!block_ticks || safe_mode) { stat_cpu = 0; return; }
    /* 32-bit only: no library division helpers in the audio interrupt. */
    uint32_t per_pct = block_ticks / 100U;
    uint32_t pct = per_pct ? used_ticks / per_pct : 0;
    stat_cpu = (uint8_t)(pct > 255U ? 255U : pct);
    if (pct > 85U && grain_limit > 4U) { grain_limit = grain_limit * 3U / 4U; if (grain_limit < 4U) grain_limit = 4U; calm = 0; }
    else if (pct > 70U && grain_limit > 4U) { grain_limit -= 2U; calm = 0; }
    else if (pct < 55U && grain_limit < TP_MAX_GRAINS && ++calm > 64U) { ++grain_limit; calm = 0; }
    else if (pct >= 55U) calm = 0;
}

/* ================================================================ live */
TP_AUDIO unsigned tempura_monitor_gain_q7(void)
{
    float m = tp_cook.fx.monitor;
    return (unsigned)(tp_clampf(m, 0.0f, 1.0f) * 128.0f);
}
TP_AUDIO void tempura_live_feed(const int16_t *bt, const int32_t *usb, unsigned frames)
{
    if (tp_cook.fx.freeze) return;
    unsigned src = tp_cook.fx.live_src;
    if ((src == 1 && !usb) || (src == 0 && !bt)) return;
    float gain = tp_cook.fx.live_gain;
    for (unsigned i = 0; i < frames; ++i) {
        int32_t m = 0;
        if (bt && src != 1) m += ((int32_t)bt[2 * i] + bt[2 * i + 1]) >> 1;
        if (usb && src != 0) m += (usb[2 * i] + usb[2 * i + 1]) >> 9;
        live_pair += m;
        if (!(live_odd ^= 1U)) {
            int16_t s = tp_sat16((float)live_pair * 0.5f * gain);
            live_pair = 0;
            uint32_t w = live_write;
            live[w & live_mask] = s;
            live_write = (w + 1U) & live_mask;
            uint16_t a = (uint16_t)(s < 0 ? -(int32_t)s : s);
            if (a > live_px_peak) live_px_peak = a;
            if (++live_px_count >= LIVE_PX) {
                live_peaks[live_px] = (uint8_t)(live_px_peak >> 7);
                live_px_peak = 0; live_px_count = 0;
                if (++live_px >= 160U) live_px = 0;
            }
        }
    }
}
unsigned tempura_live_head(void) { return (unsigned)((uint64_t)live_write * 160U / live_len); }

/* =========================================================== cooking */
static float norm(int v) { return (float)v * (1.0f / 127.0f); }
void tempura_engine_cook(const struct tp_canvas *c)
{
    struct tp_cooked k;
    memset(&k, 0, sizeof k);
    const float block = (float)FM1_BLOCK_FRAMES;
    for (unsigned e = 0; e < TP_EMITTERS; ++e) {
        const int16_t *p = c->ep[e];
        struct tp_cooked_emitter *o = &k.e[e];
        o->rate_hz = 0.5f * tp_exp2((float)p[EP_RATE] * (8.6f / 127.0f));
        o->size_s = 0.004f * tp_exp2((float)p[EP_SIZE] * (9.0f / 127.0f));
        o->shape = (uint8_t)p[EP_SHAPE];
        o->reverse_p = (float)p[EP_REVERSE] * 0.01f;
        o->pos_offset = (float)p[EP_POS] * (1.0f / 64.0f);
        o->spray_y = norm(p[EP_SPRAY_Y]) * norm(p[EP_SPRAY_Y]) * 4.0f;
        o->spray_x = norm(p[EP_SPRAY_X]) * norm(p[EP_SPRAY_X]) * 3.5f;
        float scan = (float)p[EP_SCAN] * (1.0f / 64.0f);
        o->scan_cells_s = scan * (scan < 0 ? -scan : scan) * 4.0f;
        o->pitch_semis = (float)(p[EP_OCTAVE] * 12 + p[EP_SEMI] + c->g[G_ROOT]) + (float)p[EP_FINE] * 0.01f;
        o->quant = (uint8_t)p[EP_QUANT];
        o->pitch_spray = o->quant >= 2 ? norm(p[EP_PITCH_SPRAY]) : norm(p[EP_PITCH_SPRAY]) * norm(p[EP_PITCH_SPRAY]) * 12.0f;
        o->level = norm(p[EP_LEVEL]) * norm(p[EP_LEVEL]) * 0.9f;
        o->pan = (float)p[EP_PAN] * (1.0f / 64.0f);
        o->pan_spray = norm(p[EP_PAN_SPRAY]);
        o->velo = norm(p[EP_VELO]);
        o->mode = (uint8_t)p[EP_MODE];
        o->align = (uint8_t)p[EP_ALIGN_X];
        float glide = 0.002f * tp_exp2((float)p[EP_GLIDE] * (10.0f / 127.0f));
        o->glide_coef = p[EP_GLIDE] ? tp_exp(-4.6f * block / (glide * 44100.0f)) : 0.0f;
        float attack = 0.001f * tp_exp2((float)p[EP_ATTACK] * (13.0f / 127.0f));
        o->attack_inc = 1.0f / (attack * 44100.0f);
        float decay = 0.005f * tp_exp2((float)p[EP_DECAY] * (11.0f / 127.0f));
        float release = 0.005f * tp_exp2((float)p[EP_RELEASE] * (11.0f / 127.0f));
        o->decay_coef = tp_exp(-6.9f * block / (decay * 44100.0f));
        o->release_coef = tp_exp(-6.9f * block / (release * 44100.0f));
        o->sustain = norm(p[EP_SUSTAIN]);
        o->filter = (uint8_t)p[EP_FILTER];
        o->cutoff_oct = norm(p[EP_CUTOFF]) * 9.8f;
        o->res_q = 0.5f + norm(p[EP_RES]) * norm(p[EP_RES]) * 12.0f;
        o->env_filter_oct = (float)p[EP_ENV_FILTER] * (5.0f / 64.0f);
        o->lfo_shape = (uint8_t)p[EP_LFO_SHAPE];
        o->lfo_rate = 0.02f * tp_exp2((float)p[EP_LFO_RATE] * (10.0f / 127.0f));
        o->lfo_depth = norm(p[EP_LFO_DEPTH]);
        o->lfo_dest = (uint8_t)p[EP_LFO_DEST];
        o->send_mod = norm(p[EP_SEND_MOD]);
        o->send_delay = norm(p[EP_SEND_DELAY]);
        o->send_reverb = norm(p[EP_SEND_REVERB]);
        o->dry = norm(p[EP_DRY]);
        unsigned placed = 0;
        for (unsigned t = 0; t < TP_TRACKS; ++t) { o->cells[t] = c->cells[e][t]; placed += bit_count8(c->cells[e][t]); }
        o->placed = (uint8_t)placed;
    }
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        const int16_t *p = c->tr[t];
        struct tp_cooked_track *o = &k.t[t];
        o->source = (uint8_t)p[TR_SOURCE];
        o->level = norm(p[TR_LEVEL]) * norm(p[TR_LEVEL]) * 1.25f;
        o->pan = (float)p[TR_PAN] * (1.0f / 64.0f);
        o->tune_semis = (float)p[TR_TUNE] + (float)p[TR_FINE] * 0.01f;
        o->start = norm(p[TR_START]);
        o->end = norm(p[TR_END]);
        o->reverse = (uint8_t)p[TR_REVERSE];
    }
    struct tp_cooked_fx *f = &k.fx;
    const int16_t *g = c->g;
    f->mod_type = (uint8_t)g[G_MOD_TYPE];
    f->mod_rate = 0.05f * tp_exp2((float)g[G_MOD_RATE] * (8.0f / 127.0f));
    f->mod_depth = norm(g[G_MOD_DEPTH]);
    f->mod_return = norm(g[G_MOD_RETURN]);
    f->mod_feedback = norm(g[G_MOD_FEEDBACK]) * 0.9f;
    unsigned sync = (unsigned)g[G_DLY_SYNC];
    unsigned tempo = tempura_midi_clock_bpm();
    float beats = 60.0f / (float)(tempo ? tempo : (unsigned)g[G_BPM]);
    static const float division[9] = {0.0f, 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.0f};
    f->dly_seconds = sync && sync < 9 ? beats * division[sync] : 0.02f * tp_exp2((float)g[G_DLY_TIME] * (5.2f / 127.0f)); /* 20 ms .. 0.74 s */
    f->dly_feedback = norm(g[G_DLY_FEEDBACK]) * 0.97f;
    f->dly_tone = 400.0f * tp_exp2(norm(g[G_DLY_TONE]) * 5.3f);
    f->dly_return = norm(g[G_DLY_RETURN]);
    f->ping = (uint8_t)g[G_DLY_PING];
    f->rev_feedback = 0.70f + norm(g[G_REV_SIZE]) * 0.28f;
    f->rev_damp = norm(g[G_REV_DAMP]) * 0.5f;
    f->rev_width = norm(g[G_REV_WIDTH]);
    f->rev_return = norm(g[G_REV_RETURN]);
    f->volume = norm(g[G_VOLUME]) * norm(g[G_VOLUME]) * 3.2f;
    f->live_src = (uint8_t)g[G_LIVE_SRC];
    f->live_gain = norm(g[G_LIVE_GAIN]) * 2.0f;
    f->monitor = norm(g[G_LIVE_MONITOR]);
    f->freeze = (uint8_t)g[G_LIVE_FREEZE];
    f->at_dest = (uint8_t)g[G_AT_DEST];
    /* Word-sized copies: a block may see a mix of old and new values, never a torn float. */
    volatile tp_word *dst = (volatile tp_word *)(void *)&tp_cook;
    const tp_word *src = (const tp_word *)(const void *)&k;
    for (unsigned i = 0; i < sizeof k / 4U; ++i) dst[i] = src[i];
    tempura_set_bpm_q8((uint32_t)((60.0f / beats) * 256.0f));
}

/* ======================================================= visualisation */
unsigned tempura_grain_snapshot(struct tp_grain_view *out, unsigned max)
{
    unsigned n = 0;
    for (unsigned i = 0; i < TP_MAX_GRAINS && n < max; ++i) {
        const struct tp_grain *g = &grains[i];
        if (!g->active || g->delay) continue;
        float travelled = (float)(g->total - g->remain) * g->vis_dy;
        const struct tp_cooked_track *t = &tp_cook.t[g->col];
        float span = 0.0f;
        if (t->source < TP_SLOTS) span = (float)slots[t->source].frames;
        else span = (float)live_len;
        span *= (t->end - t->start);
        float y = g->vis_y0 + (span > 1.0f ? travelled / span : 0.0f);
        y -= (float)tp_floori(y);
        unsigned idx = g->phase >> 24;
        float w = window_table[g->shape][idx];
        out[n].x = (uint8_t)tp_clampf(((float)g->col + g->blend) * 32.0f + 16.0f, 0.0f, 255.0f);
        out[n].y = (uint8_t)(y * 255.0f);
        out[n].emitter = g->emitter;
        out[n].bright = (uint8_t)(tp_clampf(w, 0.0f, 1.0f) * 255.0f);
        ++n;
    }
    return n;
}
unsigned tempura_voice_mask(void)
{
    unsigned mask = 0;
    for (unsigned i = 0; i < TP_VOICES; ++i)
        if (voices[i].active && voices[i].stage != 2) mask |= 1U << voices[i].emitter;
    return mask;
}
void tempura_stats(struct tp_stats *s)
{
    uint32_t dly = 0, rev = 0;
    tp_fx_heap(&dly, &rev);
    s->cpu = stat_cpu; s->grains = (uint8_t)grain_count; s->grain_limit = (uint8_t)grain_limit;
    s->limiting = stat_limiting; s->overloads = stat_overloads; s->shed = stat_shed; s->starved = stat_starved;
    s->voices = stat_voices;
    s->heap_cache = (uint32_t)sizeof cache; s->heap_live = (uint32_t)sizeof live; s->heap_delay = dly; s->heap_reverb = rev;
    s->decodes = decode_count;
}

/* ===================================================== waveform peaks */
static uint8_t peaks[TP_TRACKS][160];
static struct peak_job {
    const uint8_t *data;
    int16_t source, start, end;
    uint32_t chunk, first, last, frame0, span;
    uint8_t done;
} jobs[TP_TRACKS];
static int16_t peak_pcm[CHUNK];
void tempura_peaks_poll(const struct tp_canvas *c)
{
    unsigned budget = 12;
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        struct peak_job *j = &jobs[t];
        int16_t src = c->tr[t][TR_SOURCE];
        const uint8_t *data = src < TP_SLOTS ? slots[src].data : NULL;
        if (j->source != src || j->start != c->tr[t][TR_START] || j->end != c->tr[t][TR_END] || j->data != data) {
            j->source = src; j->start = c->tr[t][TR_START]; j->end = c->tr[t][TR_END]; j->data = data;
            memset(peaks[t], 0, sizeof peaks[t]);
            j->done = 1;
            if (data) {
                uint32_t frames = slots[src].frames;
                j->frame0 = (uint32_t)((float)frames * norm(j->start));
                uint32_t frame1 = (uint32_t)((float)frames * norm(j->end));
                j->span = frame1 > j->frame0 ? frame1 - j->frame0 : 1U;
                j->first = j->frame0 / CHUNK; j->last = (frame1 + CHUNK - 1U) / CHUNK;
                if (j->last > slots[src].chunks) j->last = slots[src].chunks;
                j->chunk = j->first; j->done = 0;
            }
        }
        if (src >= TP_SLOTS) {
            /* Live input: map the ring's running overview into the region. */
            for (unsigned p = 0; p < 160; ++p) {
                unsigned q = (unsigned)(((float)j->start + (float)p * (float)(j->end - j->start) / 160.0f) * 160.0f / 127.0f);
                peaks[t][p] = live_peaks[q < 160 ? q : 159];
            }
            continue;
        }
        while (!j->done && budget) {
            --budget;
            if (j->chunk >= j->last || fm1_sea_decode(data + j->chunk * CHUNK_BYTES, CHUNK_BYTES, 5, tp_sea_dqt, peak_pcm)) { j->done = 1; break; }
            uint32_t base = j->chunk * CHUNK;
            for (unsigned i = 0; i < CHUNK; i += 2) {
                uint32_t f = base + i;
                if (f < j->frame0) continue;
                uint32_t px = (f - j->frame0) * 160U / j->span;
                if (px >= 160) break;
                int a = peak_pcm[i] < 0 ? -peak_pcm[i] : peak_pcm[i];
                uint8_t v = (uint8_t)(a >> 7 > 255 ? 255 : a >> 7);
                if (v > peaks[t][px]) peaks[t][px] = v;
            }
            ++j->chunk;
        }
    }
}
uint8_t tempura_track_peak(unsigned track, unsigned pixel)
{
    return track < TP_TRACKS && pixel < 160 ? peaks[track][pixel] : 0;
}
