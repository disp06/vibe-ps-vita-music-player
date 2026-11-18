/* Style 1: Retro-Futurism.
 * Thin-line HUD after the reference clips: framed panels with corner brackets,
 * spinning tape reels whose packs shrink/grow with playback progress, a live
 * oscilloscope, segmented spectrum and stereo level meters, radar sweep, a
 * segmented seek bar and a scan-line wipe between pages. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "ui.h"

static float g_reel; /* reel angle */
static float g_sweep;

static uint32_t k_acc_dark[6] = {0xB6FF3B, 0xFF3DA8, 0x3DE7FF, 0xFF8A1E, 0x7C93FF, 0xFF4848};
static uint32_t k_acc_light[6] = {0x3C7A00, 0xB0105E, 0x007A94, 0xB04A00, 0x2E3FB0, 0xB01818};

static uint32_t BG(void) { return cfg.dark ? col_hex(0x030706) : col_hex(0xE4EAE4); }
static uint32_t LN(void) { return cfg.dark ? col_hex(0xA8F0DC) : col_hex(0x0E3B33); }
static uint32_t DIM(void) { return col_alpha(LN(), cfg.dark ? 0.32f : 0.38f); }
static uint32_t FAINT(void) { return col_alpha(LN(), cfg.dark ? 0.12f : 0.14f); }
static uint32_t ACC(void) { return col_hex((cfg.dark ? k_acc_dark : k_acc_light)[cfg.accent % 6]); }
static uint32_t HOT(void) { return cfg.dark ? col_hex(0xFF7A1A) : col_hex(0xC04A00); }

static float label(float x, float y, const char *s, uint32_t c) { return font_draw_ex(FONT_MONO, 13, x, y, c, s, 1.6f); }

static void brackets(float x, float y, float w, float h, float l, uint32_t c) {
    gfx_rect(x, y, l, 2, c); gfx_rect(x, y, 2, l, c);
    gfx_rect(x + w - l, y, l, 2, c); gfx_rect(x + w - 2, y, 2, l, c);
    gfx_rect(x, y + h - 2, l, 2, c); gfx_rect(x, y + h - l, 2, l, c);
    gfx_rect(x + w - l, y + h - 2, l, 2, c); gfx_rect(x + w - 2, y + h - l, 2, l, c);
}

static void panel(float x, float y, float w, float h, const char *title) {
    gfx_rect(x, y, w, h, cfg.dark ? col_alpha(LN(), 0.025f) : col_alpha(LN(), 0.04f));
    gfx_frame(x, y, w, h, 1, DIM());
    brackets(x - 1, y - 1, w + 2, h + 2, 9, LN());
    if (title) {
        gfx_rect(x + 8, y + 7, 4, 4, ACC());
        label(x + 18, y + 3, title, col_alpha(LN(), 0.85f));
    }
}

static void segbar(float x, float y, float w, float h, int n, float v, int vertical) {
    float gap = 2;
    float seg = ((vertical ? h : w) - gap * (n - 1)) / n;
    int lit = (int)(v * n + 0.5f);
    for (int i = 0; i < n; i++) {
        uint32_t c = i < lit ? (i >= n * 0.85f ? HOT() : i >= n * 0.65f ? ACC() : LN()) : FAINT();
        if (vertical) gfx_rect(x, y + h - (i + 1) * seg - i * gap, w, seg, c);
        else gfx_rect(x + i * (seg + gap), y, seg, h, c);
    }
}

static void box_btn(float x, float y, float w, float h, int act, int lit, const char *txt, int icon) {
    uint32_t fg = lit ? BG() : LN();
    if (lit) gfx_rect(x, y, w, h, LN());
    else { gfx_frame(x, y, w, h, 1.5f, col_alpha(LN(), 0.7f)); }
    float cx = x + w / 2, cy = y + h / 2;
    switch (icon) {
    case 1: icon_prev(cx, cy, 9, fg); break;
    case 2: if (ui_playing()) icon_pause(cx, cy, 9, fg); else icon_play(cx + 1, cy, 10, fg); break;
    case 3: icon_next(cx, cy, 9, fg); break;
    case 4: icon_stop(cx, cy, 8, fg); break;
    default: font_draw_center(FONT_MONO, 14, cx, cy - 9, fg, txt); break;
    }
    ui_hit(x - 3, y - 6, w + 6, h + 12, act, 0);
}

