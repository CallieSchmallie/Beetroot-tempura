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

static int16_t plot_a[VW], plot_b[VW];
static uint16_t ticks[64];             /* grain starts (x | reversed << 12) */
static uint8_t plot_n;                 /* generic count prepared for the view */
static float view_f[8];                /* generic values prepared for the view */
static struct tp_arp_view arp_v;

static int band_hits(int y0, int y1) { return gfx_top < y1 && gfx_top + GFX_BAND > y0; }
static float vnorm(int v) { return (float)v * (1.0f / 127.0f); }
static float vclamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static float vlog2(float x);
static float vsqrt(float x) { return x > 0.0f ? tp_exp2(0.5f * vlog2(x)) : 0.0f; }
/* log2 for drawing only: exponent bits plus a quadratic for the mantissa. */
static float vlog2(float x)
{
    union { float f; uint32_t u; } v = {x > 1e-20f ? x : 1e-20f};
    float e = (float)(int)((v.u >> 23) & 255U) - 127.0f;
    v.u = (v.u & 0x007fffffU) | 0x3f800000U;
    float t = (v.f - 1.0f) / (v.f + 1.0f), t2 = t * t;    /* ln m = 2 atanh t */
    return e + 2.8853901f * t * (1.0f + t2 * (0.33333333f + t2 * (0.2f + t2 * 0.14285714f)));
}
static int vy(float v, float lo, float hi, int top, int bot)
{
    float t = vclamp((v - lo) / (hi - lo), 0.0f, 1.0f);
    return bot - (int)(t * (float)(bot - top) + 0.5f);
}
static uint32_t vhash(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
static float vrand(uint32_t x) { return (float)(vhash(x) & 0xffffU) * (1.0f / 65535.0f); }

/* Draw a prepared curve; base >= 0 also fills down (or up) to that row. */
static void curve(const int16_t *y, int top, int bot, int base, uint16_t c)
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
static void ctext(int x, int y, const char *s, int scale, uint16_t c) { gfx_text(x - gfx_text_width(s, scale) / 2, y, s, scale, c); }
static void rtext(int x, int y, const char *s, uint16_t c) { gfx_text(x - gfx_text_width(s, 1), y, s, 1, c); }
/* "2.3" style numbers for labels. */
static void vnum(char *out, float v, int decimals, const char *unit)
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
static void seconds_label(char *out, float s)
{
    if (s < 1.0f) vnum(out, s * 1000.0f, 0, " MS"); else vnum(out, s, s < 10.0f ? 2 : 1, " S");
}
static void hbar(int x, int y, int w, int h, float fill, uint16_t c)
{
    gfx_fill(x, y, w, h, C_GRIDL);
    int f = (int)(vclamp(fill, 0.0f, 1.0f) * (float)w + 0.5f);
    if (f) gfx_fill(x, y, f, h, c);
}
static void vbar(int x, int top, int bot, int w, float fill, uint16_t c)
{
    gfx_fill(x, top, w, bot - top, C_GRIDL);
    int h = (int)(vclamp(fill, 0.0f, 1.0f) * (float)(bot - top) + 0.5f);
    if (h) gfx_fill(x, bot - h, w, h, c);
}
static void dot(int x, int y, int r, uint16_t c) { gfx_fill(x - r, y - r, 2 * r + 1, 2 * r + 1, c); }

static float lfo_at(unsigned shape, float t)
{
    float ph = t - (float)(int)t;
    unsigned cyc = (unsigned)(int)t;
    if (shape == 0) return tp_sin1(ph);
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
static void view_prepare(unsigned view)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    const int16_t *p = canvas.ep[sel];
    switch (view) {
    case V_GRAIN: {
        const float *w = tempura_window(ce->shape);
        for (int i = 0; i < VW; ++i) plot_a[i] = (int16_t)vy(w[i * 256 / (VW - 1)], 0.0f, 1.0f, VY0 + 8, VY0 + 58);
        float size = ce->size_s, rate = ce->rate_hz, overlap = size * rate;
        float span = vclamp(tp_maxf(size * 3.0f, 6.0f / rate), 0.02f, 6.0f);
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
            for (int i = 0; i < VW; ++i) peak = tp_maxf(peak, sum[i]);
        }
        if (peak <= 0.0f) plot_n = 0;
        for (int i = 0; i < VW; ++i) plot_b[i] = (int16_t)vy(peak > 0.0f ? sum[i] / peak : 0.75f, 0.0f, 1.0f, VY0 + 84, VY1 - 12);
        break;
    }
    case V_ENV: {
        float a = 0.001f * tp_exp2((float)p[EP_ATTACK] * (13.0f / 127.0f));
        float d = 0.005f * tp_exp2((float)p[EP_DECAY] * (11.0f / 127.0f));
        float r = 0.005f * tp_exp2((float)p[EP_RELEASE] * (11.0f / 127.0f));
        float s = vnorm(p[EP_SUSTAIN]);
        float sa = vsqrt(a), sd = vsqrt(d), sr = vsqrt(r), tot = sa + sd + sr;
        float avail = (float)VW * 0.80f;
        int xa = (int)(avail * sa / tot), xd = xa + (int)(avail * sd / tot), xs = xd + VW / 5;
        view_f[0] = a; view_f[1] = d; view_f[2] = r; view_f[3] = s;
        view_f[4] = (float)xa; view_f[5] = (float)xd; view_f[6] = (float)xs;
        for (int i = 0; i < VW; ++i) {
            float e;
            if (i < xa) e = xa ? (float)i / (float)xa : 1.0f;
            else if (i < xd) e = s + (1.0f - s) * tp_exp(-6.9f * (float)(i - xa) / (float)(xd - xa > 0 ? xd - xa : 1));
            else if (i < xs) e = s;
            else e = s * tp_exp(-6.9f * (float)(i - xs) / (float)(VW - xs > 1 ? VW - xs : 1));
            plot_a[i] = (int16_t)vy(e, 0.0f, 1.0f, VY0 + 14, VY1 - 22);
        }
        break;
    }
    case V_FILTER: {
        float fc = 20.0f * tp_exp2(ce->cutoff_oct), q = ce->res_q;
        float fc2 = 20.0f * tp_exp2(vclamp(ce->cutoff_oct + ce->env_filter_oct, 0.0f, 10.0f));
        for (int pass = 0; pass < 2; ++pass) {
            float c = pass ? fc2 : fc;
            for (int i = 0; i < VW; ++i) {
                float f = 20.0f * tp_exp2(10.0f * (float)i / (float)VW), w = f / c, w2 = w * w;
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
        for (int i = 0; i < VW; ++i) {
            float ph = 2.0f * (float)i / (float)VW;
            plot_a[i] = (int16_t)vy((base + depth * (0.5f + 0.5f * tp_sin1(ph))) * 1000.0f, 0.0f, 21.0f, VY0 + 26, VY1 - 18);
            plot_b[i] = (int16_t)vy((base + depth * (0.5f + 0.5f * tp_cos1(ph))) * 1000.0f, 0.0f, 21.0f, VY0 + 26, VY1 - 18);
        }
        view_f[0] = fx->mod_rate;
        break;
    }
    case V_REVERB: {
        float fb = fx->rev_feedback;
        float rt = 3.0f * 0.0305f / (-0.30103f * vlog2(fb));
        float span = vclamp(rt * 1.15f, 0.3f, 14.0f);
        view_f[0] = rt;
        for (int i = 0; i < VW; ++i) {
            float t = span * (float)i / (float)VW, env = tp_exp2(-9.966f * t / rt);
            float nl = 0.25f + 0.75f * vrand((uint32_t)i * 7919U), nr = 0.25f + 0.75f * vrand((uint32_t)i * 104729U + 3U);
            nr = nl + (nr - nl) * fx->rev_width;
            plot_a[i] = (int16_t)(env * nl * 54.0f);
            plot_b[i] = (int16_t)(env * nr * 54.0f);
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
static void view_grain(void)
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
static void view_position(void)
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
static void view_pitch(void)
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
        char a[24], b[12];
        vnum(b, ce->pitch_semis, 2, " SEMITONES"); strcpy(a, ce->pitch_semis >= 0 ? "+" : ""); strcat(a, b);
        ctext(120, VY1 - 20, a, 1, C_TEXT);
        ctext(120, VY1 - 9, ce->quant >= 2 ? "SPRAY JUMPS OCTAVES" : ce->quant == 1 ? "SPRAY SNAPS TO SEMITONES" : "SPRAY IS SMOOTH", 1, C_DIM);
    }
}
static void view_mix(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    const int16_t *p = canvas.ep[sel];
    uint16_t ec = emitter_color[sel];
    if (band_hits(VY0, VY1)) { vbar(14, VY0 + 12, VY1 - 12, 16, vnorm(p[EP_LEVEL]), ec); }
    if (band_hits(VY0, VY0 + 10)) { gfx_text(10, VY0, "LVL", 1, C_DIM); gfx_text(52, VY0, "STEREO", 1, C_DIM); }
    int fx0 = 56, fx1 = 226, fy = VY0 + 34;
    if (band_hits(fy - 16, fy + 18)) {
        int w = fx1 - fx0, cxp = fx0 + (int)((ce->pan + 1.0f) * 0.5f * (float)w);
        int sp = (int)(ce->pan_spray * 0.5f * (float)w);
        gfx_fill(cxp - sp, fy - 8, 2 * sp + 1, 17, gfx_mix(C_BG, ec, 60));
        gfx_hline(fx0, fy, w, C_DIM);
        gfx_vline(fx0 + w / 2, fy - 4, 9, C_DIM);
        gfx_text(fx0 - 8, fy - 3, "L", 1, C_DIM); gfx_text(fx1 + 3, fy - 3, "R", 1, C_DIM);
        dot(cxp, fy, 3, ec);
    }
    int bx0 = 56, bx1 = 226, by0 = VY0 + 66, by1 = VY1 - 14;
    if (band_hits(by0 - 12, by1 + 12)) {
        if (band_hits(by0 - 12, by0 - 2)) gfx_text(bx0, by0 - 11, "VELOCITY TO LEVEL", 1, C_DIM);
        gfx_rect(bx0, by0, bx1 - bx0, by1 - by0, C_GRIDL);
        int prev = by1;
        for (int x = 0; x < bx1 - bx0; ++x) {
            float v = (float)x / (float)(bx1 - bx0), g = 1.0f - ce->velo * (1.0f - v * v);
            int y = vy(g, 0.0f, 1.0f, by0 + 2, by1 - 2);
            int lo = y < prev ? y : prev, hi = y < prev ? prev : y;
            gfx_vline(bx0 + x, lo, hi - lo + 1, ec);
            prev = y;
        }
    }
}
static void view_mode(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    const int16_t *p = canvas.ep[sel];
    uint16_t ec = emitter_color[sel];
    if (band_hits(VY0, VY0 + 18)) ctext(120, VY0 + 2, tempura_list_name(tempura_emitter_info(EP_MODE), ce->mode), 2, ec);
    int ky = VY0 + 34, sy = VY0 + 80;
    if (band_hits(ky - 2, sy + 40)) {
        gfx_text(VX0, ky + 2, "KEYS", 1, C_DIM);
        gfx_fill(60, ky, 80, 8, C_DIM); gfx_fill(110, ky + 12, 80, 8, C_DIM);
        gfx_text(VX0, sy + 2, "SOUND", 1, C_DIM);
        if (ce->mode == MODE_POLY) { gfx_fill(60, sy, 80, 8, ec); gfx_fill(110, sy + 12, 80, 8, ec); }
        else if (ce->mode == MODE_LATCH) { gfx_fill(60, sy, VX1 - 60, 8, ec); gfx_fill(110, sy + 12, VX1 - 110, 8, ec); gfx_text(150, sy + 26, "KEEPS PLAYING", 1, C_DIM); }
        else {
            /* One voice: glides to the new note when the second key comes in. */
            float glide = p[EP_GLIDE] ? 0.002f * tp_exp2((float)p[EP_GLIDE] * (10.0f / 127.0f)) : 0.0f;
            int gw = glide > 0.0f ? (int)vclamp(vlog2(glide / 0.002f) * 6.0f, 1.0f, 60.0f) : 0;
            gfx_fill(60, sy + 10, 50, 8, ec);
            for (int x = 0; x <= gw; ++x) gfx_fill(110 + x, sy + 10 - (gw ? x * 10 / gw : 10), 1, 8, ec);
            gfx_fill(110 + gw, sy, 80 - gw, 8, ec);
            if (gw) { char b[16]; seconds_label(b, glide); gfx_text(110, sy + 24, "GLIDE ", 1, C_DIM); gfx_text(146, sy + 24, b, 1, C_TEXT); }
        }
    }
    if (band_hits(VY1 - 22, VY1)) {
        char a[28];
        strcpy(a, "PITCH QUANT "); strcat(a, tempura_list_name(tempura_emitter_info(EP_QUANT), ce->quant));
        gfx_text(VX0, VY1 - 20, a, 1, C_DIM);
        strcpy(a, "SPRAY X "); strcat(a, tempura_list_name(tempura_emitter_info(EP_ALIGN_X), ce->align));
        gfx_text(VX0, VY1 - 9, a, 1, C_DIM);
    }
}
static void view_env(void)
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
static void view_filter(void)
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
static void view_lfo(void)
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
static void view_sends(void)
{
    const struct tp_cooked_emitter *ce = &tp_cook.e[sel];
    uint16_t ec = emitter_color[sel];
    static const char *const n[4] = {"MOD", "DELAY", "REVERB", "DRY"};
    float v[4] = {ce->send_mod, ce->send_delay, ce->send_reverb, ce->dry};
    for (unsigned i = 0; i < 4; ++i) {
        int x = 22 + (int)i * 54;
        if (band_hits(VY0 + 8, VY1 - 18)) vbar(x, VY0 + 10, VY1 - 22, 34, v[i], i == 3 ? C_TEXT : ec);
        if (band_hits(VY1 - 18, VY1)) ctext(x + 17, VY1 - 16, n[i], 1, C_DIM);
    }
}
static void view_modfx(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    int top = VY0 + 26, bot = VY1 - 18;
    if (band_hits(VY0, VY0 + 18)) {
        gfx_text(VX0, VY0, tempura_list_name(tempura_global_info(G_MOD_TYPE), fx->mod_type), 2, C_TEAL);
        char b[16]; vnum(b, view_f[0], view_f[0] < 10.0f ? 2 : 1, " HZ"); rtext(VX1, VY0 + 4, b, C_TEXT);
    }
    if (fx->mod_type != 2) curve(plot_b, top, bot, -1, C_AMBER);
    curve(plot_a, top, bot, -1, C_TEAL);
    if (band_hits(bot + 2, bot + 14)) {
        gfx_text(VX0, bot + 6, fx->mod_type == 2 ? "DELAY TIME (MS)" : "DELAY TIME  L", 1, C_DIM);
        if (fx->mod_type != 2) gfx_text(VX0 + 84, bot + 6, "R", 1, C_AMBER);
        hbar(150, bot + 7, 74, 4, fx->mod_feedback / 0.9f, C_BEET);
    }
}
static void view_delay(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    float dt = fx->dly_seconds, span = vclamp(dt * 9.0f, 0.4f, 7.0f);
    int mid = (VY0 + VY1) / 2 + 2;
    if (band_hits(mid - 60, mid + 62)) {
        gfx_hline(VX0, mid, VW, C_GRIDL);
        gfx_fill(VX0, mid - 50, 3, 101, C_DIM);   /* the dry sound */
        float amp = 1.0f, keep = 0.55f + 0.45f * vnorm(canvas.g[G_DLY_TONE]);
        int width = 2 + (int)((1.0f - keep) * 6.0f);
        for (unsigned n = 1; n < 40; ++n) {
            float t = dt * (float)n;
            if (t > span || amp < 0.02f) break;
            int x = VX0 + (int)(t / span * (float)VW), h = (int)(vsqrt(amp) * 50.0f);
            uint16_t c = gfx_mix(C_BG, C_TEAL, (unsigned)(255.0f * tp_maxf(0.25f, amp)));
            if (!fx->ping) gfx_fill(x, mid - h, width, 2 * h + 1, c);
            else if (n & 1U) gfx_fill(x, mid - h, width, h, c);
            else gfx_fill(x, mid + 1, width, h, c);
            amp *= fx->dly_feedback * keep;
        }
        if (fx->ping) { gfx_text(VX1 - 6, mid - 58, "L", 1, C_DIM); gfx_text(VX1 - 6, mid + 52, "R", 1, C_DIM); }
    }
    if (band_hits(VY0, VY0 + 10)) {
        char b[16]; seconds_label(b, dt);
        gfx_text(VX0, VY0, "ECHO EVERY", 1, C_DIM); gfx_text(VX0 + 66, VY0, b, 1, C_TEXT);
    }
}
static void view_dlymod(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    unsigned clock = tempura_midi_clock_bpm();
    unsigned bpm = clock ? clock : (unsigned)canvas.g[G_BPM];
    unsigned beat = ((fm1_millis() % 240000U) * bpm / 60000U) & 3U;
    if (band_hits(VY0, VY0 + 40)) {
        char b[16]; vnum(b, (float)bpm, 0, clock ? " BPM MIDI" : " BPM");
        gfx_text(VX0, VY0, b, 2, C_TEXT);
        for (unsigned i = 0; i < 4; ++i) dot(150 + (int)i * 20, VY0 + 7, 5, i == beat ? (i ? C_AMBER : C_BEET) : C_GRIDL);
    }
    int ly = VY0 + 54;
    if (band_hits(ly - 4, ly + 30)) {
        gfx_rect(40, ly, 40, 22, C_TEAL); ctext(60, ly + 7, "L", 1, C_TEAL);
        gfx_rect(160, ly, 40, 22, C_TEAL); ctext(180, ly + 7, "R", 1, C_TEAL);
        if (fx->ping) { gfx_hline(82, ly + 6, 76, C_AMBER); gfx_hline(82, ly + 15, 76, C_AMBER); ctext(120, ly - 2, "PING PONG", 1, C_AMBER); }
        else ctext(120, ly + 7, "STEREO", 1, C_DIM);
    }
    if (band_hits(VY1 - 50, VY1)) {
        gfx_text(VX0, VY1 - 44, "DELAY RETURN", 1, C_DIM); hbar(VX0, VY1 - 34, VW, 6, fx->dly_return, C_TEAL);
        gfx_text(VX0, VY1 - 22, "MOD RETURN", 1, C_DIM); hbar(VX0, VY1 - 12, VW, 6, fx->mod_return, C_AMBER);
    }
}
static void view_reverb(void)
{
    const struct tp_cooked_fx *fx = &tp_cook.fx;
    int mid = (VY0 + VY1) / 2 + 4;
    if (band_hits(mid - 58, mid + 58)) {
        for (int i = 0; i < VW; ++i) {
            unsigned fade = (unsigned)(255.0f * (1.0f - fx->rev_damp * 1.6f * (float)i / (float)VW));
            uint16_t c = gfx_mix(RGB(60, 40, 90), RGB(200, 170, 255), fade);
            if (plot_a[i]) gfx_vline(VX0 + i, mid - plot_a[i], plot_a[i], c);
            if (plot_b[i]) gfx_vline(VX0 + i, mid + 1, plot_b[i], c);
        }
        gfx_hline(VX0, mid, VW, C_GRIDL);
    }
    if (band_hits(VY0, VY0 + 10)) {
        char b[16]; seconds_label(b, view_f[0]);
        gfx_text(VX0, VY0, "DECAY", 1, C_DIM); gfx_text(VX0 + 36, VY0, b, 1, C_TEXT);
        rtext(VX1, VY0, fx->rev_width > 0.5f ? "WIDE" : "NARROW", C_DIM);
    }
}
static void view_track(void)
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
    int y = bot + 12;
    if (band_hits(y - 2, VY1)) {
        char a[24], b[12];
        tempura_format(tempura_track_info(TR_TUNE), canvas.tr[cur_x][TR_TUNE], b); strcpy(a, "TUNE "); strcat(a, b);
        tempura_format(tempura_track_info(TR_FINE), canvas.tr[cur_x][TR_FINE], b); strcat(a, " "); strcat(a, b);
        gfx_text(VX0, y, a, 1, C_TEXT);
        gfx_text(VX0, y + 16, "LEVEL", 1, C_DIM); hbar(VX0 + 36, y + 17, 70, 5, vnorm(canvas.tr[cur_x][TR_LEVEL]), C_TEXT);
        gfx_text(124, y + 16, "PAN", 1, C_DIM);
        gfx_hline(148, y + 19, 76, C_GRIDL); dot(148 + (int)((tr->pan + 1.0f) * 38.0f), y + 19, 2, C_TEXT);
    }
}
static void view_keys(void)
{
    uint16_t ec = emitter_color[sel];
    int top = VY0 + 20, bot = VY0 + 96, ww = VW / 16;
    unsigned cells = canvas.g[G_KEY_MODE] == KEYS_CELLS;
    if (band_hits(top, bot + 30)) {
        for (unsigned w = 0; w < 16; ++w) {
            unsigned k = white_keys[w];
            uint16_t c = (prev_keys >> k) & 1U ? ec : RGB(170, 164, 176);
            gfx_fill(VX0 + (int)w * ww, top, ww - 1, bot - top, c);
            if (cells) {
                char lab[3] = {(char)('1' + (w & 7U)), 0, 0};
                ctext(VX0 + (int)w * ww + ww / 2, bot - 12, lab, 1, C_BG);
            }
        }
        for (unsigned k = 0; k < 27; ++k) {
            unsigned m = (5U + k) % 12U;     /* the lowest key is an F */
            if (!(m == 1 || m == 3 || m == 6 || m == 8 || m == 10)) continue;
            unsigned w = 0; while (w < 16 && white_keys[w] < k) ++w;
            gfx_fill(VX0 + (int)w * ww - ww / 3, top, ww * 2 / 3, (bot - top) * 3 / 5, (prev_keys >> k) & 1U ? ec : RGB(30, 26, 34));
        }
    }
    if (band_hits(VY0, VY0 + 16)) ctext(120, VY0, cells ? "KEYS PLAY CELLS" : "KEYS PLAY NOTES", 2, ec);
    if (band_hits(bot + 6, VY1)) {
        char a[28], b[12];
        if (cells) { strcpy(a, "WHITE KEYS = TRACKS, 2 ROWS"); gfx_text(VX0, bot + 10, a, 1, C_DIM); }
        else {
            strcpy(a, "LOWEST KEY F"); vnum(b, (float)(3 + octave), 0, NULL); strcat(a, b);
            gfx_text(VX0, bot + 10, a, 1, C_TEXT);
            tempura_format(tempura_global_info(G_ROOT), canvas.g[G_ROOT], b); strcpy(a, "TRANSPOSE "); strcat(a, b);
            gfx_text(VX0, bot + 24, a, 1, C_DIM);
        }
        gfx_text(VX0, bot + 40, canvas.g[G_KEY_LAYER] ? "PLAYS ALL PLACED EMITTERS" : "PLAYS THE SELECTED EMITTER", 1, C_DIM);
    }
}
static void view_midi(void)
{
    if (!band_hits(VY0, VY1)) return;
    unsigned active = tempura_midi_activity();
    static const char *const ins[3] = {"USB", "DIN", "BLE"};
    for (unsigned i = 0; i < 3; ++i) {
        int y = VY0 + 14 + (int)i * 30;
        gfx_rect(VX0, y, 40, 20, C_DIM); ctext(VX0 + 20, y + 6, ins[i], 1, C_TEXT);
        gfx_hline(VX0 + 42, y + 10, 30, active ? C_AMBER : C_GRIDL);
    }
    gfx_vline(VX0 + 72, VY0 + 24, 61, active ? C_AMBER : C_GRIDL);
    if (canvas.g[G_MIDI_MODE]) {
        for (unsigned e = 0; e < TP_EMITTERS; ++e) {
            int y = VY0 + 8 + (int)e * 22;
            char a[12] = "CH "; a[3] = (char)('1' + e); a[4] = 0; strcat(a, " > E"); a[8] = (char)('1' + e); a[9] = 0;
            gfx_fill(100, y, 124, 18, gfx_mix(C_BG, emitter_color[e], 60)); gfx_text(108, y + 5, a, 1, C_TEXT);
        }
    } else {
        char a[16] = "ANY CH > E"; a[10] = (char)('1' + sel); a[11] = 0;
        gfx_fill(100, VY0 + 30, 124, 24, gfx_mix(C_BG, emitter_color[sel], 70)); gfx_text(108, VY0 + 38, a, 1, C_TEXT);
    }
    char a[28], b[12];
    tempura_format(tempura_global_info(G_CELL_CH), canvas.g[G_CELL_CH], b);
    strcpy(a, "CELL NOTES ON CH "); strcat(a, b);
    gfx_text(VX0, VY1 - 36, a, 1, C_DIM);
    strcpy(a, "KEYS OUT TO "); strcat(a, tempura_list_name(tempura_global_info(G_KEY_OUT), canvas.g[G_KEY_OUT]));
    gfx_text(VX0, VY1 - 22, a, 1, C_DIM);
    unsigned clock = tempura_midi_clock_bpm();
    strcpy(a, "CLOCK IN ");
    if (clock) { vnum(b, (float)clock, 0, " BPM"); strcat(a, b); } else strcat(a, "NONE");
    gfx_text(VX0, VY1 - 8, a, 1, clock ? C_GREEN : C_DIM);
}
static void view_live(void)
{
    struct tp_stats st; tempura_stats(&st);
    unsigned live_track = TP_TRACKS;
    for (unsigned t = 0; t < TP_TRACKS && live_track == TP_TRACKS; ++t) if (canvas.tr[t][TR_SOURCE] == TP_SOURCE_LIVE) live_track = t;
    int top = VY0 + 22, bot = VY0 + 80, mid = (top + bot) / 2;
    unsigned frozen = canvas.g[G_LIVE_FREEZE];
    if (band_hits(VY0, VY0 + 16)) {
        gfx_text(VX0, VY0, tempura_list_name(tempura_global_info(G_LIVE_SRC), canvas.g[G_LIVE_SRC]), 2, C_TEAL);
        rtext(VX1, VY0 + 4, frozen ? "FROZEN" : "RECORDING", frozen ? C_AMBER : C_BEET);
    }
    if (band_hits(top, bot)) {
        gfx_rect(VX0, top, VW, bot - top, C_GRIDL);
        if (live_track < TP_TRACKS)
            for (int x = 0; x < VW; x += 2) {
                int h = (int)tempura_track_peak(live_track, (unsigned)(x * 160 / VW)) * (bot - top - 4) / 2 / 255;
                gfx_vline(VX0 + x, mid - h, 2 * h + 1, RGB(70, 120, 110));
            }
        else ctext(120, mid - 3, "NO TRACK SET TO LIVE IN (SCL)", 1, C_DIM);
        gfx_vline(VX0 + (int)tempura_live_head() * VW / 160, top, bot - top, frozen ? C_DIM : C_BEET);
    }
    if (band_hits(bot + 4, VY1)) {
        char a[24], b[12];
        seconds_label(b, (float)st.heap_live / 2.0f / (float)TP_LIVE_RATE); strcpy(a, "TAPE LOOP "); strcat(a, b);
        gfx_text(VX0, bot + 8, a, 1, C_DIM);
        gfx_text(VX0, bot + 26, "IN GAIN", 1, C_DIM); hbar(VX0 + 56, bot + 27, VW - 56, 5, vnorm(canvas.g[G_LIVE_GAIN]), C_TEAL);
        gfx_text(VX0, bot + 42, "MONITOR", 1, C_DIM); hbar(VX0 + 56, bot + 43, VW - 56, 5, vnorm(canvas.g[G_LIVE_MONITOR]), C_TEXT);
        gfx_text(VX0, bot + 60, "REC+PLAY: FREEZE / RECORD", 1, C_DIM);
    }
}
static void view_arp(void)
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

static void draw_view(unsigned view)
{
    if (gfx_top + GFX_BAND <= GY - 2 || gfx_top >= PANEL_Y - 2) return;
    gfx_fill(0, GY - 2, GFX_W, PANEL_Y - GY, C_BG);
    switch (view) {
    case V_GRAIN: view_grain(); break;
    case V_POSITION: view_position(); break;
    case V_PITCH: view_pitch(); break;
    case V_MIX: view_mix(); break;
    case V_MODE: view_mode(); break;
    case V_ENV: view_env(); break;
    case V_FILTER: view_filter(); break;
    case V_LFO: view_lfo(); break;
    case V_SENDS: view_sends(); break;
    case V_MODFX: view_modfx(); break;
    case V_DELAY: view_delay(); break;
    case V_DLYMOD: view_dlymod(); break;
    case V_REVERB: view_reverb(); break;
    case V_TRACK: view_track(); break;
    case V_KEYS: view_keys(); break;
    case V_MIDI: view_midi(); break;
    case V_LIVE: view_live(); break;
    case V_ARP: view_arp(); break;
    default: break;
    }
}
