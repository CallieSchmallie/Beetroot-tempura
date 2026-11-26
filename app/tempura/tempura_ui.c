/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: panel controls and screen.
 *
 *   SELECT knob (E0)  cursor track        MAIN knob (E6)   cursor row
 *   PAGE knob (E1)    page in section     K1-K4 (E2-E5)    parameters
 *   REC   place / remove emitter on cursor cell (hold + move to paint)
 *   SEQ   next emitter (hold SEQ + first 4 white keys: pick E1-E4)
 *   PLAY  drone on/off for the selected emitter (REC+PLAY: freeze live)
 *   ARP   arpeggiator page (press again: next page; hold 0.5 s: on/off)
 *   OCT- / OCT+  octave; both together: PLAY <-> CELLS keyboard
 *   HOME canvas  EDIT grain  ENV envelope  LFO filter/LFO  FX effects
 *   SCL  track   GLO settings/Bluetooth/system   SAVE canvases
 *   Each section draws its own picture of what its knobs change
 *   (tempura_views.inc); HOME shows the canvas.
 *   HOME+PLAY    panic
 */
/* Control-task code: optimise for size (the FM-1 app area is about 444 KB). */
#pragma clang attribute push (__attribute__((minsize)), apply_to = function)
#include "tempura.h"
#include "tempura_internal.h"
#include "tempura_gfx.h"
#include "instrument.h"
#include <string.h>

