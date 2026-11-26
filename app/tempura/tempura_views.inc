/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Beetroot Tempura: one picture per page, drawn in the canvas area while a
 * section other than HOME is open. Included by tempura_ui.c (shares its
 * state). Curves are computed once per frame (view_prepare) and then drawn
 * band by band; values come from the engine's cooked parameters, so each
 * picture shows what the sound actually does. */

/* View ids (V_*) are declared with the page tables in tempura_ui.c. */
enum { VX0 = 8, VX1 = 232, VW = VX1 - VX0, VY0 = 22, VY1 = 174 };
#define C_GRIDL RGB(40, 32, 46)
#define C_TEAL  RGB(48, 214, 192)

/* Out-of-line math for drawing: the DSP versions are force-inlined at every
 * call, which costs code space the FM-1 doesn't have. */
TP_SMALL static __attribute__((noinline)) float vexp2(float x) { return tp_exp2(x); }
TP_SMALL static __attribute__((noinline)) float vsin1(float x) { return tp_sin1(x); }
TP_SMALL static float vexp(float x) { return vexp2(x * 1.442695041f); }
TP_SMALL static float vcos1(float x) { return vsin1(x + 0.25f); }
TP_SMALL static float vmax(float a, float b) { return a > b ? a : b; }
static int16_t plot_a[VW], plot_b[VW];
static uint16_t ticks[64];             /* grain starts (x | reversed << 12) */
static int16_t plot_c[VW];
static uint8_t plot_n;                 /* generic count prepared for the view */
static float view_f[8];                /* generic values prepared for the view */
static struct tp_arp_view arp_v;

TP_SMALL static int band_hits(int y0, int y1) { return gfx_top < y1 && gfx_top + GFX_BAND > y0; }
TP_SMALL static float vnorm(int v) { return (float)v * (1.0f / 127.0f); }
TP_SMALL static float vclamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float vlog2(float x);
TP_SMALL static float vsqrt(float x) { return x > 0.0f ? vexp2(0.5f * vlog2(x)) : 0.0f; }
/* log2 for drawing only: exponent bits plus a quadratic for the mantissa. */
TP_SMALL static float vlog2(float x)
{
    union { float f; uint32_t u; } v = {x > 1e-20f ? x : 1e-20f};
    float e = (float)(int)((v.u >> 23) & 255U) - 127.0f;
    v.u = (v.u & 0x007fffffU) | 0x3f800000U;
    float t = (v.f - 1.0f) / (v.f + 1.0f), t2 = t * t;    /* ln m = 2 atanh t */
    return e + 2.8853901f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * 0.14285714f)));
}
TP_SMALL static int vy(float v, float lo, float hi, int top, int bot)
{
    float t = vclamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    return bot - (int)(t * (float)(bot - top) + 0.5f);
}
TP_SMALL static uint32_t vhash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
TP_SMALL static float vrand(uint32_t x) { return (float)(vhash(x) & 0xffffU) * (1.0f / 65535.0f); }

/* Draw a prepared curve; base >= 0 also fills down (or up) to that row. */
TP_SMALL static void curve(const int16_t *y, int top, int bot, int base, uint16_t c)
{
    if (!band_hits(top - 1, bot + 2)) return;
    uint16_t f = gfx_mix(C_BG, c, 70);
    for (int i = 0; i < VW; ++i) {
        int a = y[i], b = i ? y[i - 1] : y[i];
        if (base >= 0) { int lo = a < base ? a : base, hi = a < base ? base : a; gfx_vline(VX0 + i, lo, hi - lo + 1, f); }
        int lo = a < b ? a : b, hi = a < b ? b : a;
        gfx_vline(VX0 + i, lo, hi - lo + 1, c);
    }
}
TP_SMALL static void ctext(int x, int y, const char *s, int scale, uint16_t c) { gfx_text(x - gfx_text_width(s, scale) / 2, y, s, scale, c); }
TP_SMALL static void rtext(int x, int y, const char *s, uint16_t c) { gfx_text(x - gfx_text_width(s, 1), y, s, 1, c); }
/* "2.3" style numbers for labels. */
TP_SMALL static void vnum(char *out, float v, int decimals, const char *unit)
{
    int neg = v < 0.0f; if (neg) v = -v;
    unsigned scale = decimals == 2 ? 100U : decimals == 1 ? 10U : 1U;
    unsigned n = (unsigned)(v * (float)scale + 0.5f), ip = n / scale, fp = n % scale, k = 0;
    char tmp[12]; unsigned t = 0;
    do { tmp[t++] = (char)('0' + ip % 10U); ip /= 10U; } while (ip && t < 8);
    if (neg) out[k++] = '-';
    while (t) out[k++] = tmp[--t];
    if (decimals) { out[k++] = '.'; if (decimals == 2) out[k++] = (char)('0' + fp / 10U); out[k++] = (char)('0' + fp % 10U); }
    out[k] = 0;
    if (unit) strcat(out, unit);
}
TP_SMALL static void seconds_label(char *out, float s)
{
    if (s < 1.0f) vnum(out, s * 1000.0f, 0, " MS"); else vnum(out, s, s < 10.0f ? 2 : 1, " S");
}
TP_SMALL static void hbar(int x, int y, int w, int h, float fill, uint16_t c)
{
    gfx_fill(x, y, w, h, C_GRIDL);
    int f = (int)(vclamp(fill, 0.0f, 1.0f) * (float)w + 0.5f);
    if (f) gfx_fill(x, y, f, h, c);
}
TP_SMALL static void vbar(int x, int top, int bot, int w, float fill, uint16_t c)
{
    gfx_fill(x, top, w, bot - top, C_GRIDL);
    int h = (int)(vclamp(fill, 0.0f, 1.0f) * (float)(bot - top) + 0.5f);
    if (h) gfx_fill(x, bot - h, w, h, c);
}
TP_SMALL static void dot(int x, int y, int r, uint16_t c) { gfx_fill(x - r, y - r, 2 * r + 1, 2 * r + 1, c); }
/* A soft, glowing dot. */
TP_SMALL static void glow(int x, int y, uint16_t c)
{
    for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) { int d = dx * dx + dy * dy; if (d <= 9) gfx_blend(x + dx, y + dy, c, d <= 2 ? 255U : (unsigned)(200 - d * 18)); }
}
/* A straight line, drawn as small squares (t = thickness). */
TP_SMALL static void seg(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    int lo = y0 < y1 ? y0 : y1, hi = y0 < y1 ? y1 : y0;
    if (!band_hits(lo - t, hi + t + 1)) return;
    int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    for (int i = 0; i <= n; ++i) gfx_fill(x0 + (n ? dx * i / n : 0), y0 + (n ? dy * i / n : 0), t, t, c);
}
/* Seconds since start, wrapped (for animation). */
TP_SMALL static float vtime(void) { return (float)(fm1_millis() % 600000U) * 0.001f; }
TP_SMALL static float vfrac(float x) { return x - (float)(int)x; }

