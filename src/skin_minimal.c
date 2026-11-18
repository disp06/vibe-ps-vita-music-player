/* Optional style: clean minimal player. Flat, quiet, big artwork, one accent. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "ui.h"

static const uint32_t k_acc[6] = {0x8BC34A, 0xE91E63, 0x29B6F6, 0xFF9800, 0x5C6BC0, 0xF44336};
static float g_alpha = 1, g_oy;
static float g_tab_x, g_tab_w;

static uint32_t A(uint32_t c) { return col_alpha(c, g_alpha); }
static uint32_t BG(void) { return cfg.dark ? RGB(0, 0, 0) : col_hex(0xF5F5F3); }
static uint32_t FG(void) { return cfg.dark ? col_hex(0xF2F2F2) : col_hex(0x161616); }
static uint32_t SUB(void) { return cfg.dark ? col_hex(0x8A8A8A) : col_hex(0x7A7A7A); }
static uint32_t LINE(void) { return cfg.dark ? col_hex(0x262626) : col_hex(0xDDDDDA); }
static uint32_t ACC(void) { return col_hex(k_acc[cfg.accent % 6]); }

static void pill(float x, float y, float w, float h, uint32_t c) {
    float r = h / 2;
    gfx_rect(x + r, y, w - 2 * r, h, c);
    gfx_circle(x + r, y + r, r, c);
    gfx_circle(x + w - r, y + r, r, c);
}

static void art(float x, float y, float s) {
    if (ui.cover) gfx_tex_draw(ui.cover, 0, 0, gfx_tex_w(ui.cover), gfx_tex_h(ui.cover), x, y, s, s, A(RGB(255, 255, 255)));
    else {
        gfx_rect_grad_v(x, y, s, s, A(col_mix(ACC(), BG(), 0.35f)), A(col_mix(ACC(), BG(), 0.8f)));
        icon_note(x + s / 2, y + s / 2, s * 0.18f, A(col_alpha(RGB(255, 255, 255), 0.8f)));
    }
}

static void progress(float x, float y, float w) {
    float p = ui_progress();
    pill(x, y, w, 4, A(LINE()));
    if (p > 0.005f) pill(x, y, fmaxf(w * p, 4), 4, A(ACC()));
    gfx_circle(x + w * p, y + 2, ui.seeking ? 9 : 6, A(FG()));
    ui_seek_area(x, y, w, 4);
    char a[16], b[16];
    fmt_time(ui_pos(), a);
    fmt_time(ui.ps.dur, b);
    font_draw(FONT_REGULAR, 15, x, y + 12, A(SUB()), a);
    font_draw_right(FONT_REGULAR, 15, x + w, y + 12, A(SUB()), b);
}

static void controls(float cx, float cy, float gap) {
    uint32_t fg = A(FG());
    gfx_circle(cx, cy, 34, A(ACC()));
    if (ui_playing()) icon_pause(cx, cy, 13, A(RGB(255, 255, 255)));
    else icon_play(cx + 2, cy, 15, A(RGB(255, 255, 255)));
    ui_hit(cx - 40, cy - 40, 80, 80, ACT_PLAYPAUSE, 0);
    icon_prev(cx - gap, cy, 14, fg);
    ui_hit(cx - gap - 34, cy - 34, 68, 68, ACT_PREV, 0);
    icon_next(cx + gap, cy, 14, fg);
    ui_hit(cx + gap - 34, cy - 34, 68, 68, ACT_NEXT, 0);
    float g2 = gap * 2;
    icon_shuffle(cx - g2, cy, 13, 2.2f, ui.ps.shuffle ? A(ACC()) : A(SUB()));
    ui_hit(cx - g2 - 30, cy - 30, 60, 60, ACT_SHUFFLE, 0);
    icon_repeat(cx + g2, cy, 13, 2.2f, ui.ps.repeat == REPEAT_ONE, ui.ps.repeat ? A(ACC()) : A(SUB()));
    ui_hit(cx + g2 - 30, cy - 30, 60, 60, ACT_REPEAT, 0);
}

static void spectrum_line(float x, float y, float w, float h) {
    int n = NBANDS;
    float bw = w / n;
    for (int i = 0; i < n; i++) {
        float v = ui.bands[i];
        float bh = fmaxf(2, v * h);
        gfx_rect(x + i * bw + bw * 0.3f, y + h - bh, bw * 0.4f, bh, A(col_alpha(ACC(), 0.25f + v * 0.6f)));
    }
}

static void page_playing(void) {
    float W = ui.W, H = ui.H, oy = g_oy;
    if (ui.land) {
        float s = 330, x = 48, y = 92 + oy;
        art(x, y, s);
        float tx = x + s + 44, tw = W - tx - 48;
        font_draw_fit(FONT_REGULAR, 34, tx, y + 10, tw, A(FG()), ui_title());
        font_draw_fit(FONT_REGULAR, 21, tx, y + 62, tw, A(SUB()), ui_artist());
        font_draw_fit(FONT_LIGHT, 18, tx, y + 92, tw, A(SUB()), ui.ps.album);
        spectrum_line(tx, y + 128, tw, 46);
        progress(tx, y + 196, tw);
        controls(tx + tw / 2, y + 290, 82);
    } else {
        float s = W - 96, x = 48, y = 96 + oy;
        art(x, y, s);
        float ty = y + s + 34, tw = W - 96;
        font_draw_fit(FONT_REGULAR, 30, x, ty, tw, A(FG()), ui_title());
        font_draw_fit(FONT_REGULAR, 20, x, ty + 46, tw, A(SUB()), ui_artist());
        spectrum_line(x, ty + 86, tw, 40);
        progress(x, ty + 148, tw);
        controls(W / 2, ty + 240, 86);
    }
}

static void page_library(void) {
    float W = ui.W, H = ui.H, oy = g_oy;
    float x = 32, top = 84 + oy;
    char path[540];
    snprintf(path, sizeof path, "%s", ui.br.path[0] ? ui.br.path : "Devices");
    if (ui.br.path[0]) {
        icon_prev(x + 6, top + 12, 7, A(ACC()));
        ui_hit(0, top - 6, W, 40, ACT_UP, 0);
    }
    font_draw_fit(FONT_REGULAR, 17, x + (ui.br.path[0] ? 22 : 0), top, W - 2 * x - 22, A(SUB()), path);
    float mini = 72;
    float ly = top + 38, lh = H - mini - ly - 4, rh = ui.land ? 52 : 60;
    ui_list_area(0, ly, W, lh, rh);
    gfx_clip(0, ly, W, lh);
    int first = (int)(ui.scroll / rh);
    if (first < 0) first = 0;
    for (int i = first; i < ui.br.n; i++) {
        float y = ly + i * rh - ui.scroll;
        if (y > ly + lh) break;
        float e = ui_ease((float)(ui.t - ui.list_t) * 4 - (i - first) * 0.08f);
        float sa = g_alpha;
        g_alpha *= e;
        Entry *en = &ui.br.e[i];
        int sel = i == ui.sel;
        if (sel) { gfx_rect(0, y, W, rh, A(cfg.dark ? col_hex(0x141414) : col_hex(0xE8E8E5))); gfx_rect(0, y, 4, rh, A(ACC())); }
        float cy = y + rh / 2;
        if (en->is_dir) icon_folder(x + 14, cy, 12, A(sel ? ACC() : SUB()));
        else icon_note(x + 14, cy, 13, A(sel ? ACC() : SUB()));
        char nm[256];
        snprintf(nm, sizeof nm, "%s", en->name);
        char *dot = en->is_dir ? NULL : strrchr(nm, '.');
        char ext[8] = "";
        if (dot) { snprintf(ext, sizeof ext, "%s", dot + 1); *dot = 0; }
        float ew = ext[0] ? font_width(FONT_REGULAR, 13, ext) + 16 : 0;
        font_draw_fit(FONT_REGULAR, 20, x + 44, cy - 14, W - x - 64 - ew - 44, A(FG()), nm);
        if (!ui.br.path[0]) font_draw_right(FONT_REGULAR, 14, W - x, cy - 9, A(SUB()), browser_device_label(en->name));
        if (ext[0]) {
            for (char *p = ext; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
            pill(W - x - ew, cy - 11, ew, 22, A(LINE()));
            font_draw_center(FONT_REGULAR, 13, W - x - ew / 2, cy - 9, A(SUB()), ext);
        }
        gfx_rect(x + 44, y + rh - 1, W - x - 44 - x, 1, A(LINE()));
        g_alpha = sa;
    }
    gfx_noclip();
    if (!ui.br.n) font_draw(FONT_REGULAR, 18, x, ly + 12, A(SUB()), "No MP3, OGG, FLAC or WAV files here");
    /* mini player */
    float my = H - mini;
    gfx_rect(0, my, W, mini, A(cfg.dark ? col_hex(0x0E0E0E) : col_hex(0xEBEBE8)));
    gfx_rect(0, my, W * ui_progress(), 2, A(ACC()));
    ui_seek_area(0, my, W, 2);
    art(16, my + 12, mini - 24);
    font_draw_fit(FONT_REGULAR, 18, 16 + mini, my + 13, W - mini - 160, A(FG()), ui_title());
    font_draw_fit(FONT_REGULAR, 14, 16 + mini, my + 39, W - mini - 160, A(SUB()), ui_artist());
    ui_hit(0, my + 6, W - 140, mini - 6, ACT_PAGE, PAGE_PLAYING);
    float cx = W - 76, cy = my + mini / 2;
    gfx_circle(cx, cy, 22, A(ACC()));
    if (ui_playing()) icon_pause(cx, cy, 9, A(RGB(255, 255, 255)));
    else icon_play(cx + 1, cy, 10, A(RGB(255, 255, 255)));
    ui_hit(cx - 28, my, 56, mini, ACT_PLAYPAUSE, 0);
    icon_next(W - 28, cy, 9, A(FG()));
    ui_hit(W - 50, my, 50, mini, ACT_NEXT, 0);
}