enum { B_OCT_DOWN, B_OCT_UP, B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC };
#define BTN(b) (1U << (b))
static const uint8_t white_keys[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

/* ---- colours ---- */
#define C_BG       RGB(0, 0, 0)
#define C_CELL0    RGB(16, 14, 20)
#define C_CELL1    RGB(22, 19, 27)
#define C_WAVE     RGB(58, 54, 72)
#define C_TEXT     RGB(236, 230, 226)
#define C_DIM      RGB(120, 112, 124)
#define C_PANEL    RGB(24, 18, 26)
#define C_BEET     RGB(196, 30, 92)
#define C_AMBER    RGB(255, 168, 40)
#define C_GREEN    RGB(80, 220, 120)
static const uint16_t emitter_color[TP_EMITTERS] = {
    RGB(255, 94, 77), RGB(255, 204, 48), RGB(48, 214, 192), RGB(172, 116, 255)
};

/* ---- geometry ---- */
enum { GX = 4, CW = 29, GY = 18, CH = 20, PANEL_Y = 180 };

/* ---- pages ---- */
enum { K_EMITTER, K_TRACK, K_GLOBAL, K_BLE, K_SYSTEM, K_CANVAS };
struct page { const char *title; uint8_t kind, view; int8_t p[4]; };
enum { S_HOME, S_EDIT, S_ENV, S_LFO, S_FX, S_TRACK, S_SET, S_CANVAS, S_ARP, SECTIONS };
enum {
    V_GRID, V_GRAIN, V_POSITION, V_PITCH, V_MIX, V_MODE, V_ENV, V_FILTER, V_LFO,
    V_SENDS, V_MODFX, V_DELAY, V_REVERB, V_TRACK, V_ARP
};
static const struct page home_pages[] = {
    {"PLAY", K_EMITTER, V_GRID, {EP_RATE, EP_SIZE, EP_SPRAY_Y, EP_SEMI}},
};
static const struct page edit_pages[] = {
    {"GRAIN", K_EMITTER, V_GRAIN, {EP_RATE, EP_SIZE, EP_SHAPE, EP_REVERSE}},
    {"POSITION", K_EMITTER, V_POSITION, {EP_POS, EP_SPRAY_Y, EP_SPRAY_X, EP_SCAN}},
    {"PITCH", K_EMITTER, V_PITCH, {EP_OCTAVE, EP_SEMI, EP_FINE, EP_PITCH_SPRAY}},
    {"MIX", K_EMITTER, V_MIX, {EP_LEVEL, EP_PAN, EP_PAN_SPRAY, EP_VELO}},
    {"PLAY MODE", K_EMITTER, V_MODE, {EP_MODE, EP_QUANT, EP_ALIGN_X, EP_GLIDE}},
};
static const struct page env_pages[] = {
    {"ENVELOPE", K_EMITTER, V_ENV, {EP_ATTACK, EP_DECAY, EP_SUSTAIN, EP_RELEASE}},
};
static const struct page lfo_pages[] = {
    {"FILTER", K_EMITTER, V_FILTER, {EP_FILTER, EP_CUTOFF, EP_RES, EP_ENV_FILTER}},
    {"LFO", K_EMITTER, V_LFO, {EP_LFO_SHAPE, EP_LFO_RATE, EP_LFO_DEPTH, EP_LFO_DEST}},
};
static const struct page fx_pages[] = {
    {"SENDS", K_EMITTER, V_SENDS, {EP_SEND_MOD, EP_SEND_DELAY, EP_SEND_REVERB, EP_DRY}},
    {"MODULATION", K_GLOBAL, V_MODFX, {G_MOD_TYPE, G_MOD_RATE, G_MOD_DEPTH, G_MOD_FEEDBACK}},
    {"DELAY", K_GLOBAL, V_DELAY, {G_DLY_TIME, G_DLY_FEEDBACK, G_DLY_TONE, G_DLY_SYNC}},
    {"DELAY+MOD", K_GLOBAL, V_DELAY, {G_DLY_PING, G_BPM, G_DLY_RETURN, G_MOD_RETURN}},
    {"REVERB", K_GLOBAL, V_REVERB, {G_REV_SIZE, G_REV_DAMP, G_REV_WIDTH, G_REV_RETURN}},
};
static const struct page track_pages[] = {
    {"TRACK", K_TRACK, V_TRACK, {TR_SOURCE, TR_LEVEL, TR_TUNE, TR_FINE}},
    {"TRACK", K_TRACK, V_TRACK, {TR_PAN, TR_REVERSE, TR_START, TR_END}},
};
static const struct page set_pages[] = {
    {"KEYBOARD", K_GLOBAL, V_GRID, {G_KEY_MODE, G_KEY_LAYER, G_ROOT, G_VOLUME}},
    {"MIDI", K_GLOBAL, V_GRID, {G_MIDI_MODE, G_CELL_CH, G_KEY_OUT, G_AT_DEST}},
    {"LIVE INPUT", K_GLOBAL, V_GRID, {G_LIVE_SRC, G_LIVE_GAIN, G_LIVE_MONITOR, G_LIVE_FREEZE}},
    {"BLUETOOTH", K_BLE, V_GRID, {-1, -1, -1, -1}},
    {"SYSTEM", K_SYSTEM, V_GRID, {-1, -1, -1, -1}},
};
static const struct page canvas_pages[] = {
    {"CANVASES", K_CANVAS, V_GRID, {-1, -1, -1, -1}},
};
static const struct page arp_pages[] = {
    {"ARP", K_GLOBAL, V_ARP, {G_ARP_ON, G_ARP_MODE, G_ARP_RATE, G_ARP_OCT}},
    {"ARP FEEL", K_GLOBAL, V_ARP, {G_ARP_GATE, G_ARP_LATCH, G_ARP_SWING, G_ARP_VELO}},
};
static const struct { const struct page *pages; uint8_t count; } sections[SECTIONS] = {
    {home_pages, 1}, {edit_pages, 5}, {env_pages, 1}, {lfo_pages, 2},
    {fx_pages, 5}, {track_pages, 2}, {set_pages, 5}, {canvas_pages, 1}, {arp_pages, 2},
};

/* ---- state ---- */
static struct tp_canvas canvas;
static unsigned canvas_dirty, canvas_slot, booted, sel, cur_x, cur_y;
static unsigned section, page_of[SECTIONS];
static int octave;
static uint32_t prev_keys, prev_buttons, boot_ms, frame_ms;
static uint8_t key_note[27], key_mask[27], key_cell[27];
static uint8_t drone[TP_EMITTERS];
static uint32_t press_ms[14];
static int8_t enc_dir[7];
static uint32_t enc_ms[7];
static char notice[28];
static uint32_t notice_until;
static unsigned save_armed;
static char slot_names[TP_SLOTS][13];
static uint8_t slot_valid[TP_SLOTS];
static unsigned canvas_cursor, name_pos;
static unsigned ble_index, forget_armed;
static uint8_t bt_audio_on = 1;
static struct tp_grain_view grain_view[TP_MAX_GRAINS];
static unsigned grain_views;
static unsigned painting;

struct tp_canvas *tempura_canvas(void) { return &canvas; }
unsigned tempura_ui_selected(void) { return sel; }

static void say(const char *text)
{
    size_t n = strlen(text);
    if (n > sizeof notice - 1) n = sizeof notice - 1;
    memcpy(notice, text, n); notice[n] = 0;
    notice_until = fm1_millis() + 1600U;
}
static void changed(void) { canvas_dirty = 1; }

static const struct page *current_page(void)
{
    return &sections[section].pages[page_of[section] % sections[section].count];
}

/* ---------------------------------------------------------------- keys */
static void key_out(uint8_t status, uint8_t note, uint8_t vel)
{
    uint8_t m[3] = {status, note, vel};
    tempura_midi_key_out(m, 3);
}
static void key_down(unsigned k)
{
    key_mask[k] = 0; key_cell[k] = 255;
    if (canvas.g[G_KEY_MODE] == KEYS_CELLS) {
        unsigned w = 16;
        for (unsigned i = 0; i < 16; ++i) if (white_keys[i] == k) w = i;
        if (w == 16) return;
        unsigned row = (cur_y + (w >= 8 ? 1U : 0U)) & 7U, track = w & 7U;
        key_cell[k] = (uint8_t)(track * 8U + row);
        key_note[k] = 60;
        key_mask[k] = (uint8_t)(1U << sel);
        (void)tempura_note_on(sel, 60, 100, key_cell[k], 0);
        key_out((uint8_t)(0x90U | (canvas.g[G_MIDI_MODE] ? sel : 0U)), (uint8_t)(36U + key_cell[k]), 100);
        return;
    }
    int note = 53 + (int)k + octave * 12;
    if (note < 0 || note > 127) return;
    key_note[k] = (uint8_t)note;
    unsigned mask = 1U << sel;
    if (canvas.g[G_KEY_LAYER]) {
        mask = 0;
        for (unsigned e = 0; e < TP_EMITTERS; ++e)
            for (unsigned t = 0; t < TP_TRACKS; ++t) if (canvas.cells[e][t]) { mask |= 1U << e; break; }
        if (!mask) mask = 1U << sel;
    }
    key_mask[k] = (uint8_t)mask;
    for (unsigned e = 0; e < TP_EMITTERS; ++e) if (mask & (1U << e)) (void)tempura_note_on(e, (unsigned)note, 100, 255, 0);
    key_out((uint8_t)(0x90U | (canvas.g[G_MIDI_MODE] ? sel : 0U)), (uint8_t)note, 100);
}
static void key_up(unsigned k)
{
    if (!key_mask[k]) return;
    for (unsigned e = 0; e < TP_EMITTERS; ++e)
        if (key_mask[k] & (1U << e)) (void)tempura_note_off(e, key_note[k], key_cell[k], 0);
    unsigned e = 0;
    while (e < 3 && !(key_mask[k] & (1U << e))) ++e;
    key_out((uint8_t)(0x80U | (canvas.g[G_MIDI_MODE] ? e : 0U)), key_cell[k] < 64 ? (uint8_t)(36U + key_cell[k]) : key_note[k], 0);
    key_mask[k] = 0;
}
static void release_all_keys(void)
{
    for (unsigned k = 0; k < 27; ++k) if (prev_keys & (1U << k)) key_up(k);
}

/* ------------------------------------------------------------- canvases */
static void refresh_slot_names(void)
{
    for (unsigned i = 0; i < TP_SLOTS; ++i) {
        memset(slot_names[i], 0, sizeof slot_names[i]);
        slot_valid[i] = tempura_store_name(i, slot_names[i]) == 0;
        if (!slot_valid[i]) memcpy(slot_names[i], "- EMPTY -", 10);
    }
}
static void load_canvas(unsigned slot, int quiet)
{
    struct tp_canvas next;
    int r = tempura_store_load(slot, &next);
    if (r) { if (!quiet) say(r > 0 ? "SLOT IS EMPTY" : "LOAD FAILED"); return; }
    tempura_all_off();
    memset(drone, 0, sizeof drone);
    canvas = next;
    canvas_slot = slot;
    changed();
    if (!quiet) { char m[28] = "LOADED "; memcpy(m + 7, canvas.name, 12); m[19] = 0; say(m); }
}
unsigned tempura_ui_program_change(unsigned slot)
{
    load_canvas(slot % TP_SLOTS, 0);
    return 0;
}

/* ------------------------------------------------------------- editing */
static int16_t *param_ref(const struct page *pg, unsigned i, const struct tp_param_info **info)
{
    int p = pg->p[i];
    if (p < 0) { *info = NULL; return NULL; }
    if (pg->kind == K_EMITTER) { *info = tempura_emitter_info((unsigned)p); return &canvas.ep[sel][p]; }
    if (pg->kind == K_TRACK) { *info = tempura_track_info((unsigned)p); return &canvas.tr[cur_x][p]; }
    if (pg->kind == K_GLOBAL) { *info = tempura_global_info((unsigned)p); return &canvas.g[p]; }
    *info = NULL;
    return NULL;
}
static int accelerate(unsigned enc, int delta, uint32_t now, int range)
{
    int dir = (delta > 0) - (delta < 0);
    unsigned fast = dir == enc_dir[enc] && now - enc_ms[enc] < 60U;
    enc_dir[enc] = (int8_t)dir; enc_ms[enc] = now;
    if (range > 40 && fast) return delta * 4;
    return delta;
}
static void edit_param(unsigned knob, int delta, uint32_t now)
{
    const struct page *pg = current_page();
    const struct tp_param_info *info;
    int16_t *v = param_ref(pg, knob, &info);
    if (!v || !info) return;
    int d = accelerate(2U + knob, delta, now, info->max - info->min);
    int x = *v + d;
    if (x < info->min) x = info->min;
    if (x > info->max) x = info->max;
    if (pg->kind == K_TRACK && pg->p[knob] == TR_START && x >= canvas.tr[cur_x][TR_END]) x = canvas.tr[cur_x][TR_END] - 1;
    if (pg->kind == K_TRACK && pg->p[knob] == TR_END && x <= canvas.tr[cur_x][TR_START]) x = canvas.tr[cur_x][TR_START] + 1;
    if (x != *v) {
        if (pg->kind == K_GLOBAL && pg->p[knob] == G_KEY_MODE) release_all_keys();
        *v = (int16_t)x;
        changed();
    }
}
static void toggle_cell(unsigned set_only)
{
    uint8_t bit = (uint8_t)(1U << cur_y);
    if (set_only) canvas.cells[sel][cur_x] |= bit;
    else canvas.cells[sel][cur_x] ^= bit;
    changed();
}

/* ------------------------------------------------------------ bluetooth */
static void ble_controls(const struct fm1_controls *c, uint32_t pressed)
{
    struct fm1_ble_status st;
    fm1_ble_get_status(&st);
    if (c->encoders[2]) {
        int r = fm1_ble_set_role(c->encoders[2] > 0 ? FM1_BLE_CENTRAL : FM1_BLE_PERIPHERAL);
        if (r == -2) say("BLUETOOTH BUSY");
        ble_index = 0;
    }
    if (c->encoders[3]) {
        bt_audio_on = c->encoders[3] > 0;
        (void)fm1_bluetooth_configure(FM1_BT_MIDI | (bt_audio_on ? FM1_BT_AUDIO : 0U));
    }
    if (c->encoders[5] > 0) { forget_armed = 1; say("REC: FORGET ALL PAIRINGS"); }
    if (c->encoders[5] < 0) forget_armed = 0;
    int move = c->encoders[0] + c->encoders[6];
    if (move && st.count) {
        int i = (int)ble_index + move;
        if (i < 0) i = 0;
        if (i >= st.count) i = st.count - 1;
        ble_index = (unsigned)i;
    }
    if (pressed & BTN(B_REC)) {
        if (forget_armed) { forget_armed = 0; say(fm1_ble_forget_bonds() ? "BUSY - TRY AGAIN" : "PAIRINGS FORGOTTEN"); }
        else if (st.role == FM1_BLE_CENTRAL) say(fm1_ble_scan() ? "SCAN NOT READY" : "SCANNING 15 S");
        else say("SWITCH K1 TO CENTRAL TO SCAN");
    }
    if (pressed & BTN(B_PLAY)) say(fm1_ble_connect(ble_index) ? "CANNOT CONNECT" : "CONNECTING");
    if (pressed & BTN(B_ARP)) say(fm1_ble_disconnect() ? "NOT CONNECTED" : "DISCONNECTING");
}

/* --------------------------------------------------------------- canvas */
static void canvas_controls(const struct fm1_controls *c, uint32_t pressed)
{
    int move = c->encoders[0] + c->encoders[6];
    if (move) {
        int i = (int)canvas_cursor + move;
        canvas_cursor = (unsigned)(i < 0 ? 0 : i >= TP_SLOTS ? TP_SLOTS - 1 : i);
        save_armed = 0;
    }
    if (c->encoders[2]) {
        int p = (int)name_pos + c->encoders[2];
        name_pos = (unsigned)(p < 0 ? 0 : p > 11 ? 11 : p);
    }
    if (c->encoders[3]) {
        static const char chars[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+#&*!?";
        const char *at = strchr(chars, canvas.name[name_pos]);
        int idx = at ? (int)(at - chars) : 0, n = (int)strlen(chars);
        idx = ((idx + c->encoders[3]) % n + n) % n;
        canvas.name[name_pos] = chars[idx];
        changed();
    }
    if (pressed & BTN(B_SAVE)) {
        if (!save_armed) { save_armed = 1; say("PRESS SAVE AGAIN TO STORE"); }
        else {
            save_armed = 0;
            int r = tempura_store_save(canvas_cursor, &canvas);
            if (!r) { canvas_slot = canvas_cursor; refresh_slot_names(); say("CANVAS SAVED"); }
            else say("SAVE FAILED");
        }
    }
    if (pressed & BTN(B_PLAY)) { save_armed = 0; load_canvas(canvas_cursor, 0); }
    if (pressed & BTN(B_REC)) {
        tempura_all_off(); memset(drone, 0, sizeof drone);
        tempura_canvas_default(&canvas);
        memset(canvas.cells, 0, sizeof canvas.cells);
        memcpy(canvas.name, "NEW CANVAS  ", 12);
        changed(); say("NEW EMPTY CANVAS");
    }
}

/* ----------------------------------------------------------- main poll */
static void open_section(unsigned s)
{
    if (section == s) page_of[s] = (page_of[s] + 1U) % sections[s].count;
    else section = s;
    save_armed = 0; forget_armed = 0;
    if (s == S_CANVAS) { refresh_slot_names(); canvas_cursor = canvas_slot; }
}
static void handle_buttons(const struct fm1_controls *c, uint32_t pressed, uint32_t released, uint32_t now)
{
    uint32_t held = c->buttons;
    for (unsigned b = 0; b < 14; ++b) if (pressed & BTN(b)) press_ms[b] = now;

    if ((held & BTN(B_HOME)) && (pressed & BTN(B_PLAY))) {
        tempura_panic(); memset(drone, 0, sizeof drone); say("ALL SOUND OFF"); return;
    }
    if ((pressed & (BTN(B_OCT_DOWN) | BTN(B_OCT_UP))) && (held & BTN(B_OCT_DOWN)) && (held & BTN(B_OCT_UP))) {
        release_all_keys();
        canvas.g[G_KEY_MODE] = canvas.g[G_KEY_MODE] ? KEYS_PLAY : KEYS_CELLS;
        changed();
        say(canvas.g[G_KEY_MODE] ? "KEYS PLAY CELLS" : "KEYS PLAY NOTES");
        return;
    }
    if ((released & BTN(B_OCT_DOWN)) && !(held & BTN(B_OCT_UP)) && !(prev_buttons & BTN(B_OCT_UP)) && octave > -3) { --octave; }
    if ((released & BTN(B_OCT_UP)) && !(held & BTN(B_OCT_DOWN)) && !(prev_buttons & BTN(B_OCT_DOWN)) && octave < 3) { ++octave; }

    if (pressed & BTN(B_HOME)) { section = S_HOME; save_armed = 0; }
    if (pressed & BTN(B_EDIT)) open_section(S_EDIT);
    if (pressed & BTN(B_ENV)) open_section(S_ENV);
    if (pressed & BTN(B_LFO)) open_section(S_LFO);
    if (pressed & BTN(B_FX)) open_section(S_FX);
    if (pressed & BTN(B_SCL)) open_section(S_TRACK);
    if (pressed & BTN(B_GLO)) open_section(S_SET);
    /* ARP: a tap opens (then pages through) the arpeggiator; holding it for
     * half a second switches the arpeggiator on or off from any section. */
    if ((released & BTN(B_ARP)) && now - press_ms[B_ARP] >= 500U) {
        release_all_keys();
        canvas.g[G_ARP_ON] = !canvas.g[G_ARP_ON];
        changed();
        say(canvas.g[G_ARP_ON] ? "ARPEGGIATOR ON" : "ARPEGGIATOR OFF");
    } else if (released & BTN(B_ARP)) open_section(S_ARP);
    if ((pressed & BTN(B_SAVE)) && section != S_CANVAS) { open_section(S_CANVAS); return; }

    const struct page *pg = current_page();
    if (pg->kind == K_BLE) { ble_controls(c, pressed); return; }
    if (pg->kind == K_CANVAS) { canvas_controls(c, pressed); return; }

    /* SEQ: next emitter on release unless used as a modifier. */
    if ((released & BTN(B_SEQ)) && now - press_ms[B_SEQ] < 400U && !(c->keys | prev_keys)) {
        sel = (sel + 1U) % TP_EMITTERS;
    }
    if ((held & BTN(B_REC)) && (pressed & BTN(B_PLAY))) {
        canvas.g[G_LIVE_FREEZE] = !canvas.g[G_LIVE_FREEZE];
        changed();
        say(canvas.g[G_LIVE_FREEZE] ? "LIVE INPUT FROZEN" : "LIVE INPUT RECORDING");
        return;
    }
    if (pressed & BTN(B_REC)) { toggle_cell(0); painting = (canvas.cells[sel][cur_x] >> cur_y) & 1U; }
    if (released & BTN(B_REC)) painting = 0;
    if ((pressed & BTN(B_PLAY)) && !(held & BTN(B_REC))) {
        drone[sel] = !drone[sel];
        if (drone[sel]) (void)tempura_note_on(sel, 60, 100, 255, 2);
        else (void)tempura_note_off(sel, 60, 255, 2);
    }
}

static void handle_encoders(const struct fm1_controls *c, uint32_t now)
{
    const struct page *pg = current_page();
    if (pg->kind == K_BLE || pg->kind == K_CANVAS) return;
    if (c->encoders[0] || c->encoders[6]) {
        int x = (int)cur_x + c->encoders[0], y = (int)cur_y + c->encoders[6];
        cur_x = (unsigned)(x < 0 ? 0 : x > 7 ? 7 : x);
        cur_y = (unsigned)(y < 0 ? 0 : y > 7 ? 7 : y);
        if (painting) toggle_cell(1);
    }
    if (c->encoders[1]) {
        int n = sections[section].count;
        page_of[section] = (unsigned)((((int)page_of[section] + c->encoders[1]) % n + n) % n);
    }
    for (unsigned k = 0; k < 4; ++k) if (c->encoders[2 + k]) edit_param(k, c->encoders[2 + k], now);
}

static void handle_keys(const struct fm1_controls *c)
{
    uint32_t down = c->keys & ~prev_keys, up = prev_keys & ~c->keys;
    for (unsigned k = 0; k < 27; ++k) {
        if (up & (1U << k)) key_up(k);
        if (!(down & (1U << k))) continue;
        if (c->buttons & BTN(B_SEQ)) {
            for (unsigned i = 0; i < TP_EMITTERS; ++i) if (white_keys[i] == k) sel = i;
            continue;
        }
        key_down(k);
    }
}

static void update_leds(void)
{
    const struct page *pg = current_page();
    static uint32_t shown = 0xffffffffU, shown_keys = 0xffffffffU;
    uint32_t b = 0;
    static const uint8_t section_button[SECTIONS] = {B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SCL, B_GLO, B_SAVE, B_ARP};
    b |= BTN(section_button[section]);
    if (drone[sel]) b |= BTN(B_PLAY);
    if ((canvas.cells[sel][cur_x] >> cur_y) & 1U) b |= BTN(B_REC);
    if (canvas.g[G_ARP_ON] && ((fm1_millis() >> 8) & 1U || section == S_ARP)) b |= BTN(B_ARP);  /* blinks while on */
    if (octave < 0) b |= BTN(B_OCT_DOWN);
    if (octave > 0) b |= BTN(B_OCT_UP);
    if (pg->kind == K_EMITTER) b |= BTN(B_SEQ);
    uint32_t keys = prev_keys;
    if (canvas.g[G_KEY_MODE] == KEYS_CELLS)
        for (unsigned w = 0; w < 16; ++w) {
            unsigned row = (cur_y + (w >= 8 ? 1U : 0U)) & 7U;
            if ((canvas.cells[sel][w & 7U] >> row) & 1U) keys |= 1U << white_keys[w];
        }
    if (b != shown) { for (unsigned i = 0; i < 14; ++i) if ((b ^ shown) & BTN(i)) fm1_led_set(i, (b >> i) & 1U); shown = b; }
    if (keys != shown_keys) { for (unsigned i = 0; i < 27; ++i) if ((keys ^ shown_keys) & (1U << i)) fm1_led_set(14U + i, (keys >> i) & 1U); shown_keys = keys; }
}

/* ============================================================== drawing */
static void draw_header(void)
{
    if (gfx_top >= 16) return;
    gfx_vgrad(0, 0, GFX_W, 16, RGB(34, 14, 34), RGB(10, 4, 12));
    unsigned voices = tempura_voice_mask();
    for (unsigned e = 0; e < TP_EMITTERS; ++e) {
        int x = 2 + (int)e * 24;
        char label[3] = {'E', (char)('1' + e), 0};
        if (e == sel) { gfx_rfill(x, 2, 22, 12, 3, emitter_color[e]); gfx_hline(x + 3, 3, 16, gfx_mix(emitter_color[e], C_TEXT, 140)); gfx_text(x + 5, 4, label, 1, C_BG); }
        else { gfx_rfill(x, 2, 22, 12, 3, gfx_mix(C_BG, emitter_color[e], 60)); gfx_text(x + 5, 4, label, 1, emitter_color[e]); }
        if (voices & (1U << e)) gfx_fill(x + 18, 4, 2, 2, e == sel ? C_BG : C_TEXT);
        if (drone[e]) gfx_fill(x + 18, 9, 2, 2, e == sel ? C_BG : emitter_color[e]);
    }
    char name[13]; memcpy(name, canvas.name, 12); name[12] = 0;
    gfx_label(100, 4, name, C_TEXT);
    char right[8];
    if (canvas.g[G_KEY_MODE] == KEYS_CELLS) memcpy(right, "CEL", 4);
    else { right[0] = 'O'; right[1] = octave < 0 ? '-' : '+'; right[2] = (char)('0' + (octave < 0 ? -octave : octave)); right[3] = 0; }
    gfx_text(176, 4, right, 1, C_DIM);
    struct fm1_ble_status ble; fm1_ble_get_status(&ble);
    struct fm1_bluetooth_status bt; fm1_bluetooth_get_status(&bt);
    uint16_t bc = ble.state == FM1_BLE_READY || ble.state == FM1_BLE_CONNECTED ? C_GREEN :
                  ble.state == FM1_BLE_OFF ? RGB(60, 56, 64) : C_DIM;
    gfx_text(198, 4, "B", 1, bc);
    gfx_text(205, 4, "A", 1, bt.audio_playing ? C_GREEN : bt.audio_connected ? C_DIM : RGB(60, 56, 64));
    if (tempura_midi_activity()) gfx_fill(213, 6, 3, 3, C_AMBER);
    struct tp_stats st; tempura_stats(&st);
    unsigned cpu = st.cpu > 100 ? 100 : st.cpu;
    /* Outline turns amber while the output limiter works, red when it works hard. */
    gfx_rect(219, 4, 19, 7, st.limiting == 2 ? C_BEET : st.limiting ? C_AMBER : C_DIM);
    gfx_fill(220, 5, (int)(cpu * 17U / 100U), 5, cpu > 80 ? C_BEET : C_GREEN);
}

static uint16_t cell_bg(unsigned t, unsigned r)
{
    uint16_t bg = ((r + t) & 1U) ? C_CELL1 : C_CELL0;
    if ((canvas.cells[sel][t] >> r) & 1U) bg = gfx_mix(bg, emitter_color[sel], 52);
    return bg;
}
static void draw_grid(void)
{
    if (gfx_top + GFX_BAND <= GY - 2 || gfx_top >= GY + CH * TP_ROWS + 2) return;
    gfx_fill(0, GY - 2, GFX_W, CH * TP_ROWS + 4, C_BG);
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        int x = GX + (int)t * CW;
        int16_t source = canvas.tr[t][TR_SOURCE];
        unsigned empty = source < TP_SLOTS ? !tempura_slot_frames((unsigned)source) : 0;
        for (int y = gfx_top; y < gfx_top + GFX_BAND; ++y) {
            if (y < GY || y >= GY + CH * TP_ROWS) continue;
            unsigned r = (unsigned)(y - GY) / CH, in = (unsigned)(y - GY) % CH;
            if (in == CH - 1) continue;
            gfx_hline(x, y, CW - 1, in == 0 ? gfx_mix(cell_bg(t, r), C_TEXT, 22) : cell_bg(t, r));
            if (!empty) {
                unsigned peak = tempura_track_peak(t, (unsigned)(y - GY));
                int half = (int)(peak * 13U / 255U);
                gfx_hline(x + 14 - half, y, half * 2 + 1, source == TP_SOURCE_LIVE ? RGB(70, 120, 110) : C_WAVE);
            }
        }
        if (empty && gfx_top <= GY + 4 && gfx_top + GFX_BAND > GY + 2) gfx_text(x + 8, GY + 2, "-", 1, C_DIM);
        if (source == TP_SOURCE_LIVE) gfx_hline(x, GY + (int)tempura_live_head(), CW - 1, canvas.g[G_LIVE_FREEZE] ? C_DIM : C_BEET);
        for (unsigned r = 0; r < TP_ROWS; ++r) {
            int y = GY + (int)r * CH;
            if (y + CH <= gfx_top || y >= gfx_top + GFX_BAND) continue;
            static const int8_t ox[4] = {2, 21, 2, 21}, oy[4] = {2, 2, 12, 12};
            for (unsigned e = 0; e < TP_EMITTERS; ++e)
                if ((canvas.cells[e][t] >> r) & 1U) gfx_fill(x + ox[e], y + oy[e], 5, 5, emitter_color[e]);
            if ((canvas.cells[sel][t] >> r) & 1U) gfx_rect(x, y, CW - 1, CH - 1, emitter_color[sel]);
        }
    }
    for (unsigned i = 0; i < grain_views; ++i) {
        const struct tp_grain_view *g = &grain_view[i];
        int col = (g->x - 16) / 32, frac = (g->x - 16) % 32;
        int px = GX + col * CW + 13 + frac * CW / 32;
        int py = GY + (int)g->y * (CH * TP_ROWS) / 256;
        uint16_t c = gfx_mix(C_CELL0, emitter_color[g->emitter], 110U + g->bright * 145U / 255U);
        if (g->bright > 90) {
            gfx_fill(px - 2, py - 1, 4, 2, c);
            gfx_fill(px - 1, py - 2, 2, 4, c);
            gfx_fill(px - 1, py - 1, 2, 2, gfx_mix(c, C_TEXT, 120));
        } else gfx_fill(px - 1, py - 1, 2, 2, c);
    }
    /* Nothing to granulate: say so instead of staying silent. */
    unsigned sources = 0;
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        int16_t src = canvas.tr[t][TR_SOURCE];
        if (src == TP_SOURCE_LIVE || tempura_slot_frames((unsigned)src)) ++sources;
    }
    if (!sources) {
        gfx_fill(8, GY + 58, GFX_W - 16, 44, C_PANEL);
        gfx_rect(8, GY + 58, GFX_W - 16, 44, C_BEET);
        gfx_text(120 - gfx_text_width("NO SOUNDS LOADED", 1) / 2, GY + 66, "NO SOUNDS LOADED", 1, C_AMBER);
        gfx_text(120 - gfx_text_width("FLASHER PAGE: LOAD SOUNDS", 1) / 2, GY + 78, "FLASHER PAGE: LOAD SOUNDS", 1, C_TEXT);
        gfx_text(120 - gfx_text_width("OR SCL: SOURCE LIVE IN", 1) / 2, GY + 90, "OR SCL: SOURCE LIVE IN", 1, C_DIM);
    }
    int cx = GX + (int)cur_x * CW - 1, cy = GY + (int)cur_y * CH - 1;
    uint16_t cc = painting ? C_BEET : C_TEXT;
    gfx_rect(cx, cy, CW + 1, CH + 1, cc);
}

