/* Style 2: Windows Phone / Zune "Metro".
 * Panorama hub with a parallax gradient "music" header, lowercase pivot titles,
 * circle buttons, an app bar, a giant drifting artist name behind the now-playing
 * page (Zune kinetic typography), turnstile list entry and continuum-style
 * title slides on track change. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "ui.h"

static const uint32_t k_accent[6] = {0x00C4A4, 0x7300D8, 0xE2A11B, 0x0068FA, 0xEF5000, 0x0014E5}; /* BGR */

static GTex *g_head;
static int g_head_w, g_head_h, g_head_px;
static float g_alpha = 1, g_ox;
static float g_pivot = 1; /* animated page position for the parallax header */

static uint32_t A(uint32_t c) { return col_alpha(c, g_alpha); }
static uint32_t BG(void) { return cfg.dark ? RGB(0, 0, 0) : RGB(255, 255, 255); }
static uint32_t FG(void) { return cfg.dark ? RGB(255, 255, 255) : RGB(0, 0, 0); }
static uint32_t SUB(void) { return cfg.dark ? RGB(160, 160, 160) : RGB(110, 110, 110); }
static uint32_t BAR(void) { return cfg.dark ? RGB(31, 31, 31) : RGB(221, 221, 221); }
static uint32_t ACC(void) { uint32_t b = k_accent[cfg.accent % 6]; return RGB(b & 0xFF, (b >> 8) & 0xFF, b >> 16); }
static uint32_t TRACK(void) { return cfg.dark ? RGB(70, 70, 70) : RGB(190, 190, 190); }

static void make_header(void) {
    int px = ui.land ? 92 : 136;
    if (g_head && px == g_head_px) return;
    if (g_head) gfx_tex_free(g_head);
    g_head = font_render_gradient(FONT_LIGHT, px, "music", RGB(248, 148, 32), RGB(232, 46, 120), RGB(150, 22, 100), &g_head_w, &g_head_h);
    g_head_px = px;
}

static void m_init(UI *u) { make_header(); }
static void m_reset(UI *u) { make_header(); }

static void circle_btn(float cx, float cy, float r, int act, int filled) {
    uint32_t fg = A(FG());
    if (filled) gfx_circle(cx, cy, r, fg);
    else gfx_arc(cx, cy, r - 1.5f, 3.2f, 0, 6.2832f, fg);
    ui_hit(cx - r - 6, cy - r - 6, 2 * r + 12, 2 * r + 12, act, 0);
}

static void transport(float cx, float cy, float r, float gap, int with_modes) {
    uint32_t fg = A(FG());
    circle_btn(cx - gap, cy, r, ACT_PREV, 0);
    icon_prev(cx - gap, cy, r * 0.42f, fg);
    circle_btn(cx, cy, r, ACT_PLAYPAUSE, 0);
    if (ui_playing()) icon_pause(cx, cy, r * 0.42f, fg);
    else icon_play(cx + r * 0.06f, cy, r * 0.46f, fg);
    circle_btn(cx + gap, cy, r, ACT_NEXT, 0);
    icon_next(cx + gap, cy, r * 0.42f, fg);
    if (with_modes) {
        float g2 = gap * 1.9f;
        uint32_t on = A(FG()), off = A(SUB());
        icon_shuffle(cx - g2, cy, r * 0.5f, 3, ui.ps.shuffle ? on : col_alpha(off, 0.7f));
        if (ui.ps.shuffle) gfx_rect(cx - g2 - 3, cy + r * 0.62f, 6, 3, A(ACC()));
        ui_hit(cx - g2 - r, cy - r, 2 * r, 2 * r, ACT_SHUFFLE, 0);
        icon_repeat(cx + g2, cy, r * 0.5f, 3, ui.ps.repeat == REPEAT_ONE, ui.ps.repeat ? on : col_alpha(off, 0.7f));
        if (ui.ps.repeat) gfx_rect(cx + g2 - 3, cy + r * 0.62f, 6, 3, A(ACC()));
        ui_hit(cx + g2 - r, cy - r, 2 * r, 2 * r, ACT_REPEAT, 0);
    }
}