/* ---------------- tape deck ---------------- */
static void reel(float cx, float cy, float r, float pack, float ang) {
    uint32_t ln = LN();
    gfx_arc(cx, cy, r, 1.5f, 0, 6.2832f, DIM());
    /* tape pack */
    gfx_arc(cx, cy, (pack + r * 0.28f) / 2, pack - r * 0.28f, 0, 6.2832f, col_alpha(LN(), cfg.dark ? 0.14f : 0.18f));
    gfx_arc(cx, cy, pack, 2, 0, 6.2832f, ln);
    /* hub with three spokes */
    gfx_arc(cx, cy, r * 0.28f, 2, 0, 6.2832f, ln);
    gfx_circle(cx, cy, r * 0.07f, ln);
    for (int i = 0; i < 3; i++) {
        float a = ang + i * 2.0944f;
        gfx_line(cx + cosf(a) * r * 0.1f, cy + sinf(a) * r * 0.1f, cx + cosf(a) * r * 0.26f, cy + sinf(a) * r * 0.26f, 2.5f, ln);
        /* window notches on the outer flange */
        float b = ang + i * 2.0944f + 1.0472f;
        gfx_arc(cx, cy, r * 0.86f, 3, b - 0.12f, b + 0.12f, ACC());
    }
}

static void tape_deck(float x, float y, float w, float h) {
    float r = fminf(h * 0.42f, w * 0.17f);
    float cy = y + h * 0.48f;
    float lx = x + w * 0.2f, rx = x + w * 0.8f;
    float p = ui_progress();
    float pmin = r * 0.34f, pmax = r * 0.9f;
    float pl = pmin + (pmax - pmin) * sqrtf(1 - p), pr = pmin + (pmax - pmin) * sqrtf(p);
    reel(lx, cy, r, pl, g_reel);
    reel(rx, cy, r, pr, g_reel * 1.07f);
    /* tape path: packs -> guide rollers -> head */
    float gy = y + h - 10;
    float g1 = x + w * 0.06f, g2 = x + w * 0.94f;
    uint32_t ln = LN();
    gfx_line(lx - pl, cy, g1, gy - 6, 1.5f, ln);
    gfx_line(rx + pr, cy, g2, gy - 6, 1.5f, ln);
    gfx_line(g1, gy - 6, x + w * 0.5f - 40, gy, 1.5f, ln);
    gfx_line(x + w * 0.5f + 40, gy, g2, gy - 6, 1.5f, ln);
    gfx_line(x + w * 0.5f - 40, gy, x + w * 0.5f + 40, gy, 1.5f, ln);
    gfx_arc(g1, gy - 6, 5, 1.5f, 0, 6.2832f, ln);
    gfx_arc(g2, gy - 6, 5, 1.5f, 0, 6.2832f, ln);
    /* head block + capstan */
    gfx_frame(x + w * 0.5f - 14, gy - 18, 28, 12, 1.5f, ACC());
    gfx_circle(x + w * 0.5f + 52, gy - 4, 4, ln);
    /* centre window: azimuth scale + moving index */
    float sx0 = x + w * 0.36f, sx1 = x + w * 0.64f, sy = cy;
    for (int i = 0; i <= 24; i++) {
        float tx = sx0 + (sx1 - sx0) * i / 24;
        gfx_rect(tx, sy - (i % 6 == 0 ? 8 : 4), 1, i % 6 == 0 ? 16 : 8, DIM());
    }
    float ix = sx0 + (sx1 - sx0) * p;
    gfx_rect(ix - 1.5f, sy - 16, 3, 32, ACC());
    gfx_line(lx + r * 0.95f, cy - r * 0.92f, x + w * 0.5f - 12, y + 6, 1, DIM());
    gfx_line(x + w * 0.5f + 12, y + 6, rx - r * 0.95f, cy - r * 0.92f, 1, DIM());
    gfx_circle(x + w * 0.5f, y + 6, 3, LN());
}