static void knob(unsigned col, const char *label, const char *value, uint16_t color, int fill255)
{
    int x = 4 + (int)col * 59;
    gfx_label(x, 196, label, RGB(150, 130, 150));
    if (gfx_title_width(value, 0) <= 54) gfx_title(x, 207, value, gfx_mix(color, C_TEXT, 90), gfx_mix(color, C_BG, 70), GFX_SHADOW);
    else gfx_label(x, 212, value, color);
    gfx_rfill(x, 229, 54, 5, 2, RGB(44, 30, 46));
    if (fill255 >= 0) {
        int w = fill255 * 54 / 255;
        if (w > 1) { gfx_rfill(x, 229, w, 5, 2, color); gfx_hline(x + 2, 230, w > 4 ? w - 4 : 1, gfx_mix(color, C_TEXT, 150)); }
        gfx_fill(x + w - 1 < x ? x : x + w - 1, 227, 2, 9, C_TEXT);
    }
}
/* 8x8 section icons, one byte per row, bit 7 on the left. */
static const uint8_t section_icon[SECTIONS][8] = {
    {0xee, 0xaa, 0xee, 0x00, 0xee, 0xaa, 0xee, 0x00},   /* HOME: canvas */
    {0x10, 0x44, 0x00, 0x92, 0x00, 0x44, 0x10, 0x00},   /* EDIT: grains */
    {0x20, 0x30, 0x58, 0x4e, 0x42, 0x81, 0x81, 0x00},   /* ENV */
    {0x60, 0x90, 0x90, 0x09, 0x09, 0x06, 0x00, 0x00},   /* LFO */
    {0x10, 0x54, 0x38, 0xfe, 0x38, 0x54, 0x10, 0x00},   /* FX */
    {0x10, 0x52, 0x5a, 0xff, 0x5a, 0x52, 0x10, 0x00},   /* SCL: waveform */
    {0x18, 0x5a, 0x24, 0xc3, 0xc3, 0x24, 0x5a, 0x18},   /* GLO: gear */
    {0xfe, 0x82, 0xfe, 0x82, 0xba, 0xba, 0xfe, 0x00},   /* SAVE: disk */
    {0x03, 0x03, 0x0c, 0x0c, 0x30, 0x30, 0xc0, 0xc0},   /* ARP: steps */
};
static void draw_title(const char *title, uint16_t color, const char *right)
{
    for (int r = 0; r < 8; ++r)
        for (int b = 0; b < 8; ++b) if (section_icon[section][r] & (0x80U >> b)) gfx_fill(4 + b, 183 + r, 1, 1, color);
    if (notice[0] && (int32_t)(fm1_millis() - notice_until) < 0) { gfx_label(16, 184, notice, C_AMBER); return; }
    gfx_label(16, 184, title, color);
    if (right) gfx_label(236 - gfx_label_width(right), 184, right, RGB(150, 130, 150));
}
static void draw_params(const struct page *pg)
{
    char title[28] = {0}, right[16] = {0};
    uint16_t color = C_TEXT;
    if (pg->kind == K_EMITTER) {
        title[0] = 'E'; title[1] = (char)('1' + sel); title[2] = ' ';
        strncpy(title + 3, pg->title, sizeof title - 4);
        color = emitter_color[sel];
    } else if (pg->kind == K_TRACK) {
        title[0] = 'T'; title[1] = (char)('1' + cur_x); title[2] = ' ';
        strncpy(title + 3, pg->title, sizeof title - 4);
        int16_t s = canvas.tr[cur_x][TR_SOURCE];
        strncpy(right, s < TP_SLOTS ? tempura_slot_name((unsigned)s) : "LIVE IN", 15);
    } else strncpy(title, pg->title, sizeof title - 1);
    unsigned count = sections[section].count;
    if (count > 1) {
        size_t n = strlen(title);
        title[n] = ' '; title[n + 1] = (char)('1' + page_of[section] % count); title[n + 2] = '/'; title[n + 3] = (char)('0' + count); title[n + 4] = 0;
    }
    draw_title(title, color, right[0] ? right : NULL);
    for (unsigned i = 0; i < 4; ++i) {
        const struct tp_param_info *info;
        int16_t *v = param_ref(pg, i, &info);
        if (!v) continue;
        char value[12];
        tempura_format(info, *v, value);
        int fill = info->max > info->min ? (*v - info->min) * 255 / (info->max - info->min) : 0;
        knob(i, info->name, value, color, fill);
    }
}