static void seekbar(float x, float y, float w, int show_times, float tpx) {
    char a[16], b[24];
    double pos = ui_pos();
    fmt_time(pos, a);
    b[0] = '-';
    fmt_time(ui.ps.dur - pos, b + 1);
    float bx = x, bw = w;
    if (show_times) {
        font_draw(FONT_REGULAR, tpx, x, y - tpx * 0.72f, A(FG()), a);
        font_draw_right(FONT_REGULAR, tpx, x + w, y - tpx * 0.72f, A(FG()), b);
        bx = x + tpx * 3.4f;
        bw = w - tpx * 6.8f;
    }
    float p = ui_progress();
    gfx_rect(bx, y, bw, 4, A(TRACK()));
    gfx_rect(bx, y, bw * p, 4, A(FG()));
    if (ui.seeking) gfx_rect(bx + bw * p - 2, y - 10, 5, 24, A(ACC()));
    ui_seek_area(bx, y, bw, 4);
}

static void cover(float x, float y, float s) {
    if (ui.cover) {
        gfx_tex_draw(ui.cover, 0, 0, gfx_tex_w(ui.cover), gfx_tex_h(ui.cover), x, y, s, s, A(RGB(255, 255, 255)));
    } else {
        gfx_rect(x, y, s, s, A(ACC()));
        icon_note(x + s / 2, y + s / 2, s * 0.22f, A(RGB(255, 255, 255)));
    }
}

/* text slides in from the right on track change, each line a little later (continuum) */
static float slide(int line) {
    float t = (float)(ui.t - ui.track_t) * 3.2f - line * 0.18f;
    return (1 - ui_ease(t)) * 120;
}

static void appbar(void) {
    float h = ui.land ? 54 : 62, y = ui.H - h;
    gfx_rect(0, y, ui.W, h, BAR());
    uint32_t fg = FG();
    float cy = y + h / 2, cx = ui.W / 2.0f, r = 21;
    float xs[3] = {cx - 72, cx, cx + 72};
    for (int i = 0; i < 3; i++) gfx_arc(xs[i], cy, r - 1, 2.4f, 0, 6.2832f, fg);
    icon_bulb(xs[0], cy, 11, 2, cfg.always_on, cfg.always_on ? ACC() : fg);
    ui_hit(xs[0] - 30, y, 60, h, ACT_ALWAYSON, 0);
    icon_moon(xs[1], cy, 15, fg, BAR());
    ui_hit(xs[1] - 30, y, 60, h, ACT_SCREENOFF, 0);
    icon_rotate(xs[2], cy, 11, 2, fg);
    ui_hit(xs[2] - 30, y, 60, h, ACT_ROTATE, 0);
    icon_dots(ui.W - 40, y + 16, 9, fg);
    ui_hit(ui.W - 80, y, 80, h, ACT_MORE, 0);
}

static const char *k_titles[PAGE_COUNT] = {"live", "collection", "settings"};

static float pivot_title(int page) {
    float px = ui.land ? 50 : 66, y = ui.land ? 76 : 110;
    float x = 24 + g_ox;
    float w = font_draw(FONT_LIGHT, px, x, y, A(FG()), k_titles[page]);
    /* the next pivot peeks in, dimmed, like a real pivot control */
    font_draw(FONT_LIGHT, px, x + w + 36, y, A(col_alpha(SUB(), 0.45f)), k_titles[(page + 1) % PAGE_COUNT]);
    ui_hit(x + w + 20, y, 300, px * 1.2f, ACT_PAGE, (page + 1) % PAGE_COUNT);
    return y + px * 1.36f;
}