/* ---------------- viz panels ---------------- */
static void scope(float x, float y, float w, float h) {
    panel(x, y, w, h, "SCOPE // CH-A");
    float ix = x + 10, iy = y + 22, iw = w - 20, ih = h - 30;
    for (int i = 1; i < 8; i++) gfx_rect(ix + iw * i / 8, iy, 1, ih, FAINT());
    for (int i = 1; i < 4; i++) gfx_rect(ix, iy + ih * i / 4, iw, 1, FAINT());
    gfx_rect(ix, iy + ih / 2, iw, 1, DIM());
    float cy = iy + ih / 2, amp = ih * 0.46f;
    float px = ix, py = cy;
    int n = 128;
    for (int i = 0; i < n; i++) {
        float v = ui.wave[i * NWAVE / n] * 1.4f;
        if (!ui_playing()) v = 0.04f * sinf(i * 0.25f + (float)ui.t * 2);
        v = ui_clamp(v, -1, 1);
        float xx = ix + iw * i / (n - 1), yy = cy - v * amp;
        if (i) {
            gfx_line(px, py, xx, yy, 4, col_alpha(ACC(), 0.18f));
            gfx_line(px, py, xx, yy, 1.6f, ACC());
        }
        px = xx; py = yy;
    }
    gfx_rect(x - 4, cy - 4, 4, 8, LN());
    gfx_rect(x + w, cy - 4, 4, 8, LN());
}

static void spectrum(float x, float y, float w, float h) {
    panel(x, y, w, h, "SPECTRUM // 24 BAND");
    float ix = x + 10, iy = y + 24, iw = w - 20, ih = h - 34;
    float bw = iw / NBANDS;
    int segs = (int)(ih / 6);
    for (int b = 0; b < NBANDS; b++) {
        segbar(ix + b * bw + 1, iy, bw - 3, ih, segs, ui.bands[b], 1);
        float py = iy + ih - ui.peaks[b] * ih;
        if (ui.peaks[b] > 0.02f) gfx_rect(ix + b * bw + 1, py - 2, bw - 3, 2, HOT());
    }
}

static void levels(float x, float y, float w, float h) {
    panel(x, y, w, h, "STEREO LEVEL");
    float iy = y + 24, bh = (h - 34) / 2 - 3;
    label(x + 10, iy - 1, "L", LN());
    label(x + 10, iy + bh + 5, "R", LN());
    segbar(x + 26, iy, w - 36, bh, 32, ui_clamp(ui.lvl_l * 1.2f, 0, 1), 0);
    segbar(x + 26, iy + bh + 6, w - 36, bh, 32, ui_clamp(ui.lvl_r * 1.2f, 0, 1), 0);
}

static void radar(float cx, float cy, float r) {
    gfx_arc(cx, cy, r, 1.2f, 0, 6.2832f, DIM());
    gfx_arc(cx, cy, r * 0.55f, 1, 0, 6.2832f, FAINT());
    gfx_rect(cx - r, cy, 2 * r, 1, FAINT());
    gfx_rect(cx, cy - r, 1, 2 * r, FAINT());
    for (int i = 0; i < 6; i++) gfx_arc(cx, cy, r * 0.5f, r, g_sweep - 0.12f * (i + 1), g_sweep - 0.12f * i, col_alpha(ACC(), 0.35f - i * 0.055f));
    gfx_line(cx, cy, cx + cosf(g_sweep) * r, cy + sinf(g_sweep) * r, 1.5f, ACC());
}

static void counter(float x, float y, float px) {
    char a[16], b[16], s[40];
    fmt_time(ui_pos(), a);
    fmt_time(ui.ps.dur, b);
    snprintf(s, sizeof s, "%s / %s", a, b);
    gfx_tri(x, y + px * 0.25f, x, y + px * 0.85f, x + px * 0.3f, y + px * 0.55f, ACC());
    float w = font_draw_ex(FONT_MONO, px, x + px * 0.6f, y, LN(), s, px * 0.08f);
    float ex = x + px * 1.2f + w;
    gfx_tri(ex, y + px * 0.25f, ex, y + px * 0.85f, ex - px * 0.3f, y + px * 0.55f, ACC());
}