static void draw_overlay_box(void)
{
    gfx_fill(0, GY - 2, GFX_W, CH * TP_ROWS + 4, C_PANEL);
    gfx_hline(0, GY - 2, GFX_W, C_BEET);
}
static const char *ble_state_name(unsigned s)
{
    static const char *const names[] = {"OFF", "IDLE", "ADVERTISING", "SCANNING", "CONNECTING", "DISCOVERING", "CONNECTED", "READY", "DISCONNECTING"};
    return s < 9 ? names[s] : "?";
}
static void draw_ble(void)
{
    struct fm1_ble_status st; fm1_ble_get_status(&st);
    struct fm1_bluetooth_status bt; fm1_bluetooth_get_status(&bt);
    if (gfx_top < PANEL_Y) {
        draw_overlay_box();
        gfx_text(8, 24, "BLE MIDI", 2, C_TEXT);
        gfx_text(8, 46, st.role == FM1_BLE_CENTRAL ? "ROLE  CENTRAL (FM-1 CONNECTS)" : "ROLE  PERIPHERAL (PHONE CONNECTS)", 1, C_DIM);
        gfx_text(8, 58, "STATE", 1, C_DIM);
        gfx_text(44, 58, ble_state_name(st.state), 1, st.state == FM1_BLE_READY ? C_GREEN : C_TEXT);
        gfx_text(8, 70, "BT AUDIO", 1, C_DIM);
        gfx_text(62, 70, !bt_audio_on ? "OFF" : bt.audio_playing ? "PLAYING -> LIVE IN" : bt.audio_connected ? "CONNECTED" : "WAITING", 1,
                 bt.audio_playing ? C_GREEN : C_TEXT);
        if (st.role == FM1_BLE_CENTRAL) {
            for (unsigned i = 0; i < st.count && i < 6; ++i) {
                struct fm1_ble_device d;
                if (fm1_ble_get_device(i, &d)) continue;
                int y = 88 + (int)i * 12;
                if (i == ble_index) gfx_fill(4, y - 2, 232, 11, RGB(60, 30, 50));
                gfx_text(8, y, d.name[0] ? d.name : "UNNAMED", 1, d.midi ? C_TEXT : C_DIM);
            }
            if (!st.count) gfx_text(8, 92, "REC: SCAN FOR BLE MIDI DEVICES", 1, C_DIM);
        } else {
            gfx_text(8, 92, "PHONE: CONNECT A BLE MIDI APP", 1, C_DIM);
            gfx_text(8, 104, "BT AUDIO FEEDS THE LIVE INPUT", 1, C_DIM);
        }
        gfx_text(8, 164, "REC SCAN  PLAY CONNECT  ARP DROP", 1, C_DIM);
        return;
    }
    draw_title("BLUETOOTH", C_TEXT, NULL);
    knob(0, "ROLE", st.role == FM1_BLE_CENTRAL ? "CENT" : "PERI", C_TEXT, -1);
    knob(1, "BT AUDIO", bt_audio_on ? "ON" : "OFF", C_TEXT, -1);
    knob(3, "FORGET", forget_armed ? "REC!" : "TURN", forget_armed ? C_BEET : C_DIM, -1);
}
static void put_kb(char *out, const char *label, uint32_t bytes)
{
    char num[8]; unsigned kb = bytes / 1024U, n = 0; char tmp[8];
    do { tmp[n++] = (char)('0' + kb % 10U); kb /= 10U; } while (kb);
    unsigned k = 0; while (n) num[k++] = tmp[--n]; num[k] = 0;
    strcpy(out, label); strcat(out, num); strcat(out, "K");
}
static void draw_system(void)
{
    struct tp_stats st; tempura_stats(&st);
    if (gfx_top < PANEL_Y) {
        draw_overlay_box();
        gfx_text(8, 24, TEMPURA_NAME, 2, C_BEET);
        gfx_text(8, 44, "VERSION " TEMPURA_VERSION " ON FM1 OPEN SDK", 1, C_DIM);
        char line[32];
        put_kb(line, "GRAIN CACHE  ", st.heap_cache); gfx_text(8, 62, line, 1, C_TEXT);
        put_kb(line, "LIVE BUFFER  ", st.heap_live); gfx_text(8, 74, line, 1, st.heap_live ? C_TEXT : C_BEET);
        put_kb(line, "DELAY        ", st.heap_delay); gfx_text(8, 86, line, 1, st.heap_delay ? C_TEXT : C_BEET);
        put_kb(line, "REVERB       ", st.heap_reverb); gfx_text(8, 98, line, 1, st.heap_reverb ? C_TEXT : C_BEET);
        char n[12];
        strcpy(line, "GRAINS ");
        tempura_format(tempura_global_info(G_VOLUME), st.grains, n); strcat(line, n); strcat(line, " / ");
        tempura_format(tempura_global_info(G_VOLUME), st.grain_limit, n); strcat(line, n);
        gfx_text(8, 116, line, 1, C_TEXT);
        strcpy(line, "CPU ");
        tempura_format(tempura_global_info(G_VOLUME), st.cpu, n); strcat(line, n); strcat(line, "%  OVERLOADS ");
        tempura_format(tempura_global_info(G_VOLUME), st.overloads, n); strcat(line, n);
        gfx_text(8, 128, line, 1, st.cpu > 80 ? C_BEET : C_TEXT);
        unsigned clock = tempura_midi_clock_bpm();
        strcpy(line, "MIDI CLOCK ");
        if (clock) { tempura_format(tempura_global_info(G_VOLUME), (int)clock, n); strcat(line, n); strcat(line, " BPM"); }
        else strcat(line, "NONE");
        gfx_text(8, 140, line, 1, C_TEXT);
        gfx_text(8, 160, "INSPIRED BY BEETLECRAB TEMPERA", 1, C_DIM);
        return;
    }
    draw_title("SYSTEM", C_TEXT, NULL);
}
static void draw_canvas_page(void)
{
    if (gfx_top < PANEL_Y) {
        draw_overlay_box();
        gfx_text(8, 22, "CANVASES", 1, C_BEET);
        for (unsigned i = 0; i < TP_SLOTS; ++i) {
            int y = 36 + (int)i * 16;
            if (i == canvas_cursor) gfx_fill(4, y - 3, 232, 14, RGB(70, 26, 52));
            char label[4] = {(char)('1' + i), ' ', 0};
            gfx_text(10, y, label, 1, C_DIM);
            gfx_text(26, y, slot_names[i], 1, slot_valid[i] ? C_TEXT : C_DIM);
            if (i == canvas_slot) gfx_text(194, y, "CURRENT", 1, C_GREEN);
        }
        return;
    }
    draw_title(save_armed ? "PRESS SAVE AGAIN TO STORE" : "SAVE STORE  PLAY LOAD  REC NEW", save_armed ? C_AMBER : C_TEXT, NULL);
    char name[13]; memcpy(name, canvas.name, 12); name[12] = 0;
    gfx_text(4, 200, "NAME", 1, C_DIM);
    gfx_text(40, 198, name, 2, C_TEXT);
    gfx_fill(40 + (int)name_pos * 12, 216, 10, 2, C_BEET);
    gfx_text(4, 226, "K1 CURSOR  K2 LETTER", 1, C_DIM);
}