/* ---------------- now playing ---------------- */
static void page_playing(void) {
    float top = pivot_title(PAGE_PLAYING);
    const char *artist = ui_artist();

    /* Zune kinetic typography: the artist name drifts slowly behind everything */
    {
        float px = ui.land ? 300 : 260;
        float tw = font_width(FONT_LIGHT, px, artist);
        float speed = 22;
        static double drift = -1;
        if (drift < 0) drift = ui.W * 0.55;
        if (ui_playing()) drift += ui.dt * speed;
        float x = ui.W - fmodf((float)drift, tw + ui.W);
        float y = ui.land ? 150 : 420;
        font_draw(FONT_LIGHT, px, x + g_ox * 0.5f, y, A(col_alpha(FG(), cfg.dark ? 0.07f : 0.06f)), artist);
    }

    char nx[300];
    snprintf(nx, sizeof nx, "Up next: %s", ui.ps.next_title[0] ? ui.ps.next_title : "-");
    if (ui.land) {
        float s = 274, x = 28 + g_ox, y = top + 4;
        cover(x, y, s);
        float tx = x + s + 30, tw = ui.W - tx - 28;
        font_draw_fit(FONT_REGULAR, 36, tx + slide(0), y - 6, tw, A(FG()), ui_title());
        font_draw_fit(FONT_REGULAR, 28, tx + slide(1), y + 42, tw, A(ACC()), artist);
        font_draw_fit(FONT_LIGHT, 22, tx + slide(2), y + 82, tw, A(SUB()), ui.ps.album);
        font_draw_fit_ex(FONT_REGULAR, 19, tx + slide(3), y + 114, tw, A(SUB()), nx, 0.8f);
        seekbar(tx, y + 186, tw, 1, 20);
        transport(tx + tw / 2, y + 248, 32, 84, 1);
    } else {
        float s = 400, x = 28 + g_ox, y = top + 2;
        cover(x, y, s);
        float ty = y + s + 16, tw = ui.W - 56;
        font_draw_fit(FONT_REGULAR, 34, x + slide(0), ty, tw, A(FG()), ui_title());
        font_draw_fit(FONT_REGULAR, 28, x + slide(1), ty + 46, tw, A(ACC()), artist);
        font_draw_fit_ex(FONT_REGULAR, 20, x + slide(2), ty + 88, tw, A(SUB()), nx, 0.8f);
        seekbar(x, ty + 150, tw, 1, 21);
        transport(ui.W / 2.0f + g_ox, ty + 218, 34, 100, 1);
    }
}

/* ---------------- collection ---------------- */
static void mini_player(float y, float h) {
    float x = 28 + g_ox, w = ui.W - 56;
    gfx_rect(x, y, w, 1.5f, A(TRACK()));
    char a[16], b[24];
    double pos = ui_pos();
    fmt_time(pos, a);
    b[0] = '-';
    fmt_time(ui.ps.dur - pos, b + 1);
    if (ui.land) {
        /* single row: art | title | time bar | transport */
        float as = h - 22, ay = y + 12, r = 22;
        cover(x, ay, as);
        float tx = x + as + 14, tw = 250;
        font_draw_fit(FONT_REGULAR, 21, tx, ay + 2, tw, A(FG()), ui_title());
        font_draw_fit(FONT_REGULAR, 17, tx, ay + 30, tw, A(SUB()), ui_artist());
        ui_hit(x, ay, as + tw, as, ACT_PAGE, PAGE_PLAYING);
        float bx0 = tx + tw + 16, bx1 = x + w - r * 7.4f;
        float by = ay + as / 2;
        font_draw(FONT_REGULAR, 16, bx0, by - 22, A(FG()), a);
        font_draw_right(FONT_REGULAR, 16, bx1, by - 22, A(FG()), b);
        gfx_rect(bx0, by + 2, bx1 - bx0, 3, A(TRACK()));
        gfx_rect(bx0, by + 2, (bx1 - bx0) * ui_progress(), 3, A(FG()));
        ui_seek_area(bx0, by + 2, bx1 - bx0, 3);
        transport(x + w - r * 3.4f, by, r, r * 2.5f, 0);
        return;
    }
    float by = y + 34;
    font_draw(FONT_REGULAR, 18, x, by - 13, A(FG()), a);
    font_draw_right(FONT_REGULAR, 18, x + w, by - 13, A(FG()), b);
    gfx_rect(x + 70, by, w - 140, 3, A(TRACK()));
    gfx_rect(x + 70, by, (w - 140) * ui_progress(), 3, A(FG()));
    ui_seek_area(x + 70, by, w - 140, 3);
    float ay = by + 18, as = h - 60;
    cover(x, ay, as);
    float r = 27;
    float bx = x + w - r * 5.4f;
    font_draw_fit(FONT_REGULAR, 22, x + as + 14, ay + as * 0.5f - 28, bx - x - as - 40 - r, A(FG()), ui_title());
    font_draw_fit(FONT_REGULAR, 19, x + as + 14, ay + as * 0.5f + 2, bx - x - as - 40 - r, A(SUB()), ui_artist());
    ui_hit(x, ay, as + 200, as, ACT_PAGE, PAGE_PLAYING);
    transport(bx + r * 2.7f, ay + as / 2, r, r * 2.6f, 0);
}