static void seek(float x, float y, float w, float h) {
    gfx_frame(x, y, w, h, 1, DIM());
    int n = 60;
    float p = ui_progress();
    float sw = (w - 8) / n;
    for (int i = 0; i < n; i++) {
        float f = (i + 0.5f) / n;
        gfx_rect(x + 4 + i * sw + 1, y + 4, sw - 2, h - 8, f <= p ? (ui.seeking ? ACC() : LN()) : FAINT());
    }
    for (int i = 0; i <= 10; i++) gfx_rect(x + w * i / 10, y + h + 2, 1, i % 5 ? 4 : 8, DIM());
    label(x, y + h + 8, "00", DIM());
    font_draw_right(FONT_MONO, 13, x + w, y + h + 8, DIM(), "END");
    gfx_rect(x + 4 + (w - 8) * p - 1, y - 6, 3, h + 12, ACC());
    ui_seek_area(x, y, w, h);
}

static void title_block(float x, float y, float w, float px) {
    char s[96];
    snprintf(s, sizeof s, "NOW PLAYING // CASSETTE %c", 'A' + (ui.ps.qindex % 26));
    label(x, y, s, DIM());
    font_draw_fit_ex(FONT_MONO, px, x, y + 16, w, LN(), ui_title(), 0.5f);
    float ax = x + label(x, y + 18 + px * 1.25f, "ARTIST // ", ACC());
    font_draw_fit_ex(FONT_MONO, 15, ax, y + 18 + px * 1.25f - 2, w - (ax - x), LN(), ui_artist(), 1.0f);
    if (ui.ps.codec) {
        char sig[96];
        snprintf(sig, sizeof sig, "%d.%dKHZ // %d-BIT // %s // %s%s", ui.ps.rate / 1000, (ui.ps.rate % 1000) / 100, ui.ps.bits,
                 ui.ps.codec, ui.ps.channels > 1 ? "STEREO" : "MONO", "");
        label(x, y + 40 + px * 1.25f, sig, DIM());
    }
}

static void controls(float x, float y, float w, float h, int rows) {
    struct { int act, lit, icon; const char *t; } b[8] = {
        {ACT_PREV, 0, 1, ""}, {ACT_PLAYPAUSE, 1, 2, ""}, {ACT_NEXT, 0, 3, ""}, {ACT_STOP, 0, 4, ""},
        {ACT_SHUFFLE, ui.ps.shuffle, 0, "SHF"},
        {ACT_REPEAT, ui.ps.repeat != 0, 0, ui.ps.repeat == REPEAT_ONE ? "RPT-1" : "RPT"},
        {ACT_ALWAYSON, cfg.always_on, 0, "A-ON"}, {ACT_SCREENOFF, 0, 0, "BLANK"}};
    int per = 8 / rows;
    float gap = 8, bw = (w - gap * (per - 1)) / per;
    for (int i = 0; i < 8; i++) {
        int r = i / per, c = i % per;
        box_btn(x + c * (bw + gap), y + r * (h + 10), bw, h, b[i].act, b[i].lit, b[i].t, b[i].icon);
    }
}

/* ---------------- pages ---------------- */
static void page_deck(void) {
    float W = ui.W, H = ui.H;
    if (ui.land) {
        panel(24, 58, 580, 282, NULL);
        title_block(36, 66, 556, 26);
        tape_deck(40, 168, 548, 166);
        levels(620, 58, 316, 70);
        spectrum(620, 136, 316, 112);
        scope(620, 256, 316, 84);
        counter(26, 350, 34);
        radar(W - 50, 370, 18);
        char q[48];
        if (ui.ps.bitrate) snprintf(q, sizeof q, "TRK %02d/%02d  %dKBPS", ui.ps.qindex + 1, ui.ps.qcount, ui.ps.bitrate);
        else snprintf(q, sizeof q, "TRK %02d/%02d", ui.ps.qindex + 1, ui.ps.qcount);
        font_draw_right(FONT_MONO, 13, W - 84, 356, DIM(), q);
        seek(26, 400, W - 52, 18);
        controls(26, 446, W - 52, 40, 1);
    } else {
        panel(20, 56, W - 40, 342, NULL);
        title_block(32, 64, W - 64, 24);
        tape_deck(30, 168, W - 60, 222);
        counter(24, 410, 32);
        radar(W - 44, 428, 18);
        seek(24, 462, W - 48, 18);
        controls(24, 508, W - 48, 42, 2);
        levels(20, 622, W - 40, 70);
        spectrum(20, 700, W - 40, 118);
        scope(20, 826, W - 40, 82);
    }
}