static void page_settings(void) {
    float W = ui.W, H = ui.H, oy = g_oy;
    float x = 32, ly = 84 + oy, lh = H - ly - 8, rh = ui.land ? 50 : 72;
    ui_list_area(0, ly, W, lh, rh);
    gfx_clip(0, ly, W, lh);
    for (int i = 0; i < NSET; i++) {
        float y = ly + i * rh - ui.set_scroll;
        if (y + rh < ly || y > ly + lh) continue;
        int sel = i == ui.set_sel;
        if (sel) { gfx_rect(0, y, W, rh, A(cfg.dark ? col_hex(0x141414) : col_hex(0xE8E8E5))); gfx_rect(0, y, 4, rh, A(ACC())); }
        char v[96];
        set_value(i, v, sizeof v);
        float ty = ui.land ? y + (rh - 22) / 2 : y + 12;
        font_draw(FONT_REGULAR, 19, x, ty, A(FG()), set_label(i));
        if (set_is_toggle(i)) {
            int on = set_toggle_on(i);
            float sx = W - x - 52, sy = y + rh / 2 - 14;
            pill(sx, sy, 52, 28, A(on ? ACC() : LINE()));
            gfx_circle(on ? sx + 38 : sx + 14, sy + 14, 11, A(RGB(255, 255, 255)));
        } else if (ui.land) font_draw_right(FONT_REGULAR, 17, W - x, ty + 1, A(SUB()), v);
        else font_draw_fit(FONT_REGULAR, 15, x, ty + 30, W - 2 * x, A(SUB()), v);
        gfx_rect(x, y + rh - 1, W - 2 * x, 1, A(LINE()));
    }
    gfx_noclip();
}