static void page_library(void) {
    float top = pivot_title(PAGE_LIBRARY);
    float bar = ui.land ? 54 : 62;
    float mini = ui.land ? 80 : 150;
    float x = 28 + g_ox;
    /* breadcrumb / up */
    char path[540];
    if (ui.br.path[0]) snprintf(path, sizeof path, "<  %s", ui.br.path);
    else snprintf(path, sizeof path, "devices");
    font_draw_fit(FONT_REGULAR, 19, x, top - 10, ui.W - 56, A(ui.br.path[0] ? ACC() : SUB()), path);
    if (ui.br.path[0]) ui_hit(0, top - 16, ui.W, 34, ACT_UP, 0);
    float ly = top + 26, lh = ui.H - bar - mini - ly - 6;
    float rh = ui.land ? 52 : 70;
    ui_list_area(0, ly, ui.W, lh, rh);
    gfx_clip(0, ly, ui.W, lh);
    int first = (int)(ui.scroll / rh);
    if (first < 0) first = 0;
    for (int i = first; i < ui.br.n; i++) {
        float y = ly + i * rh - ui.scroll;
        if (y > ly + lh) break;
        /* turnstile-ish entry: rows fly in from the right one after another */
        float e = ui_ease((float)(ui.t - ui.list_t) * 3.5f - (i - first) * 0.12f);
        float ox = (1 - e) * 140;
        float sa = g_alpha;
        g_alpha *= e;
        Entry *en = &ui.br.e[i];
        int sel = i == ui.sel;
        uint32_t c = sel ? A(ACC()) : A(FG());
        if (!ui.br.path[0]) {
            font_draw(FONT_LIGHT, ui.land ? 34 : 46, x + ox, y - 4, c, en->name);
            font_draw(FONT_REGULAR, 17, x + ox + 4, y + (ui.land ? 40 : 50), A(SUB()), browser_device_label(en->name));
        } else if (en->is_dir) {
            font_draw_fit(FONT_LIGHT, ui.land ? 34 : 44, x + ox, y + 2, ui.W - 60, c, en->name);
        } else {
            char nm[256];
            snprintf(nm, sizeof nm, "%s", en->name);
            char *dot = strrchr(nm, '.');
            const char *ext = dot ? dot + 1 : "";
            if (dot) *dot = 0;
            font_draw_fit(FONT_LIGHT, ui.land ? 26 : 32, x + ox, y + 2, ui.W - 60, c, nm);
            char sub[32];
            int k = 0;
            for (; ext[k] && k < 8; k++) sub[k] = ext[k] >= 'A' && ext[k] <= 'Z' ? ext[k] + 32 : ext[k];
            sub[k] = 0;
            font_draw(FONT_REGULAR, 15, x + ox + 2, y + (ui.land ? 32 : 42), A(SUB()), sub);
        }
        g_alpha = sa;
    }
    gfx_noclip();
    if (!ui.br.n) font_draw(FONT_LIGHT, 28, x, ly + 10, A(SUB()), "no music here");
    mini_player(ui.H - bar - mini, mini);
}