static void page_files(void) {
    float W = ui.W, H = ui.H;
    float x = 24, y = 58, w = W - 48;
    float bottom = H - (ui.land ? 112 : 136);
    char t[600];
    snprintf(t, sizeof t, "FILE SYSTEM // %s", ui.br.path[0] ? ui.br.path : "DEVICES");
    panel(x, y, w, bottom - y, NULL);
    font_draw_fit_ex(FONT_MONO, 13, x + 18, y + 4, w - 140, LN(), t, 1.6f);
    gfx_rect(x + 8, y + 8, 4, 4, ACC());
    if (ui.br.path[0]) { box_btn(x + w - 92, y + 6, 80, 22, ACT_UP, 0, "< UP", 0); }
    float ly = y + 36, lh = bottom - ly - 8, rh = ui.land ? 34 : 40;
    ui_list_area(x, ly, w - 20, lh, rh);
    gfx_clip(x + 2, ly, w - 4, lh);
    int first = (int)(ui.scroll / rh);
    if (first < 0) first = 0;
    for (int i = first; i < ui.br.n; i++) {
        float ry = ly + i * rh - ui.scroll;
        if (ry > ly + lh) break;
        float e = ui_clamp((float)(ui.t - ui.list_t) * 6 - (i - first) * 0.25f, 0, 1);
        if (e <= 0) continue;
        Entry *en = &ui.br.e[i];
        int sel = i == ui.sel;
        uint32_t fg = sel ? BG() : LN();
        if (sel) gfx_rect(x + 8, ry + 2, (w - 36) * e, rh - 4, LN());
        else if (i % 2) gfx_rect(x + 8, ry + 2, w - 36, rh - 4, col_alpha(LN(), 0.03f));
        char idx[16];
        snprintf(idx, sizeof idx, "%03d", i + 1);
        float ty = ry + (rh - 18) / 2 - 1;
        font_draw_ex(FONT_MONO, 15, x + 16, ty, sel ? fg : DIM(), idx, 1);
        const char *tag = "DIR";
        char ext[8] = "";
        if (!ui.br.path[0]) tag = "DEV";
        else if (!en->is_dir) {
            const char *d = strrchr(en->name, '.');
            int k = 0;
            for (d = d ? d + 1 : ""; d[k] && k < 4; k++) ext[k] = d[k] >= 'a' && d[k] <= 'z' ? d[k] - 32 : d[k];
            ext[k] = 0;
            tag = ext;
        }
        char tg[12];
        snprintf(tg, sizeof tg, "[%s]", tag);
        font_draw_ex(FONT_MONO, 15, x + 64, ty, sel ? fg : (en->is_dir ? ACC() : DIM()), tg, 1);
        font_draw_fit_ex(FONT_MONO, 17, x + 130, ty - 1, w - 170 - (e < 1 ? 0 : 0), fg, en->name, 0.6f);
        if (!ui.br.path[0]) {
            const char *lbl = browser_device_label(en->name);
            font_draw_right(FONT_MONO, 13, x + w - 36, ty + 2, sel ? fg : DIM(), lbl);
        }
    }
    gfx_noclip();
    /* scroll rail */
    float total = ui.br.n * rh;
    if (total > lh) {
        float rx = x + w - 14, th = lh * lh / total, tp = ly + (lh - th) * ui_clamp(ui.scroll / (total - lh), 0, 1);
        gfx_rect(rx, ly, 2, lh, FAINT());
        gfx_rect(rx - 2, tp, 6, th, LN());
    }
    if (!ui.br.n) label(x + 16, ly + 8, "NO AUDIO FILES // MP3 OGG FLAC WAV", DIM());

    /* status strip */
    float sy = bottom + 10, sh = H - 46 - sy;
    panel(x, sy, w, sh, NULL);
    char st[300];
    snprintf(st, sizeof st, "%s // ", ui_playing() ? "PLAY" : ui.ps.status == PS_PAUSED ? "PAUSE" : "IDLE");
    float tx = x + 14 + label(x + 14, sy + 8, st, ACC());
    font_draw_fit_ex(FONT_MONO, 15, tx, sy + 6, w - (tx - x) - 220, LN(), ui_title(), 0.6f);
    ui_hit(x, sy, w - 210, sh, ACT_PAGE, PAGE_PLAYING);
    char a[16], b[16], tm[40];
    fmt_time(ui_pos(), a);
    fmt_time(ui.ps.dur, b);
    snprintf(tm, sizeof tm, "%s / %s", a, b);
    font_draw_right(FONT_MONO, 15, x + w - 14, sy + 6, LN(), tm);
    float bx = x + 14, bw2 = w - 28, by = sy + sh - 16;
    gfx_rect(bx, by, bw2, 4, FAINT());
    gfx_rect(bx, by, bw2 * ui_progress(), 4, LN());
    ui_seek_area(bx, by, bw2, 4);
    float s2 = sh - 34;
    if (s2 > 12) {
        box_btn(x + w - 200, sy + 26, 56, s2, ACT_PREV, 0, "", 1);
        box_btn(x + w - 136, sy + 26, 56, s2, ACT_PLAYPAUSE, 1, "", 2);
        box_btn(x + w - 72, sy + 26, 56, s2, ACT_NEXT, 0, "", 3);
    }
}