#include "tempura_views.inc"

static void draw(void)
{
    const struct page *pg = current_page();
    static unsigned frame_view;
    if (gfx_top == 0) { frame_view = pg->view; view_prepare(frame_view); }
    draw_header();
    if (pg->kind == K_BLE) { if (gfx_top >= GY - 2) draw_ble(); }
    else if (pg->kind == K_SYSTEM) { if (gfx_top >= GY - 2) draw_system(); }
    else if (pg->kind == K_CANVAS) { if (gfx_top >= GY - 2) draw_canvas_page(); }
    else if (frame_view != V_GRID) draw_view(frame_view);
    else draw_grid();
    if (gfx_top + GFX_BAND > PANEL_Y - 2) {
        if (gfx_top < PANEL_Y) gfx_fill(0, PANEL_Y - 2, GFX_W, 2, C_BG);
        gfx_vgrad(0, PANEL_Y, GFX_W, GFX_H - PANEL_Y, RGB(36, 16, 36), RGB(10, 5, 12));
        for (int x = 0; x < GFX_W; ++x) gfx_fill(x, PANEL_Y, 1, 1, gfx_mix(C_BEET, C_AMBER, (unsigned)x * 255U / GFX_W));
        if (pg->kind == K_BLE) draw_ble();
        else if (pg->kind == K_SYSTEM) draw_system();
        else if (pg->kind == K_CANVAS) draw_canvas_page();
        else draw_params(pg);
    }
}

