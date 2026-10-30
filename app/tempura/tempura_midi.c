/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: MIDI in (DIN, USB, BLE) and key out (USB, BLE).
 *
 * Notes: OMNI plays the selected emitter on any channel; CH 1-4 maps
 * channels 1-4 to emitters 1-4. The CELL channel (default 16) maps notes
 * 36..99 to the 64 cells (C2 = track 1 top row, one track per 8 notes).
 * CCs follow the selected (OMNI) or channel's emitter:
 *   1 LFO amount  7 level  10 pan  20 rate  21 size  22 spray Y  23 spray X
 *   24 position  25 scan  26 pitch spray  27 reverse  64 sustain  71 reso
 *   72 release  73 attack  74 cutoff  91 reverb  93 mod  94 delay  123 all off
 * Program change 0-7 loads canvases 1-8. MIDI clock sets the delay tempo.
 */
#include "tempura.h"
#include "tempura_dsp.h"
#include <string.h>


enum { QUEUE = 128 };
static struct { uint8_t m[3], n, source; uint32_t at; } queue[QUEUE];
static volatile unsigned q_write, q_read;
static uint32_t clock_last, clock_period_q8, activity_until;
static unsigned clock_count, clock_bpm;

void tempura_midi_in(unsigned source, const uint8_t *m, size_t n)
{
    if (!m || !n || n > 3 || m[0] < 0x80) return;
    if (m[0] == 0xfe) return; /* active sensing */
    unsigned w = q_write, next = (w + 1U) % QUEUE;
    if (next == q_read) return;
    memcpy(queue[w].m, m, n);
    queue[w].n = (uint8_t)n; queue[w].source = (uint8_t)source;
    queue[w].at = fm1_millis();
    TP_BARRIER();
    q_write = next;
    fm1_control_request();
}

static const struct { uint8_t cc, param; } cc_map[] = {
    {1, EP_LFO_DEPTH}, {7, EP_LEVEL}, {10, EP_PAN}, {20, EP_RATE}, {21, EP_SIZE},
    {22, EP_SPRAY_Y}, {23, EP_SPRAY_X}, {24, EP_POS}, {25, EP_SCAN}, {26, EP_PITCH_SPRAY},
    {27, EP_REVERSE}, {71, EP_RES}, {72, EP_RELEASE}, {73, EP_ATTACK}, {74, EP_CUTOFF},
    {91, EP_SEND_REVERB}, {93, EP_SEND_MOD}, {94, EP_SEND_DELAY},
};

static void clock_tick(uint32_t at)
{
    uint32_t d = at - clock_last;
    clock_last = at;
    if (d == 0 || d > 250U) { clock_count = 0; return; }
    /* Smoothed interval in 1/256 ms; 24 ticks per quarter note. */
    uint32_t q8 = d << 8;
    clock_period_q8 = clock_count < 4 ? q8 : clock_period_q8 + ((int32_t)(q8 - clock_period_q8) >> 3);
    if (++clock_count >= 24 && clock_period_q8) clock_bpm = (60000U * 256U) / (clock_period_q8 * 24U);
}
unsigned tempura_midi_clock_bpm(void)
{
    if (clock_count < 24 || fm1_millis() - clock_last > 1000U) return 0;
    return clock_bpm;
}
unsigned tempura_midi_activity(void) { return (int32_t)(fm1_millis() - activity_until) < 0; }

void tempura_midi_poll(struct tp_canvas *c)
{
    unsigned tempo_before = tempura_midi_clock_bpm();
    while (q_read != q_write) {
        TP_BARRIER();
        uint8_t m[3]; memcpy(m, queue[q_read].m, 3);
        unsigned n = queue[q_read].n; uint32_t at = queue[q_read].at;
        TP_BARRIER();
        q_read = (q_read + 1U) % QUEUE;

        if (m[0] == 0xf8) { clock_tick(at); continue; }
        if (m[0] >= 0xf0) continue;
        activity_until = fm1_millis() + 80U;
        unsigned status = m[0] & 0xf0U, ch = m[0] & 15U;
        unsigned cell_ch = (unsigned)c->g[G_CELL_CH];
        unsigned is_cell = cell_ch && ch + 1U == cell_ch;
        unsigned e;
        if (is_cell || !c->g[G_MIDI_MODE]) e = tempura_ui_selected();
        else if (ch < TP_EMITTERS) e = ch;
        else continue;
        if (e >= TP_EMITTERS) continue;

        if ((status == 0x90 || status == 0x80) && n == 3) {
            unsigned on = status == 0x90 && m[2];
            if (is_cell) {
                if (m[1] < 36 || m[1] > 99) continue;
                unsigned cell = m[1] - 36U;
                if (on) (void)tempura_note_on(e, 60, m[2], cell, 1);
                else (void)tempura_note_off(e, 60, cell, 1);
            } else {
                if (on) (void)tempura_note_on(e, m[1], m[2], 255, 1);
                else (void)tempura_note_off(e, m[1], 255, 1);
            }
        } else if (status == 0xb0 && n == 3) {
            if (m[1] == 64) { tempura_set_sustain(e, m[2] >= 64); continue; }
            if (m[1] == 123 || m[1] == 120) { tempura_all_off(); continue; }
            for (unsigned i = 0; i < sizeof cc_map / sizeof cc_map[0]; ++i) {
                if (cc_map[i].cc != m[1]) continue;
                const struct tp_param_info *info = tempura_emitter_info(cc_map[i].param);
                int v = info->min + ((info->max - info->min) * m[2] + 63) / 127;
                c->ep[e][cc_map[i].param] = (int16_t)v;
                tempura_ui_touch();
            }
        } else if (status == 0xe0 && n == 3) {
            int bend = ((int)m[1] | ((int)m[2] << 7)) - 8192;
            tempura_set_bend(e, bend * 2 * 256 / 8192);
        } else if (status == 0xd0 && n == 2) {
            tempura_set_pressure(e, m[1]);
        } else if (status == 0xa0 && n == 3) {
            tempura_set_pressure(e, m[2]);
        } else if (status == 0xc0 && n == 2) {
            (void)tempura_ui_program_change(m[1] % TP_SLOTS);
        }
    }
    if (tempura_midi_clock_bpm() != tempo_before) tempura_ui_touch();
}

void tempura_midi_key_out(const uint8_t *m, unsigned n)
{
    struct tp_canvas *c = tempura_canvas();
    unsigned ports = (unsigned)c->g[G_KEY_OUT];
    if ((ports & 1U) && (fm1_midi_ready_ports() & (1U << FM1_MIDI_USB))) (void)fm1_midi_send_port(FM1_MIDI_USB, m, n);
    if ((ports & 2U) && (fm1_midi_ready_ports() & (1U << FM1_MIDI_BLE))) (void)fm1_midi_send_port(FM1_MIDI_BLE, m, n);
}