TP_SMALL static float lfo_at(unsigned shape, float t)
{
    float ph = t - (float)(int)t;
    unsigned cyc = (unsigned)(int)t;
    if (shape == 0) return vsin1(ph);
    if (shape == 1) return ph < 0.5f ? ph * 4.0f - 1.0f : 3.0f - ph * 4.0f;
    if (shape == 2) return 1.0f - ph * 2.0f;
    if (shape == 3) return ph < 0.5f ? 1.0f : -1.0f;
    float a = vrand(cyc * 2654435761U + 7U) * 2.0f - 1.0f;
    if (shape == 4) return a;
    float b = vrand((cyc + 1U) * 2654435761U + 7U) * 2.0f - 1.0f;
    float s = ph * ph * (3.0f - 2.0f * ph);
    return a + (b - a) * s;
}

/* ---------------------------------------------------------- preparation */
TP_SMALL static void view_prepare(unsigned view)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    const int16_t *p = canvas.ep[sel];
    switch (view) {
    case V_GRAIN: {
        const float *w = tempura_window(ce->shape);
        for (int i = 0; i < VW; ++i) plot_a[i] = (int16_t)vy(w[i * 256 / (VW - 1)], 0.0f, 1.0f, VY0 + 8, VY0 + 58);
        float size = ce->size_s, rate = ce->rate_hz, overlap = size * rate;
        float span = vclamp(vmax(size * 3.0f, 6.0f / rate), 0.02f, 6.0f);
        view_f[0] = overlap; view_f[1] = span;
        /* Summed grain windows over the span: what the grain cloud's
         * loudness looks like. Very dense clouds are drawn flat. */
        float peak = 0.0f;
        static float sum[VW];
        for (int i = 0; i < VW; ++i) sum[i] = 0.0f;
        unsigned grains = (unsigned)(span * rate) + 1U;
        plot_n = 0;
        if (overlap <= 32.0f && grains <= 400U) {
            for (unsigned k = 0; k < grains; ++k) {
                float t0 = (float)k / rate;
                int x0 = (int)(t0 / span * (float)VW), x1 = (int)((t0 + size) / span * (float)VW);
                if (x0 >= VW) break;
                int rev = vrand(k * 40503U + 11U) < ce->reverse_p;
                for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < VW; ++x) {
                    float u = x1 > x0 ? (float)(x - x0) / (float)(x1 - x0) : 0.5f;
                    if (rev) u = 1.0f - u;
                    sum[x] += w[(int)(u * 256.0f)];
                }
                if (k < 64U) { ticks[k] = (uint16_t)((x0 < 0 ? 0 : x0) | (rev << 12)); plot_n = (uint8_t)(k + 1U); }
            }
            for (int i = 0; i < VW; ++i) peak = vmax(peak, sum[i]);
        }
        if (peak <= 0.0f) plot_n = 0;
        for (int i = 0; i < VW; ++i) plot_b[i] = (int16_t)vy(peak > 0.0f ? sum[i] / peak : 0.75f, 0.0f, 1.0f, VY0 + 84, VY1 - 12);
        break;
    }
    case V_ENV: {
        float a = 0.001f * vexp2((float)p[EP_ATTACK] * (13.0f / 127.0f));
        float d = 0.005f * vexp2((float)p[EP_DECAY] * (11.0f / 127.0f));
        float r = 0.005f * vexp2((float)p[EP_RELEASE] * (11.0f / 127.0f));
        float s = vnorm(p[EP_SUSTAIN]);
        float sa = vsqrt(a), sd = vsqrt(d), sr = vsqrt(r), tot = sa + sd + sr;
        float avail = (float)VW * 0.80f;
        int xa = (int)(avail * sa / tot), xd = xa + (int)(avail * sd / tot), xs = xd + VW / 5;
        view_f[0] = a; view_f[1] = d; view_f[2] = r; view_f[3] = s;
        view_f[4] = (float)xa; view_f[5] = (float)xd; view_f[6] = (float)xs;
        for (int i = 0; i < VW; ++i) {
            float e;
            if (i < xa) e = xa ? (float)i / (float)xa : 1.0f;
            else if (i < xd) e = s + (1.0f - s) * vexp(-6.9f * (float)(i - xa) / (float)(xd - xa > 0 ? xd - xa : 1));
            else if (i < xs) e = s;
            else e = s * vexp(-6.9f * (float)(i - xs) / (float)(VW - xs > 1 ? VW - xs : 1));
            plot_a[i] = (int16_t)vy(e, 0.0f, 1.0f, VY0 + 14, VY1 - 22);
        }
        break;
    }
    case V_FILTER: {
        float fc = 20.0f * vexp2(ce->cutoff_oct), q = ce->res_q;
        float fc2 = 20.0f * vexp2(vclamp(ce->cutoff_oct + ce->env_filter_oct, 0.0f, 10.0f));
        for (int pass = 0; pass < 2; ++pass) {
            float c = pass ? fc2 : fc;
            for (int i = 0; i < VW; ++i) {
                float f = 20.0f * vexp2(10.0f * (float)i / (float)VW), w = f / c, w2 = w * w;
                float den = (1.0f - w2) * (1.0f - w2) + w2 / (q * q);
                float h2 = ce->filter == 1 ? 1.0f / den : ce->filter == 2 ? (w2 / (q * q)) / den : ce->filter == 3 ? w2 * w2 / den : 1.0f;
                float db = 3.0103f * vlog2(h2);
                (pass ? plot_b : plot_a)[i] = (int16_t)vy(db, -30.0f, 18.0f, VY0 + 6, VY1 - 18);
            }
        }
        view_f[0] = fc; view_f[1] = fc2;
        break;
    }
    case V_LFO:
        for (int i = 0; i < VW; ++i) {
            float v = lfo_at(ce->lfo_shape, 2.0f * (float)i / (float)VW);
            plot_a[i] = (int16_t)vy(v * ce->lfo_depth, -1.0f, 1.0f, VY0 + 24, VY1 - 16);
            plot_b[i] = (int16_t)vy(v, -1.0f, 1.0f, VY0 + 24, VY1 - 16);
        }
        break;
    case V_MODFX: {
        float base, depth;
        if (fx->mod_type == 1) { base = 0.0015f; depth = 0.004f; }
        else if (fx->mod_type == 2) { base = 0.004f; depth = 0.0035f; }
        else { base = 0.012f; depth = 0.008f; }
        depth *= fx->mod_depth;
        float ph = vfrac(vtime() * fx->mod_rate);
        float d = base + depth * (0.5f + 0.5f * vsin1(ph));                 /* delay now (s) */
        float g = fx->mod_type == 1 ? 0.55f + 0.4f * fx->mod_feedback : 0.7f;
        for (int i = 0; i < VW; ++i) {
            float u = 2.0f * (float)i / (float)VW;
            plot_b[i] = (int16_t)vy((base + depth * (0.5f + 0.5f * vsin1(u))) * 1000.0f, 0.0f, 21.0f, VY0 + 92, VY1 - 14);
            plot_c[i] = (int16_t)vy((base + depth * (0.5f + 0.5f * vcos1(u))) * 1000.0f, 0.0f, 21.0f, VY0 + 92, VY1 - 14);
            float v;
            if (fx->mod_type == 2) v = vcos1(u) * 0.5f + 0.5f;               /* vibrato: pitch wobble */
            else {
                /* Comb filter of dry + delayed copy at today's delay: notches sweep with the LFO. */
                float f = 3000.0f * (float)i / (float)VW;                 /* linear 0..3 kHz: clean teeth */
                float m2 = 1.0f + g * g + 2.0f * g * vcos1(f * d);
                v = vclamp(1.0f + 0.25f * 3.0103f * vlog2(m2 / ((1.0f + g) * (1.0f + g))) / 6.0f, 0.0f, 1.0f);
            }
            plot_a[i] = (int16_t)vy(v, 0.0f, 1.0f, VY0 + 24, VY0 + 80);
        }
        view_f[0] = fx->mod_rate; view_f[1] = ph; view_f[2] = d * 1000.0f;
        break;
    }
    case V_REVERB: {
        float fb = fx->rev_feedback;
        float rt = 3.0f * 0.0305f / (-0.30103f * vlog2(fb));
        float span = vclamp(rt * 1.15f, 0.3f, 14.0f);
        view_f[0] = rt;
        for (int i = 0; i < VW; ++i) {
            float t = span * (float)i / (float)VW, env = vexp2(-9.966f * t / rt);
            float nl = 0.25f + 0.75f * vrand((uint32_t)i * 7919U), nr = 0.25f + 0.75f * vrand((uint32_t)i * 104729U + 3U);
            nr = nl + (nr - nl) * fx->rev_width;
            plot_a[i] = (int16_t)(env * nl * 24.0f);
            plot_b[i] = (int16_t)(env * nr * 24.0f);
        }
        break;
    }
    case V_ARP:
        tempura_arp_view(sel, &arp_v);
        if (!arp_v.count) {   /* nothing held: preview the pattern on a C major chord */
            static const uint8_t demo[3] = {60, 64, 67};
            arp_v.count = (uint8_t)tp_arp_sequence(demo, 3, (unsigned)canvas.g[G_ARP_MODE], (unsigned)canvas.g[G_ARP_OCT], arp_v.notes, sizeof arp_v.notes);
            arp_v.playing = 255; arp_v.held = 0;
        }
        break;
    default: break;
    }
}