/* ---- splash: a lit, battered beetroot, drawn per pixel ---- */
static float sp_hash(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393U + (uint32_t)y * 668265263U;
    h = (h ^ (h >> 13)) * 1274126177U;
    return (float)((h ^ (h >> 16)) & 1023U) * (1.0f / 1023.0f);
}
static uint16_t sp_rgb(float r, float g, float b)
{
    r = r < 0 ? 0 : r > 1 ? 1 : r; g = g < 0 ? 0 : g > 1 ? 1 : g; b = b < 0 ? 0 : b > 1 ? 1 : b;
    return RGB((unsigned)(r * 255.0f), (unsigned)(g * 255.0f), (unsigned)(b * 255.0f));
}
static __attribute__((noinline)) float sp_sin(float x) { return tp_sin1(x); }
static float sp_sqrt(float x) { if (x <= 0.0f) return 0.0f; float r = x > 1.0f ? x : 1.0f; for (int i = 0; i < 6; ++i) r = 0.5f * (r + x / r); return r; }
enum { BEET_X = 120, BEET_Y = 94, BEET_R = 34 };
/* Leaves: base, direction (unit), length, half width. */
static const struct { int8_t bx; float dx, dy, len, wid; } leaves[5] = {
    {-8, -0.92f, -0.40f, 50.0f, 13.0f}, {8, 0.94f, -0.35f, 48.0f, 12.0f},
    {-4, -0.52f, -0.85f, 60.0f, 16.0f}, {4, 0.58f, -0.81f, 58.0f, 15.0f}, {0, 0.04f, -1.0f, 66.0f, 17.0f},
};
/* Returns 1 and the colour when (x, y) is on a leaf or stem. */
static int sp_leaf(int x, int y, float *r, float *g, float *b)
{
    int hit = 0;
    for (unsigned i = 0; i < 5; ++i) {
        float px = (float)(x - BEET_X - leaves[i].bx), py = (float)(y - (BEET_Y - BEET_R + 4));
        float a = px * leaves[i].dx + py * leaves[i].dy, c = -px * leaves[i].dy + py * leaves[i].dx;
        float ac = c < 0 ? -c : c;
        if (a < 0.0f || a > leaves[i].len) continue;
        if (a < 16.0f) {                           /* red stem */
            if (ac < 2.4f - a * 0.05f) { float l = 0.55f + 0.45f * (c > 0 ? 1.0f : 0.3f); *r = 0.78f * l; *g = 0.08f * l; *b = 0.32f * l; hit = 1; }
            continue;
        }
        float t = (a - 12.0f) / (leaves[i].len - 12.0f);
        float hw = leaves[i].wid * 4.0f * t * (1.0f - t) * (1.15f - 0.35f * t) * (1.0f + 0.10f * sp_sin(a * 0.16f + c * 0.05f));
        if (ac >= hw) continue;
        float side = c / hw;                        /* -1..1 across the blade */
        float l = 0.35f + 0.35f * side + 0.45f * t * (1.0f - side * side);   /* lit, folded blade */
        float gr = 0.04f + 0.30f * l, gg = 0.16f + 0.62f * l, gb = 0.06f + 0.22f * l;
        float vein = a * 0.16f - ac * 0.11f;
        vein -= (float)(int)vein;
        if (ac < 0.9f) { gr = 0.72f; gg = 0.10f; gb = 0.30f; }                  /* red midrib */
        else if (vein < 0.08f) { gr = gr * 0.5f + 0.32f; gg *= 0.75f; gb = gb * 0.5f + 0.12f; } /* red side veins */
        if (hw - ac < 1.0f) { gr *= 0.6f; gg *= 0.6f; gb *= 0.6f; }
        *r = gr; *g = gg; *b = gb; hit = 1;
    }
    return hit;
}
static void boot_splash(void)
{
    for (int y = gfx_top; y < gfx_top + GFX_BAND; ++y) {
        uint16_t *row = gfx_px + (y - gfx_top) * GFX_W;
        for (int x = 0; x < GFX_W; ++x) {
            float dx = (float)(x - BEET_X), dy = (float)(y - BEET_Y);
            float dist = sp_sqrt(dx * dx + dy * dy), d = dist * (1.0f / 160.0f);
            /* Background: deep plum glow, light rays from behind the beet, a halo. */
            float r = 0.20f - 0.18f * d, g = 0.025f - 0.02f * d, b = 0.17f - 0.14f * d;
            float adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            float ang = adx + ady > 0.0f ? (dy < 0 ? dx / (adx + ady) : 2.0f - dx / (adx + ady)) : 0.0f;
            float ray = (0.5f + 0.5f * sp_sin(ang * 3.5f)) * (1.0f - d) * 0.10f;
            r += ray; g += ray * 0.45f; b += ray * 0.5f;
            float halo = (dist - BEET_R) * (1.0f / 16.0f);
            halo = halo > 0.0f && halo < 3.0f ? 0.35f * (1.0f - halo / 3.0f) * (1.0f - halo / 3.0f) : 0.0f;
            r += halo * 0.9f; g += halo * 0.12f; b += halo * 0.45f;
            if (sp_hash(x, y) > 0.9965f) { r += 0.5f; g += 0.35f; b += 0.3f; }   /* drifting grains */

            float lr, lg, lb;
            if (sp_leaf(x, y, &lr, &lg, &lb)) { r = lr; g = lg; b = lb; }

            /* Body: round on top, tapering to a tail. */
            float u = dx * (1.0f / BEET_R), v = dy * (1.0f / BEET_R), w;
            if (v < -1.0f || v > 1.75f) w = 0.0f;
            else if (v <= 0.3f) w = sp_sqrt(1.0f - v * v);
            else { float t = (1.75f - v) * (1.0f / 1.45f); w = 0.954f * t * sp_sqrt(t); }
            float au = u < 0 ? -u : u;
            float edge = 0.40f + 0.06f * sp_sin(u * 2.1f) + 0.06f * sp_hash(x >> 2, 7);   /* where the batter starts */
            int battered = v > edge && v < 1.5f;
            float wb = battered ? w + 0.07f + 0.05f * sp_hash(x >> 1, y >> 1) : w;   /* crumbs widen the outline */
            if (w > 0.0f && au < wb) {
                float nx = au < w ? u / w : (u < 0 ? -1.0f : 1.0f), ny = v < 0.3f ? v : 0.3f;
                float nz = sp_sqrt(1.0f - nx * nx) * sp_sqrt(1.0f - ny * ny);
                float diff = -0.55f * nx - 0.55f * ny + 0.63f * nz;
                diff = diff < 0.0f ? 0.0f : diff;
                if (battered) {
                    /* Tempura: golden, lumpy, with bright crisp bits and dark pits. */
                    /* Lumps: two crossed ripples, lit from the upper left. */
                    float fx = (float)x, fy = (float)y;
                    float h0 = sp_sin(fx * 0.085f + sp_sin(fy * 0.05f) * 0.3f) * sp_sin(fy * 0.09f + sp_sin(fx * 0.04f) * 0.3f);
                    float h1 = sp_sin((fx - 1.5f) * 0.085f + sp_sin((fy - 1.5f) * 0.05f) * 0.3f) * sp_sin((fy - 1.5f) * 0.09f + sp_sin((fx - 1.5f) * 0.04f) * 0.3f);
                    h0 = h0 < 0 ? -h0 : h0; h1 = h1 < 0 ? -h1 : h1;   /* every lump bulges out */
                    float lump = (h0 - h1) * 1.6f;          /* facing the light (upper left) */
                    float n = sp_hash(x, y + 77);
                    float l = 0.42f + 0.45f * diff + lump * 0.30f + 0.18f * h0 + 0.08f * (n - 0.5f);
                    if (v < edge + 0.05f) l *= 0.7f;
                    r = 1.05f * l; g = 0.68f * l; b = 0.22f * l;
                    if (lump > 0.25f && n > 0.55f) { r += 0.22f; g += 0.20f; b += 0.14f; }   /* crisp glints */
                    if (h0 < 0.08f) { r *= 0.7f; g *= 0.65f; b *= 0.6f; }                  /* creases between lumps */
                } else {
                    float ring = 1.0f - 0.07f * sp_sin(v * 4.2f + 0.3f * sp_sin(u * 0.5f));
                    float l = (0.25f + 0.80f * diff) * ring;
                    r = 0.80f * l; g = 0.07f * l; b = 0.32f * l;
                    float spec = diff - 0.80f;
                    if (spec > 0.0f) { spec *= 6.0f; r += spec * 0.9f; g += spec * 0.6f; b += spec * 0.75f; }
                    float rim = nx > 0.0f ? nx * nx * nx * nx * nx : 0.0f;          /* purple back light */
                    r += rim * 0.75f; g += rim * 0.16f; b += rim * 0.95f;
                    if (sp_hash(x, y + 500) > 0.985f) { r *= 0.55f; g *= 0.55f; b *= 0.55f; }
                    if (w - au < 0.05f) { r *= 0.55f; g *= 0.5f; b *= 0.55f; }
                }
            }
            /* The root tail pokes out of the batter. */
            if (y >= BEET_Y + 50 && y < BEET_Y + 74) {
                int t = y - (BEET_Y + 50);
                int rx = BEET_X + (int)(4.0f * sp_sin((float)t * 0.045f));
                if (x == rx || (t < 8 && x == rx + 1)) { r = 0.55f; g = 0.06f; b = 0.22f; }
            }
            row[x] = sp_rgb(r, g, b);
        }
    }
    int tw = gfx_title_width("BEETROOT", GFX_BOLD | GFX_ITALIC);
    gfx_title(120 - tw / 2, 170, "BEETROOT", RGB(255, 140, 190), RGB(190, 20, 84), GFX_BOLD | GFX_ITALIC | GFX_SHADOW);
    tw = gfx_title_width("TEMPURA", GFX_BOLD | GFX_ITALIC);
    gfx_title(120 - tw / 2, 190, "TEMPURA", RGB(255, 236, 150), RGB(236, 120, 24), GFX_BOLD | GFX_ITALIC | GFX_SHADOW);
    gfx_hline(60, 210, 120, RGB(90, 30, 70)); gfx_fill(118, 209, 4, 3, C_AMBER);
    const char *tag = "GRANULAR CANVAS FOR FM-1";
    gfx_label(120 - gfx_label_width(tag) / 2, 215, tag, RGB(200, 180, 200));
    tag = "HOLD HOME FOR SAFE MODE  V" TEMPURA_VERSION;
    gfx_label(120 - gfx_label_width(tag) / 2, 228, tag, RGB(100, 88, 104));
}