/* ---------------- settings ---------------- */
static void wp_switch(float x, float y, int on) {
    float w = 72, h = 30;
    gfx_frame(x, y, w, h, 2.5f, A(FG()));
    gfx_rect(x + 5, y + 5, on ? w - 32 : 0, h - 10, A(ACC()));
    gfx_rect(x + (on ? w - 22 : 0), y - 2, 22, h + 4, A(FG()));
}

static void page_settings(void) {
    float top = pivot_title(PAGE_SETTINGS);
    float bar = ui.land ? 54 : 62;
    float ly = top, lh = ui.H - bar - ly - 4;
    float rh = ui.land ? 64 : 84;
    float x = 28 + g_ox;
    ui_list_area(0, ly, ui.W, lh, rh);
    gfx_clip(0, ly, ui.W, lh);
    for (int i = 0; i < NSET; i++) {
        float y = ly + i * rh - ui.set_scroll;
        if (y + rh < ly || y > ly + lh) continue;
        char v[96];
        set_value(i, v, sizeof v);
        int sel = i == ui.set_sel;
        font_draw(FONT_LIGHT, ui.land ? 28 : 32, x, y, A(sel ? ACC() : FG()), set_label(i));
        if (set_is_toggle(i)) {
            font_draw(FONT_REGULAR, 18, x, y + (ui.land ? 36 : 42), A(SUB()), set_toggle_on(i) ? "on" : "off");
            wp_switch(ui.W - 110 + g_ox, y + 10, set_toggle_on(i));
        } else {
            font_draw_fit(FONT_REGULAR, 18, x, y + (ui.land ? 36 : 42), ui.W - 60, A(SUB()), v);
        }
    }
    gfx_noclip();
}

static void draw_page(int p) {
    if (p == PAGE_PLAYING) page_playing();
    else if (p == PAGE_LIBRARY) page_library();
    else page_settings();
}

static void m_draw(UI *u) {
    make_header();
    gfx_rect(0, 0, ui.W, ui.H, BG());
    float e = ui_ease(ui.page_t);
    float target = ui.page;
    float from = ui.prev_page;
    if (ui.page_dir > 0 && target < from) target += PAGE_COUNT;
    if (ui.page_dir < 0 && target > from) target -= PAGE_COUNT;
    g_pivot = from + (target - from) * e;

    /* panorama header parallax (wraps around) */
    float step = ui.land ? 64 : 90;
    float hx = 20 - fmodf(g_pivot + PAGE_COUNT * 4, (float)PAGE_COUNT) * step;
    g_alpha = 1;
    g_ox = 0;
    float hy = ui.land ? -24 : -34;
    gfx_tex_draw(g_head, 0, 0, g_head_w, g_head_h, hx, hy, g_head_w, g_head_h, RGB(255, 255, 255));

    float slide_w = ui.W * 0.45f;
    if (ui.page_t < 1) {
        ui.hits_on = 0;
        g_alpha = 1 - e;
        g_ox = -ui.page_dir * slide_w * e;
        draw_page(ui.prev_page);
        ui.hits_on = 1;
    }
    g_alpha = e;
    g_ox = ui.page_dir * slide_w * (1 - e);
    draw_page(ui.page);
    g_alpha = 1;
    g_ox = 0;
    appbar();

    /* WP-style toast across the top */
    float tt = (float)(ui.t - ui.toast_t);
    if (ui.toast[0] && tt < 2.0f) {
        float k = tt < 0.2f ? tt / 0.2f : tt > 1.7f ? (2.0f - tt) / 0.3f : 1;
        float h = 44;
        gfx_rect(0, -h + h * ui_ease(k), ui.W, h, ACC());
        font_draw(FONT_REGULAR, 21, 24, -h + h * ui_ease(k) + 8, RGB(255, 255, 255), ui.toast);
    }
}

const Skin skin_metro = {"metro", m_init, m_draw, m_reset};