/* -------------------------------------------------------------- drawing */
TP_SMALL static void view_grain(void)
{
    uint16_t ec = emitter_color[sel];
    curve(plot_a, VY0 + 8, VY0 + 58, VY0 + 58, ec);
    if (band_hits(VY0, VY0 + 8)) gfx_text(VX0, VY0, "ONE GRAIN", 1, C_DIM);
    if (band_hits(VY0 + 62, VY0 + 72)) {
        char a[24], b[16];
        strcpy(a, "OVERLAP "); vnum(b, view_f[0], view_f[0] < 10.0f ? 1 : 0, "X"); strcat(a, b);
        gfx_text(VX0, VY0 + 64, a, 1, C_TEXT);
        seconds_label(b, view_f[1]); rtext(VX1, VY0 + 64, b, C_DIM);
    }
    if (gfx_top + GFX_BAND > VY0 + 80) {
        curve(plot_b, VY0 + 84, VY1 - 12, VY1 - 12, gfx_mix(ec, C_TEXT, 60));
        for (unsigned k = 0; k < plot_n; ++k) {
            int v = ticks[k], x = v & 0xfff, rev = v >> 12;
            gfx_vline(VX0 + x, VY1 - 10, 5, rev ? C_BEET : ec);
        }
        if (!plot_n && band_hits(VY0 + 100, VY0 + 110)) ctext(120, VY0 + 102, "DENSE CLOUD", 1, C_DIM);
    }
}
TP_SMALL static void view_position(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    enum { LH = 18, CWD = VW / 8 };
    for (unsigned t = 0; t < TP_TRACKS; ++t) {
        int y0 = VY0 + (int)t * LH, mid = y0 + LH / 2;
        if (!band_hits(y0, y0 + LH)) continue;
        int16_t src = canvas.tr[t][TR_SOURCE];
        unsigned empty = src < TP_SLOTS ? !tempura_slot_frames((unsigned)src) : 0;
        for (unsigned r = 0; r < TP_ROWS; ++r) {
            uint16_t bg = ((r + t) & 1U) ? C_CELL1 : C_CELL0;
            if ((canvas.cells[sel][t] >> r) & 1U) bg = gfx_mix(bg, ec, 60);
            gfx_fill(VX0 + (int)r * CWD, y0, CWD - 1, LH - 1, bg);
        }
        if (!empty)
            for (int x = 0; x < VW; x += 2) {
                int h = (int)tempura_track_peak(t, (unsigned)(x * 160 / VW)) * 7 / 255;
                gfx_vline(VX0 + x, mid - h, 2 * h + 1, src == TP_SOURCE_LIVE ? RGB(70, 120, 110) : C_WAVE);
            }
        char lab[3] = {'T', (char)('1' + t), 0};
        gfx_text(0, mid - 3, lab, 1, t == cur_x ? C_TEXT : C_DIM);
    }
    /* Live grains of the selected emitter. */
    for (unsigned i = 0; i < grain_views; ++i) {
        const struct tp_grain_view *g = &grain_view[i];
        if (g->emitter != sel) continue;
        int t = (g->x - 16) / 32, frac = (g->x - 16) % 32;
        int x = VX0 + (int)g->y * VW / 256, y = VY0 + t * LH + LH / 2 + (frac - 16) * LH / 40;
        dot(x, y, g->bright > 90 ? 1 : 0, gfx_mix(ec, C_TEXT, 120));
    }
    /* Spray: along the track around the cursor cell, and across tracks. */
    int cy = VY0 + (int)cur_x * LH, cx = VX0 + (int)cur_y * CWD + CWD / 2 + (int)(ce->pos_offset * (float)CWD);
    gfx_rect(VX0 + (int)cur_y * CWD - 1, cy - 1, CWD + 1, LH + 1, C_TEXT);
    int sy = (int)(ce->spray_y * (float)CWD);
    gfx_hline(cx - sy, cy + LH - 2, 2 * sy + 1, C_AMBER);
    gfx_vline(cx - sy, cy + LH - 5, 4, C_AMBER); gfx_vline(cx + sy, cy + LH - 5, 4, C_AMBER);
    int sx = (int)(ce->spray_x * (float)LH);
    gfx_vline(cx, cy + LH / 2 - sx, 2 * sx + 1, C_TEAL);
    if (ce->scan_cells_s != 0.0f) gfx_text(cx + (ce->scan_cells_s > 0 ? 4 : -16), cy + 2, ce->scan_cells_s > 0 ? ">>" : "<<", 1, C_AMBER);
}
TP_SMALL static void view_pitch(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    float pt = vclamp(ce->pitch_semis, -24.0f, 24.0f);
    int center = 120, top = VY0 + 30, bot = VY0 + 112;
    if (band_hits(top - 2, bot + 30)) {
        if (ce->quant < 2 && ce->pitch_spray > 0.0f) {
            int a = center + (int)((pt - ce->pitch_spray) * 4.0f), b = center + (int)((pt + ce->pitch_spray) * 4.0f);
            gfx_fill(a, top - 6, b - a + 1, bot - top + 12, gfx_mix(C_BG, ec, 50));
        }
        int target = (int)(pt < 0 ? pt - 0.5f : pt + 0.5f);
        for (int s = -24; s <= 24; ++s) {
            unsigned m = (unsigned)(s + 48) % 12U;
            int black = m == 1 || m == 3 || m == 6 || m == 8 || m == 10;
            uint16_t c = black ? RGB(70, 64, 78) : RGB(150, 144, 156);
            int jump = ce->quant >= 2 && ce->pitch_spray > 0.0f &&
                       (s == target - 12 || s == target + 12 || (ce->quant == 3 && (s == target + 7 || s == target - 5)));
            if (jump) c = gfx_mix(c, ec, 80U + (unsigned)(ce->pitch_spray * 175.0f));
            if (s == target) c = ec;
            gfx_fill(center + s * 4 - 1, top, 3, black ? (bot - top) * 2 / 3 : bot - top, c);
        }
        int fx = center + (int)(pt * 4.0f);
        gfx_vline(fx, top - 8, 6, C_TEXT); gfx_hline(fx - 2, top - 8, 5, C_TEXT);
        static const int8_t marks[5] = {-24, -12, 0, 12, 24};
        static const char *const names[5] = {"-2 OCT", "-1", "ROOT", "+1", "+2 OCT"};
        for (unsigned i = 0; i < 5; ++i) ctext(center + marks[i] * 4, bot + 6, names[i], 1, marks[i] ? C_DIM : C_TEXT);
    }
    if (band_hits(VY1 - 22, VY1)) {
        ctext(120, VY1 - 9, ce->quant >= 2 ? "SPRAY JUMPS OCTAVES" : ce->quant == 1 ? "SPRAY SNAPS TO SEMITONES" : "SPRAY IS SMOOTH", 1, C_DIM);
    }
}
TP_SMALL static void view_env(void)
{
    uint16_t ec = emitter_color[sel];
    int base = VY1 - 22;
    curve(plot_a, VY0 + 14, base, base, ec);
    int xa = (int)view_f[4], xd = (int)view_f[5], xs = (int)view_f[6];
    const int xs4[3] = {xa, xd, xs};
    for (unsigned i = 0; i < 3; ++i) for (int y = VY0 + 10; y < base; y += 4) gfx_fill(VX0 + xs4[i], y, 1, 2, C_GRIDL);
    if (band_hits(base, base + 22)) {
        char b[12];
        static const char *const n[4] = {"A", "D", "S", "R"};
        int cx[4] = {VX0 + xa / 2, VX0 + (xa + xd) / 2, VX0 + (xd + xs) / 2, VX0 + (xs + VW) / 2};
        for (unsigned i = 0; i < 4; ++i) {
            if (i == 2) vnum(b, view_f[3] * 100.0f, 0, "%"); else seconds_label(b, view_f[i == 3 ? 2 : i]);
            int tx = cx[i] - gfx_text_width(b, 1) / 2;
            if (tx < 2) tx = 2;
            if (tx + gfx_text_width(b, 1) > 238) tx = 238 - gfx_text_width(b, 1);
            gfx_text(tx, base + 12, b, 1, C_TEXT);
            gfx_text(cx[i] - 3, base + 2, n[i], 1, C_DIM);
        }
    }
    float level; unsigned stage = tempura_env_view(sel, &level);
    if (stage) {
        int y = vy(level, 0.0f, 1.0f, VY0 + 14, base), x = 0;
        if (stage == 1) x = (int)(level * (float)xa);
        else {
            int from = stage == 2 ? xa : xs, to = stage == 2 ? xs : VW - 1, best = 1 << 20;
            for (int i = from; i <= to && i < VW; ++i) { int d = plot_a[i] - y; d = d < 0 ? -d : d; if (d < best) { best = d; x = i; } }
        }
        dot(VX0 + x, y, 3, C_TEXT);
    }
}
TP_SMALL static void view_filter(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    int top = VY0 + 6, bot = VY1 - 18, zero = vy(0.0f, -30.0f, 18.0f, top, bot);
    if (band_hits(top, bot + 14)) {
        static const float marks[3] = {100.0f, 1000.0f, 10000.0f};
        static const char *const names[3] = {"100", "1K", "10K"};
        for (unsigned i = 0; i < 3; ++i) {
            int x = VX0 + (int)(vlog2(marks[i] / 20.0f) * (float)VW / 10.0f);
            gfx_vline(x, top, bot - top, C_GRIDL);
            ctext(x, bot + 6, names[i], 1, C_DIM);
        }
        gfx_hline(VX0, zero, VW, C_GRIDL);
    }
    if (ce->filter && ce->env_filter_oct != 0.0f) curve(plot_b, top, bot, -1, gfx_mix(C_BG, C_AMBER, 150));
    curve(plot_a, top, bot, ce->filter ? bot : -1, ce->filter ? ec : C_DIM);
    if (band_hits(top, top + 10)) {
        char b[16];
        if (!ce->filter) gfx_text(VX0 + 4, top + 2, "FILTER OFF", 1, C_DIM);
        else {
            vnum(b, view_f[0] >= 1000.0f ? view_f[0] / 1000.0f : view_f[0], view_f[0] >= 1000.0f ? 1 : 0, view_f[0] >= 1000.0f ? " KHZ" : " HZ");
            rtext(VX1, top + 2, b, C_TEXT);
            if (ce->env_filter_oct != 0.0f) gfx_text(VX0 + 4, top + 2, "ENV MOVES IT", 1, C_AMBER);
        }
    }
}
TP_SMALL static void view_lfo(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    int top = VY0 + 24, bot = VY1 - 16, mid = (top + bot) / 2;
    if (band_hits(top, bot)) gfx_hline(VX0, mid, VW, C_GRIDL);
    curve(plot_b, top, bot, -1, C_GRIDL);
    curve(plot_a, top, bot, mid, ec);
    float phase, value; tempura_lfo_view(sel, &phase, &value);
    int x = VX0 + (int)(vclamp(phase, 0.0f, 0.999f) * (float)VW * 0.5f);
    if (band_hits(top, bot)) { gfx_vline(x, top, bot - top, gfx_mix(C_BG, C_TEXT, 90)); dot(x, vy(value * ce->lfo_depth, -1.0f, 1.0f, top, bot), 2, C_TEXT); }
    if (band_hits(VY0, VY0 + 18)) {
        gfx_text(VX0, VY0 + 2, "TO", 1, C_DIM);
        gfx_text(VX0 + 16, VY0, tempura_list_name(tempura_emitter_info(EP_LFO_DEST), ce->lfo_dest), 2, C_AMBER);
        char b[16]; vnum(b, ce->lfo_rate, ce->lfo_rate < 10.0f ? 2 : 1, " HZ"); rtext(VX1, VY0 + 4, b, C_TEXT);
    }
}
TP_SMALL static void view_sends(void)
{
    /* A patch bay: the emitter's cables run to three effect modules and,
     * dry, straight to the output; pulses travel along each cable. */
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    static const char *const n[3] = {"MOD", "DELAY", "REVERB"};
    static const uint16_t col[3] = {RGB(255, 176, 60), RGB(48, 214, 192), RGB(176, 140, 255)};
    float v[3] = {ce->send_mod, ce->send_delay, ce->send_reverb};
    float t = vtime();
    const int ox = 38, oy = 96, mx = 112, mw = 98, jx = 226, jy = 162;
    for (unsigned k = 0; k < 4; ++k) {
        /* Cable: a curve from the orb to a module (or, dry, to the jack). */
        float send = k < 3 ? v[k] : ce->dry;
        uint16_t c = k < 3 ? col[k] : C_TEXT;
        int ex = k < 3 ? mx : jx - 6, ey = k < 3 ? 44 + (int)k * 46 : jy;
        int px = ox, py = oy;
        uint16_t cc = gfx_mix(C_BG, c, (unsigned)(40.0f + 200.0f * send));
        for (int s2 = 1; s2 <= 36; ++s2) {
            float u = (float)s2 / 36.0f, a = (1 - u) * (1 - u) * (1 - u), b = 3 * (1 - u) * (1 - u) * u, cq = 3 * (1 - u) * u * u, d = u * u * u;
            float c1x = k < 3 ? 76.0f : (float)ox, c1y = (float)oy + (k < 3 ? 0.0f : 60.0f), c2x = k < 3 ? 76.0f : 120.0f, c2y = (float)ey;
            int x = (int)(a * ox + b * c1x + cq * c2x + d * ex), y = (int)(a * oy + b * c1y + cq * c2y + d * ey);
            if (send > 0.01f || (s2 & 2)) seg(px, py, x, y, send > 0.5f ? 2 : 1, cc);
            px = x; py = y;
            if (send > 0.01f)
                for (int q = 0; q < 2; ++q) {
                    float pos = vfrac(t * (0.25f + 0.6f * send) + 0.5f * (float)q);
                    if (pos >= u - 1.0f / 36.0f && pos < u) glow(x, y, gfx_mix(c, C_TEXT, 120));
                }
        }
        if (k == 3) continue;
        int y0 = 30 + (int)k * 46;
        if (!band_hits(y0, y0 + 30)) continue;
        gfx_rfill(mx, y0, mw, 30, 5, gfx_mix(C_BG, col[k], 46));
        gfx_hline(mx + 4, y0 + 1, mw - 8, gfx_mix(col[k], C_TEXT, 70));
        gfx_label(mx + 26, y0 + 5, n[k], col[k]);
        char b[8]; vnum(b, v[k] * 100.0f, 0, "%"); gfx_label(mx + mw - 4 - gfx_label_width(b), y0 + 5, b, C_TEXT);
        hbar(mx + 26, y0 + 20, mw - 32, 4, v[k], col[k]);
        /* Module icon. */
        for (int x = 0; x < 16; ++x) {
            int yy = k == 0 ? (int)(5.0f * vsin1((float)x / 16.0f + t * 0.5f)) : 0;
            if (k == 0) gfx_fill(mx + 5 + x, y0 + 15 + yy, 1, 2, col[k]);
            else if (k == 1 && (x % 5) == 0) gfx_fill(mx + 5 + x, y0 + 8 + x / 2, 2, 14 - x / 2 * 2 + 2, col[k]);
            else if (k == 2) gfx_fill(mx + 5 + x, y0 + 22 - (int)(12.0f * vexp2(-(float)x * 0.25f) * vrand((uint32_t)(x * 31) + (uint32_t)(t * 8.0f))), 1, 1, col[k]);
        }
        seg(mx + mw, y0 + 15, jx, y0 + 15, 1, gfx_mix(C_BG, col[k], 120));
    }
    if (band_hits(30, jy + 8)) gfx_vline(jx, 45, jy - 45, C_GRIDL);
    if (band_hits(oy - 14, oy + 14)) {
        gfx_rfill(ox - 13, oy - 13, 27, 27, 6, ec);
        gfx_rfill(ox - 8, oy - 10, 10, 6, 3, gfx_mix(ec, C_TEXT, 150));
        char e[3] = {'E', (char)('1' + sel), 0}; ctext(ox, oy - 3, e, 1, C_BG);
    }
    if (band_hits(jy - 6, jy + 8)) { gfx_rfill(jx - 5, jy - 5, 11, 11, 3, C_TEXT); dot(jx, jy, 1, C_BG); gfx_label(jx - 32, jy - 3, "OUT", C_DIM); }
}
TP_SMALL static void view_modfx(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    if (band_hits(VY0, VY0 + 18)) {
        gfx_text(VX0, VY0, tempura_list_name(tempura_global_info(G_MOD_TYPE), fx->mod_type), 2, C_TEAL);
        char b[16]; vnum(b, view_f[2], 1, " MS"); rtext(VX1, VY0 + 4, b, C_TEXT);
    }
    /* Top: the comb filter (or vibrato's pitch wobble), moving with the LFO. */
    curve(plot_a, VY0 + 24, VY0 + 80, VY0 + 80, fx->mod_type == 2 ? C_AMBER : C_TEAL);
    if (band_hits(VY0 + 82, VY0 + 90)) { gfx_label(VX0, VY0 + 82, fx->mod_type == 2 ? "PITCH WOBBLE" : "COMB FILTER NOW", C_DIM); if (fx->mod_type != 2) gfx_label(VX1 - 22, VY0 + 82, "3K", C_DIM); }
    /* Bottom: left and right delay sweeps with a live playhead. */
    if (fx->mod_type != 2) curve(plot_c, VY0 + 92, VY1 - 14, -1, C_AMBER);
    curve(plot_b, VY0 + 92, VY1 - 14, -1, C_TEAL);
    int x = (int)(vfrac(view_f[1] * 0.5f + (float)((int)(vtime() * view_f[0]) & 1) * 0.5f) * (float)(VW - 1));
    if (band_hits(VY0 + 90, VY1 - 12)) {
        gfx_vline(VX0 + x, VY0 + 92, VY1 - 14 - (VY0 + 92), gfx_mix(C_BG, C_TEXT, 70));
        glow(VX0 + x, plot_b[x], C_TEAL);
        if (fx->mod_type != 2) glow(VX0 + x, plot_c[x], C_AMBER);
    }
    if (band_hits(VY1 - 10, VY1)) { gfx_label(VX0, VY1 - 8, "SWEEP L", C_TEAL); if (fx->mod_type != 2) gfx_label(VX0 + 48, VY1 - 8, "R", C_AMBER); hbar(150, VY1 - 6, 74, 4, fx->mod_feedback / 0.9f, C_BEET); }
}
TP_SMALL static void view_delay(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    float dt = fx->dly_seconds, span = vclamp(dt * 9.0f, 0.4f, 7.0f);
    int mid = (VY0 + VY1) / 2 + 4;
    /* A ball bounces from echo to echo in real time; each echo flashes as it lands. */
    float tp = vfrac(vtime() / (span * 1.15f)) * span * 1.15f;
    float keep = 0.55f + 0.45f * vnorm(canvas.g[G_DLY_TONE]);
    int width = 4 + (int)((1.0f - keep) * 6.0f);
    int prev_x = VX0, prev_y = mid - 50, ball_x = -1, ball_y = 0;
    if (band_hits(mid - 62, mid + 62)) { gfx_hline(VX0, mid, VW, C_GRIDL); gfx_rfill(VX0, mid - 50, 4, 101, 1, C_DIM); }
    float amp = 1.0f;
    for (unsigned n = 1; n < 40; ++n) {
        float t = dt * (float)n;
        if (t > span || amp < 0.02f) break;
        int x = VX0 + (int)(t / span * (float)VW), h = (int)(vsqrt(amp) * 50.0f);
        float flash = tp >= t ? vclamp(1.0f - (tp - t) * 6.0f, 0.0f, 1.0f) : 0.0f;
        uint16_t c = gfx_mix(gfx_mix(C_BG, C_TEAL, (unsigned)(255.0f * vmax(0.25f, amp))), C_TEXT, (unsigned)(flash * 200.0f));
        int hh = h + (int)(flash * 6.0f), down = fx->ping && !(n & 1U);
        int y = down ? mid + hh : mid - hh;
        if (band_hits(mid - 62, mid + 62)) {
            if (!fx->ping) gfx_rfill(x, mid - hh, width, 2 * hh + 1, 1, c);
            else if (!down) gfx_rfill(x, mid - hh, width, hh, 1, c);
            else gfx_rfill(x, mid + 1, width, hh, 1, c);
            if (flash > 0.0f) glow(x + width / 2, y, C_TEXT);
        }
        /* The arc from the previous echo to this one. */
        int lift = fx->ping ? 0 : 18;
        int ax = prev_x, ay = prev_y;
        for (int k = 1; k <= 16; ++k) {
            float u = (float)k / 16.0f;
            int bx = prev_x + (int)((float)(x - prev_x) * u), by = prev_y + (int)((float)(y - prev_y) * u) - (int)((float)lift * 4.0f * u * (1.0f - u));
            if (k & 1) seg(ax, ay, bx, by, 1, gfx_mix(C_BG, C_TEAL, 70));
            ax = bx; ay = by;
        }
        float t0 = dt * (float)(n - 1);
        if (tp >= t0 && tp < t) {
            float u = (tp - t0) / dt;
            ball_x = prev_x + (int)((float)(x - prev_x) * u);
            ball_y = prev_y + (int)((float)(y - prev_y) * u) - (int)((float)lift * 4.0f * u * (1.0f - u));
        }
        prev_x = x + width / 2; prev_y = y;
        amp *= fx->dly_feedback * keep;
    }
    if (ball_x >= 0) glow(ball_x, ball_y, C_TEXT);
    if (fx->ping && band_hits(mid - 62, mid + 62)) { gfx_text(VX1 - 6, mid - 58, "L", 1, C_DIM); gfx_text(VX1 - 6, mid + 52, "R", 1, C_DIM); }
    if (band_hits(VY0, VY0 + 10)) {
        char b[16]; seconds_label(b, dt);
        gfx_text(VX0, VY0, "ECHO EVERY", 1, C_DIM); gfx_text(VX0 + 66, VY0, b, 1, C_TEXT);
    }
}
TP_SMALL static void view_reverb(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    /* A room in perspective, sized by the decay, with sound rings spreading
     * from a source on the floor and fading with the damping. */
    float rt = view_f[0], size = vclamp(vlog2(rt * (1.0f / 0.3f)) * 0.2f, 0.0f, 1.0f);   /* 0.3 s .. 10 s */
    int fx0 = VX0, fx1 = VX1, fy0 = VY0 + 12, fy1 = VY0 + 92;
    int bw = 150 - (int)(104.0f * size), bh = 62 - (int)(42.0f * size);      /* longer decay: deeper room */
    int bx0 = 120 - bw / 2, bx1 = 120 + bw / 2, by0 = 56 - bh / 2, by1 = 56 + bh / 2;
    uint16_t wall = RGB(70, 52, 104);
    if (band_hits(fy0, fy1 + 1)) {
        gfx_vgrad(bx0, by1, bw, 1, wall, wall);
        for (int y = by1; y < fy1; ++y) {   /* floor, lighter towards the front */
            int l = bx0 - (bx0 - fx0) * (y - by1) / (fy1 - by1), r = bx1 + (fx1 - bx1) * (y - by1) / (fy1 - by1);
            if (y >= gfx_top && y < gfx_top + GFX_BAND) gfx_fill(l, y, r - l, 1, gfx_mix(RGB(14, 8, 22), RGB(40, 26, 58), (unsigned)((y - by1) * 255 / (fy1 - by1))));
        }
        gfx_rect(bx0, by0, bw, bh, wall);
        seg(fx0, fy0, bx0, by0, 1, wall); seg(fx1 - 1, fy0, bx1, by0, 1, wall);
        seg(fx0, fy1, bx0, by1, 1, wall); seg(fx1 - 1, fy1, bx1, by1, 1, wall);
        float t = vtime();
        for (unsigned k = 0; k < 5; ++k) {
            float ph = vfrac(t * (0.35f + 0.25f / rt) + (float)k * 0.2f);
            float r = ph * 104.0f, fade = (1.0f - ph);
            fade *= fade * (1.0f - 0.7f * fx->rev_damp * ph);
            uint16_t c = gfx_mix(C_BG, gfx_mix(RGB(150, 110, 240), RGB(250, 235, 255), (unsigned)(255.0f * (1.0f - ph))), (unsigned)(255.0f * vsqrt(fade)));
            for (int a = 0; a < 48; ++a) {
                float u = (float)a / 48.0f;
                int x = 120 + (int)(r * vcos1(u)), y = 76 + (int)(r * 0.32f * vsin1(u));
                if (x > fx0 && x < fx1 - 1 && y > fy0 && y < fy1) gfx_fill(x, y, 2, 1, c);
            }
        }
        glow(120, 76, C_AMBER);
    }
    /* The tail below. */
    int mid = VY1 - 32;
    if (band_hits(mid - 28, mid + 28)) {
        for (int i = 0; i < VW; ++i) {
            unsigned fade = (unsigned)(255.0f * (1.0f - fx->rev_damp * 1.6f * (float)i / (float)VW));
            uint16_t c = gfx_mix(RGB(60, 40, 90), RGB(200, 170, 255), fade);
            if (plot_a[i]) gfx_vline(VX0 + i, mid - plot_a[i], plot_a[i], c);
            if (plot_b[i]) gfx_vline(VX0 + i, mid + 1, plot_b[i], c);
        }
        gfx_hline(VX0, mid, VW, C_GRIDL);
    }
    if (band_hits(VY0, VY0 + 10)) {
        char b[16]; seconds_label(b, rt);
        gfx_text(VX0, VY0, "DECAY", 1, C_DIM); gfx_text(VX0 + 36, VY0, b, 1, C_TEXT);
        rtext(VX1, VY0, fx->rev_width > 0.5f ? "WIDE" : "NARROW", C_DIM);
    }
}
TP_SMALL static void view_track(void)
{
    const struct tp_cooked_track *tr = &tp_cook.t[cur_x];
    int16_t src = canvas.tr[cur_x][TR_SOURCE];
    int top = VY0 + 6, bot = VY0 + 104, mid = (top + bot) / 2;
    unsigned empty = src < TP_SLOTS ? !tempura_slot_frames((unsigned)src) : 0;
    int xs = VX0 + (int)(tr->start * (float)VW), xe = VX0 + (int)(tr->end * (float)VW);
    if (band_hits(top, bot)) {
        gfx_hline(VX0, mid, VW, C_GRIDL);
        if (!empty)
            for (int x = 0; x < VW; ++x) {
                int h = (int)tempura_track_peak(cur_x, (unsigned)(x * 160 / VW)) * (bot - top) / 2 / 255;
                int in = VX0 + x >= xs && VX0 + x <= xe;
                gfx_vline(VX0 + x, mid - h, 2 * h + 1, in ? (src == TP_SOURCE_LIVE ? C_TEAL : RGB(150, 140, 170)) : C_GRIDL);
            }
        gfx_vline(xs, top, bot - top, C_BEET); gfx_vline(xe, top, bot - top, C_BEET);
        for (unsigned i = 0; i < grain_views; ++i) {
            const struct tp_grain_view *g = &grain_view[i];
            if ((unsigned)((g->x - 16) / 32) != cur_x) continue;
            dot(VX0 + (int)g->y * VW / 256, mid + ((g->x - 16) % 32 - 16), 1, emitter_color[g->emitter]);
        }
        if (src == TP_SOURCE_LIVE) gfx_vline(VX0 + (int)tempura_live_head() * VW / 160, top, bot - top, canvas.g[G_LIVE_FREEZE] ? C_DIM : C_AMBER);
        if (empty) ctext(120, mid - 3, "NO SOUND IN THIS SLOT", 1, C_DIM);
    }
    if (band_hits(top - 2, top + 8)) {
        gfx_text(xs + 2, top, "S", 1, C_BEET); gfx_text(xe - 7, top, "E", 1, C_BEET);
        if (tr->reverse) ctext(120, top, "<< REVERSED", 1, C_AMBER);
    }
}
TP_SMALL static void view_arp(void)
{
    uint16_t ec = emitter_color[sel];
    unsigned on = canvas.g[G_ARP_ON] != 0;
    if (band_hits(VY0, VY0 + 18)) {
        char a[28], b[12];
        strcpy(a, tempura_list_name(tempura_global_info(G_ARP_MODE), canvas.g[G_ARP_MODE]));
        strcat(a, "  "); strcat(a, tempura_list_name(tempura_global_info(G_ARP_RATE), canvas.g[G_ARP_RATE]));
        gfx_text(VX0, VY0, a, 2, on ? ec : C_DIM);
        vnum(b, (float)canvas.g[G_ARP_OCT], 0, " OCT"); rtext(VX1, VY0 + 4, b, C_TEXT);
    }
    unsigned n = arp_v.count;
    int top = VY0 + 30, bot = VY1 - 30;
    if (n && band_hits(top - 2, bot + 12)) {
        uint8_t lo = 127, hi = 0;
        for (unsigned i = 0; i < n; ++i) { if (arp_v.notes[i] < lo) lo = arp_v.notes[i]; if (arp_v.notes[i] > hi) hi = arp_v.notes[i]; }
        float step = (float)VW / (float)n, swing = vnorm(canvas.g[G_ARP_SWING]) * 0.5f;
        float gate = 0.05f + vnorm(canvas.g[G_ARP_GATE]) * 0.95f;
        static const uint8_t spb[6] = {1, 2, 3, 4, 6, 8};
        unsigned beat = spb[(unsigned)canvas.g[G_ARP_RATE] < 6U ? (unsigned)canvas.g[G_ARP_RATE] : 3U];
        for (unsigned i = 0; i < n; ++i) {
            float x0 = step * (float)(i & ~1U) + ((i & 1U) ? step * (1.0f + swing) : 0.0f);
            float len = step * ((i & 1U) ? 1.0f - swing : 1.0f + swing);
            int x = VX0 + (int)x0, w = (int)(len * gate); if (w < 2) w = 2;
            int y = hi > lo ? vy((float)(arp_v.notes[i] - lo), 0.0f, (float)(hi - lo), top, bot - 10) : (top + bot) / 2;
            uint16_t c = !arp_v.held || !on ? C_GRIDL : i == arp_v.playing ? C_TEXT : gfx_mix(C_BG, ec, 150);
            gfx_fill(x, y, w, 8, c);
            if (canvas.g[G_ARP_VELO] == ARP_VELO_ACCENT && i % beat == 0) gfx_fill(x, bot + 4, w, 3, C_AMBER);
        }
    }
    if (band_hits(VY1 - 22, VY1)) {
        const char *msg = !on ? "ARP OFF  K1 OR HOLD ARP: ON" : !arp_v.held ? "HOLD KEYS TO ARPEGGIATE" : NULL;
        if (msg) ctext(120, VY1 - 18, msg, 1, C_DIM);
        else { char b[12]; vnum(b, (float)arp_v.held, 0, " KEYS HELD"); gfx_text(VX0, VY1 - 18, b, 1, C_TEXT); }
        if (canvas.g[G_ARP_LATCH]) rtext(VX1, VY1 - 6, "LATCH", C_AMBER);
    }
}

TP_SMALL static void draw_view(unsigned view)
{
    if (gfx_top + GFX_BAND <= GY - 2 || gfx_top >= PANEL_Y - 2) return;
    gfx_vgrad(0, GY - 2, GFX_W, PANEL_Y - GY, RGB(6, 3, 9), RGB(22, 9, 24));
    switch (view) {
    case V_GRAIN: view_grain(); break;
    case V_POSITION: view_position(); break;
    case V_PITCH: view_pitch(); break;
    case V_ENV: view_env(); break;
    case V_FILTER: view_filter(); break;
    case V_LFO: view_lfo(); break;
    case V_SENDS: view_sends(); break;
    case V_MODFX: view_modfx(); break;
    case V_DELAY: view_delay(); break;
    case V_REVERB: view_reverb(); break;
    case V_TRACK: view_track(); break;
    case V_ARP: view_arp(); break;
    default: break;
    }
}