static unsigned safe;
static void safe_screen(void)
{
    gfx_fill(0, gfx_top, GFX_W, GFX_BAND, C_BG);
    gfx_text(120 - gfx_text_width("SAFE MODE", 2) / 2, 60, "SAFE MODE", 2, C_AMBER);
    static const char *const lines[] = {
        "SOUND IS OFF. NOTHING IS SAVED.", "", "TO REINSTALL OR GO BACK:",
        "CONNECT USB AND OPEN THE", "FLASHER PAGE OR M-VAVE WORKBENCH.", "", "POWER OFF AND ON TO LEAVE.",
    };
    for (unsigned i = 0; i < sizeof lines / sizeof lines[0]; ++i)
        gfx_text(120 - gfx_text_width(lines[i], 1) / 2, 100 + (int)i * 14, lines[i], 1, i == 2 ? C_TEXT : C_DIM);
}

/* ================================================================ entry */
void tempura_ui_init(void)
{
    tempura_engine_init();
    tempura_canvas_default(&canvas);
    tempura_engine_cook(&canvas);
    gfx_init();
    booted = 0;
    boot_ms = fm1_millis();
    section = S_HOME;
    memset(page_of, 0, sizeof page_of);
    prev_keys = prev_buttons = 0;
    gfx_request_frame();
}