static void page_sys(void) {
    float W = ui.W, H = ui.H;
    float x = 24, y = 58, w = W - 48, h = H - 46 - y;
    panel(x, y, w, h, "SYSTEM CONFIGURATION // SAVED TO UX0:DATA/VIBE");
    float ly = y + 34, lh = h - 44, rh = ui.land ? 46 : 74;
    ui_list_area(x, ly, w, lh, rh);
    gfx_clip(x + 2, ly, w - 4, lh);
    for (int i = 0; i < NSET; i++) {
        float ry = ly + i * rh - ui.set_scroll;
        if (ry + rh < ly || ry > ly + lh) continue;
        int sel = i == ui.set_sel;
        if (sel) { gfx_rect(x + 8, ry + 2, 4, rh - 4, ACC()); gfx_rect(x + 14, ry + 2, w - 28, rh - 4, col_alpha(LN(), 0.07f)); }
        char lab[64], v[96], vv[110];
        snprintf(lab, sizeof lab, "%02d  ", i + 1);
        int k = strlen(lab);
        for (const char *p = set_label(i); *p && k < 60; p++) lab[k++] = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
        lab[k] = 0;
        set_value(i, v, sizeof v);
        for (char *p = v; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        snprintf(vv, sizeof vv, "[ %s ]", v);
        float ty = ui.land ? ry + 13 : ry + 10;
        float lw = font_draw_ex(FONT_MONO, 16, x + 24, ty, sel ? LN() : col_alpha(LN(), 0.8f), lab, 1.2f);
        if (ui.land) {
            float vw = font_width_ex(FONT_MONO, 15, vv, 0.8f);
            float vx = x + w - 24 - vw;
            for (float dx = x + 30 + lw; dx < vx - 10; dx += 8) gfx_rect(dx, ty + 12, 2, 2, FAINT());
            font_draw_ex(FONT_MONO, 15, vx, ty + 1, sel ? ACC() : LN(), vv, 0.8f);
        } else {
            font_draw_fit_ex(FONT_MONO, 15, x + 56, ty + 28, w - 80, sel ? ACC() : DIM(), vv, 0.8f);
        }
    }
    gfx_noclip();
}

static void draw_page(int p) {
    if (p == PAGE_PLAYING) page_deck();
    else if (p == PAGE_LIBRARY) page_files();
    else page_sys();
}

static void chrome(void) {
    float W = ui.W, H = ui.H;
    gfx_rect(0, 0, W, H, BG());
    /* faint grid */
    for (float gx = 0; gx < W; gx += 32) gfx_rect(gx, 0, 1, H, col_alpha(LN(), 0.025f));
    for (float gy = 0; gy < H; gy += 32) gfx_rect(0, gy, W, 1, col_alpha(LN(), 0.025f));
    gfx_frame(8, 8, W - 16, H - 16, 1, FAINT());
    brackets(8, 8, W - 16, H - 16, 18, DIM());
    for (int i = 0; i < 9; i++) {
        float ty = H * 0.25f + i * (H * 0.5f / 8);
        gfx_rect(12, ty, i % 4 ? 5 : 9, 1, DIM());
        gfx_rect(W - 12 - (i % 4 ? 5 : 9), ty, i % 4 ? 5 : 9, 1, DIM());
    }
    /* top bar: tabs double as page navigation */
    static const char *tabs[3] = {"DECK", "FILES", "SYS"};
    float tx = ui.land ? 300 : 24, ty = 20, tw = ui.land ? 92 : 80;
    if (ui.land) {
        label(26, 24, "VIBE // AUDIO DECK", LN());
        gfx_circle(18 + 2, 31, 3, ui_playing() && fmodf((float)ui.t, 1.0f) < 0.6f ? HOT() : FAINT());
    }
    for (int i = 0; i < 3; i++) {
        float bx = tx + i * (tw + 6);
        int on = ui.page == i;
        if (on) gfx_rect(bx, ty, tw, 24, LN());
        else gfx_frame(bx, ty, tw, 24, 1, DIM());
        font_draw_center(FONT_MONO, 14, bx + tw / 2, ty + 3, on ? BG() : LN(), tabs[i]);
        ui_hit(bx, ty - 8, tw, 40, ACT_PAGE, i);
    }
    int h, m;
    sys_clock(&h, &m);
    char clk[32];
    snprintf(clk, sizeof clk, "%02d:%02d", h, m);
    float rx = W - 24;
    icon_battery(rx - 30, 24, 26, 14, sys_battery(), LN());
    font_draw_right(FONT_MONO, 15, rx - 40, 22, LN(), clk);
    /* bottom hint strip, like the reference "OK  FDAI  MFD  RDR  NAV" */
    static const char *hints_l[] = {"L/R SKIP", "X PLAY", "O BACK", "TRI FILES", "SQ ALWAYS-ON", "START SYS", "SELECT BLANK"};
    static const char *hints_p[] = {"L/R SKIP", "TRI FILES", "SQ A-ON", "SEL BLANK"};
    const char **hints = ui.land ? hints_l : hints_p;
    int nh = ui.land ? 7 : 4;
    float hy = H - 34, hw = (W - 48) / nh;
    for (int i = 0; i < nh; i++) {
        float cx = 24 + hw * i + hw / 2;
        font_draw_center(FONT_MONO, 12, cx, hy, DIM(), hints[i]);
        gfx_rect(cx - hw * 0.3f, hy + 17, hw * 0.6f, 2, FAINT());
    }
}

static void r_draw(UI *u) {
    if (ui_playing()) g_reel -= ui.dt * 2.4f;
    g_sweep += ui.dt * 1.6f;
    chrome();
    float e = ui_ease(ui.page_t);
    if (ui.page_t < 1) {
        /* scan-line wipe: new page above the beam, old page below it */
        float by = 50 + (ui.H - 96) * e;
        ui.hits_on = 0;
        gfx_clip_outer(0, by, ui.W, ui.H - by);
        draw_page(ui.prev_page);
        ui.hits_on = 1;
        gfx_clip_outer(0, 0, ui.W, by);
        draw_page(ui.page);
        gfx_clip_outer(0, 0, 0, 0);
        gfx_rect(0, by - 1, ui.W, 2, ACC());
        gfx_rect(0, by - 8, ui.W, 16, col_alpha(ACC(), 0.12f));
    } else {
        draw_page(ui.page);
    }
    /* scanlines */
    for (float y = 0; y < ui.H; y += 3) gfx_rect(0, y, ui.W, 1, cfg.dark ? 0x0D000000 : 0x08000000);

    float tt = (float)(ui.t - ui.toast_t);
    if (ui.toast[0] && tt < 2.0f) {
        char s[120];
        snprintf(s, sizeof s, ">> %s", ui.toast);
        for (char *p = s; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
        float w = font_width_ex(FONT_MONO, 16, s, 1.2f) + 40, x = (ui.W - w) / 2, y = ui.H * 0.42f;
        if (fmodf(tt, 0.5f) < 0.42f || tt > 0.6f) {
            gfx_rect(x, y, w, 40, BG());
            panel(x, y, w, 40, NULL);
            font_draw_ex(FONT_MONO, 16, x + 20, y + 10, ACC(), s, 1.2f);
        }
    }
}

const Skin skin_retro = {"retro", NULL, r_draw, NULL};