static void draw_page(int p) {
    if (p == PAGE_PLAYING) page_playing();
    else if (p == PAGE_LIBRARY) page_library();
    else page_settings();
}

static void header(void) {
    static const char *t[3] = {"Now Playing", "Library", "Settings"};
    float x = 32, y = 26;
    float tx[3], tw[3];
    for (int i = 0; i < 3; i++) {
        tw[i] = font_width(FONT_REGULAR, 18, t[i]);
        tx[i] = x;
        font_draw(FONT_REGULAR, 18, x, y, ui.page == i ? FG() : SUB(), t[i]);
        ui_hit(x - 10, 0, tw[i] + 20, 64, ACT_PAGE, i);
        x += tw[i] + 28;
    }
    /* sliding underline */
    float k = 1 - expf(-ui.dt * 14);
    if (g_tab_w == 0) { g_tab_x = tx[ui.page]; g_tab_w = tw[ui.page]; }
    g_tab_x += (tx[ui.page] - g_tab_x) * k;
    g_tab_w += (tw[ui.page] - g_tab_w) * k;
    pill(g_tab_x, y + 30, g_tab_w, 3, ACC());
    /* quick toggles */
    float rx = ui.W - 34, cy = y + 12;
    icon_rotate(rx, cy, 11, 2, SUB());
    ui_hit(rx - 22, 0, 44, 64, ACT_ROTATE, 0);
    icon_moon(rx - 46, cy, 15, SUB(), BG());
    ui_hit(rx - 68, 0, 44, 64, ACT_SCREENOFF, 0);
    icon_bulb(rx - 92, cy, 11, 2, cfg.always_on, cfg.always_on ? ACC() : SUB());
    ui_hit(rx - 114, 0, 44, 64, ACT_ALWAYSON, 0);
}

static void n_draw(UI *u) {
    gfx_rect(0, 0, ui.W, ui.H, BG());
    float e = ui_ease(ui.page_t);
    if (ui.page_t < 1) {
        ui.hits_on = 0;
        g_alpha = 1 - e;
        g_oy = -12 * e;
        draw_page(ui.prev_page);
        ui.hits_on = 1;
    }
    g_alpha = e;
    g_oy = 16 * (1 - e);
    draw_page(ui.page);
    g_alpha = 1;
    g_oy = 0;
    header();
    float tt = (float)(ui.t - ui.toast_t);
    if (ui.toast[0] && tt < 2.0f) {
        float k = tt < 0.15f ? tt / 0.15f : tt > 1.7f ? (2 - tt) / 0.3f : 1;
        float w = font_width(FONT_REGULAR, 16, ui.toast) + 40, x = (ui.W - w) / 2, y = ui.H - 120;
        pill(x, y, w, 38, col_alpha(cfg.dark ? col_hex(0x2A2A2A) : col_hex(0x222222), k));
        font_draw(FONT_REGULAR, 16, x + 20, y + 8, col_alpha(RGB(255, 255, 255), k), ui.toast);
    }
}

const Skin skin_minimal = {"minimal", NULL, n_draw, NULL};