void tempura_ui_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    uint32_t now = fm1_millis();
    (void)groove_master_update(c.knob);

    if (safe) { gfx_pump(safe_screen, 8); return; }
    if (!booted) {
        gfx_pump(boot_splash, 8);
        if (c.buttons & BTN(B_HOME)) {
            /* Safe mode: no sound, no Tempura Bluetooth or canvas work. The
             * FM-1's own USB update path stays fully available for reflashing. */
            safe = 1;
            tempura_set_safe_mode(1);
            gfx_init();
            gfx_request_frame();
            return;
        }
        if (gfx_frame_busy() || now - boot_ms < 1200U) { prev_buttons = c.buttons; prev_keys = 0; return; }
        booted = 1;
        tempura_engine_allocate();
        (void)fm1_bluetooth_configure(FM1_BT_AUDIO | FM1_BT_MIDI);
        load_canvas(0, 1);
        refresh_slot_names();
        tempura_engine_cook(&canvas);
        gfx_init();
    }

    uint32_t pressed = c.buttons & ~prev_buttons, released = prev_buttons & ~c.buttons;
    handle_buttons(&c, pressed, released, now);
    handle_encoders(&c, now);
    handle_keys(&c);
    prev_buttons = c.buttons;
    prev_keys = c.keys;

    tempura_midi_poll(&canvas);
    if (canvas_dirty) { canvas_dirty = 0; tempura_canvas_sanitize(&canvas); tempura_engine_cook(&canvas); }
    tempura_peaks_poll(&canvas);
    update_leds();

    if (!gfx_frame_busy() && now - frame_ms >= 40U) {
        frame_ms = now;
        grain_views = tempura_grain_snapshot(grain_view, TP_MAX_GRAINS);
        gfx_request_frame();
    }
    gfx_pump(draw, 10);
}

/* MIDI-triggered canvas edits need a re-cook on the next poll. */
void tempura_ui_touch(void) { canvas_dirty = 1; }

#pragma clang attribute pop
