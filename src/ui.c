/* Shared UI helpers: hit regions, formatting, settings rows and vector icons */
#include "ui.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "i18n.h"

UI ui;

void ui_hit(float x, float y, float w, float h, int act, int param) {
    if (!ui.hits_on || ui.nhits >= (int)(sizeof ui.hits / sizeof *ui.hits)) return;
    ui.hits[ui.nhits++] = (Hit){x, y, w, h, act, param};
}

void ui_list_area(float x, float y, float w, float h, float row_h) {
    if (!ui.hits_on) return;
    ui.list_x = x; ui.list_y = y; ui.list_w = w; ui.list_h = h; ui.row_h = row_h;
    ui_hit(x, y, w, h, ACT_LIST, 0);
}

void ui_seek_area(float x, float y, float w, float h) {
    if (!ui.hits_on) return;
    ui.seek_x = x; ui.seek_w = w; ui.seek_y = y;
    ui_hit(x - 16, y - 18, w + 32, h + 36, ACT_SEEKBAR, 0);
}

void fmt_time(double s, char *buf) {
    if (s < 0) s = 0;
    int t = (int)s;
    if (t >= 3600) sprintf(buf, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
    else sprintf(buf, "%d:%02d", t / 60, t % 60);
}

float ui_ease(float t) { t = ui_clamp(t, 0, 1); t = 1 - t; return 1 - t * t * t; }
float ui_clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

double ui_pos(void) {
    if (ui.seeking) return ui.seek_frac * ui.ps.dur;
    return ui.pos_smooth;
}

float ui_progress(void) {
    if (ui.seeking) return ui.seek_frac;
    return ui.ps.dur > 0 ? ui_clamp((float)(ui.pos_smooth / ui.ps.dur), 0, 1) : 0;
}

const char *ui_title(void) { return ui.ps.title[0] ? ui.ps.title : (ui.ps.qcount ? "" : tr(S_NOTHING_PLAYING)); }
const char *ui_artist(void) { return ui.ps.artist[0] ? ui.ps.artist : (ui.ps.qcount ? tr(S_UNKNOWN_ARTIST) : tr(S_PICK_TRACK)); }
int ui_playing(void) { return ui.ps.status == PS_PLAYING; }

/* ---------------- settings rows ---------------- */

static const int k_skin[SKIN_COUNT] = {S_SKIN_METRO, S_SKIN_RETRO, S_SKIN_MINIMAL};
static const int k_orient[3] = {S_ORIENT_LAND, S_ORIENT_PORT, S_ORIENT_FLIP};
static const int k_accent[6] = {S_ACC0, S_ACC1, S_ACC2, S_ACC3, S_ACC4, S_ACC5};
static const int k_labels[NSET] = {S_L_STYLE, S_L_THEME, S_L_ACCENT, S_L_ORIENT, S_L_SCREEN_OFF,
                                   S_L_ALWAYS_ON, S_L_PIXEL, S_L_FPS, S_L_LANGUAGE, S_L_ABOUT};

const char *set_label(int i) { return i >= 0 && i < NSET ? tr(k_labels[i]) : ""; }
int set_is_toggle(int i) { return i == 5 || i == 6; }
int set_toggle_on(int i) { return i == 5 ? cfg.always_on : i == 6 ? cfg.pixel_shift : 0; }

void set_value(int i, char *b, int m) {
    switch (i) {
    case 0: snprintf(b, m, "%s", tr(k_skin[cfg.skin])); break;
    case 1: snprintf(b, m, "%s", tr(cfg.dark ? S_DARK : S_LIGHT)); break;
    case 2: snprintf(b, m, "%s", tr(k_accent[cfg.accent % 6])); break;
    case 3: snprintf(b, m, "%s", tr(k_orient[cfg.orient])); break;
    case 4: if (cfg.screen_off) snprintf(b, m, tr(S_AFTER_SEC), cfg.screen_off); else snprintf(b, m, "%s", tr(S_NEVER)); break;
    case 5: snprintf(b, m, "%s", tr(cfg.always_on ? S_ALWAYS_ON_VAL : S_OFF)); break;
    case 6: snprintf(b, m, "%s", tr(cfg.pixel_shift ? S_ON : S_OFF)); break;
    case 7: snprintf(b, m, "%s", tr(cfg.fps60 ? S_FPS60 : S_FPS30)); break;
    case 8: snprintf(b, m, "%s", lang_name(cfg.lang)); break;
    default: snprintf(b, m, "%s", tr(S_ABOUT)); break;
    }
}

/* ---------------- icons ---------------- */

void icon_play(float cx, float cy, float s, uint32_t c) {
    gfx_tri(cx - s * 0.55f, cy - s * 0.75f, cx - s * 0.55f, cy + s * 0.75f, cx + s * 0.8f, cy, c);
}

void icon_pause(float cx, float cy, float s, uint32_t c) {
    gfx_rect(cx - s * 0.6f, cy - s * 0.7f, s * 0.4f, s * 1.4f, c);
    gfx_rect(cx + s * 0.2f, cy - s * 0.7f, s * 0.4f, s * 1.4f, c);
}

void icon_stop(float cx, float cy, float s, uint32_t c) { gfx_rect(cx - s * 0.6f, cy - s * 0.6f, s * 1.2f, s * 1.2f, c); }

void icon_next(float cx, float cy, float s, uint32_t c) {
    gfx_tri(cx - s * 0.8f, cy - s * 0.6f, cx - s * 0.8f, cy + s * 0.6f, cx, cy, c);
    gfx_tri(cx - s * 0.1f, cy - s * 0.6f, cx - s * 0.1f, cy + s * 0.6f, cx + s * 0.7f, cy, c);
    gfx_rect(cx + s * 0.62f, cy - s * 0.6f, s * 0.2f, s * 1.2f, c);
}

void icon_prev(float cx, float cy, float s, uint32_t c) {
    gfx_tri(cx + s * 0.8f, cy - s * 0.6f, cx + s * 0.8f, cy + s * 0.6f, cx, cy, c);
    gfx_tri(cx + s * 0.1f, cy - s * 0.6f, cx + s * 0.1f, cy + s * 0.6f, cx - s * 0.7f, cy, c);
    gfx_rect(cx - s * 0.82f, cy - s * 0.6f, s * 0.2f, s * 1.2f, c);
}

static void arrow_head(float x, float y, float dx, float dy, float s, uint32_t c) {
    float l = sqrtf(dx * dx + dy * dy);
    dx /= l; dy /= l;
    gfx_tri(x + dx * s, y + dy * s, x - dy * s * 0.7f, y + dx * s * 0.7f, x + dy * s * 0.7f, y - dx * s * 0.7f, c);
}

void icon_shuffle(float cx, float cy, float s, float th, uint32_t c) {
    float x0 = cx - s, x1 = cx + s * 0.7f, y0 = cy - s * 0.55f, y1 = cy + s * 0.55f;
    gfx_line(x0, y1, cx - s * 0.3f, y1, th, c);
    gfx_line(cx - s * 0.3f, y1, cx + s * 0.3f, y0, th, c);
    gfx_line(cx + s * 0.3f, y0, x1, y0, th, c);
    gfx_line(x0, y0, cx - s * 0.3f, y0, th, c);
    gfx_line(cx - s * 0.3f, y0, cx + s * 0.3f, y1, th, c);
    gfx_line(cx + s * 0.3f, y1, x1, y1, th, c);
    arrow_head(x1, y0, 1, 0, s * 0.35f, c);
    arrow_head(x1, y1, 1, 0, s * 0.35f, c);
}

void icon_repeat(float cx, float cy, float s, float th, int one, uint32_t c) {
    gfx_arc(cx, cy, s * 0.75f, th, 0.5f, 6.0f, c);
    float a = 6.0f;
    float ex = cx + cosf(a) * s * 0.75f, ey = cy + sinf(a) * s * 0.75f;
    arrow_head(ex, ey, -sinf(a), cosf(a), s * 0.38f, c);
    if (one) font_draw_center(FONT_REGULAR, s * 0.9f, cx, cy - s * 0.62f, c, "1");
}

void icon_bulb(float cx, float cy, float s, float th, int on, uint32_t c) {
    gfx_arc(cx, cy - s * 0.2f, s * 0.5f, th, 2.5f, 6.92f, c);
    gfx_line(cx - s * 0.32f, cy + s * 0.18f, cx - s * 0.22f, cy + s * 0.5f, th, c);
    gfx_line(cx + s * 0.32f, cy + s * 0.18f, cx + s * 0.22f, cy + s * 0.5f, th, c);
    gfx_line(cx - s * 0.24f, cy + s * 0.62f, cx + s * 0.24f, cy + s * 0.62f, th, c);
    gfx_line(cx - s * 0.16f, cy + s * 0.8f, cx + s * 0.16f, cy + s * 0.8f, th, c);
    if (on) {
        gfx_circle(cx, cy - s * 0.2f, s * 0.3f, c);
        for (int i = 0; i < 5; i++) {
            float a = 3.1416f + i * 0.785f;
            gfx_line(cx + cosf(a) * s * 0.78f, cy - s * 0.2f + sinf(a) * s * 0.78f,
                     cx + cosf(a) * s * 0.98f, cy - s * 0.2f + sinf(a) * s * 0.98f, th, c);
        }
    }
}

void icon_moon(float cx, float cy, float s, uint32_t c, uint32_t bg) {
    gfx_circle(cx, cy, s * 0.6f, c);
    gfx_circle(cx + s * 0.3f, cy - s * 0.25f, s * 0.5f, bg);
}

void icon_rotate(float cx, float cy, float s, float th, uint32_t c) {
    gfx_frame(cx - s * 0.35f, cy - s * 0.6f, s * 0.7f, s * 1.2f, th, c);
    gfx_arc(cx, cy, s * 0.95f, th, -2.6f, -0.9f, c);
    arrow_head(cx + cosf(-0.9f) * s * 0.95f, cy + sinf(-0.9f) * s * 0.95f, 0.78f, 0.62f, s * 0.3f, c);
}

void icon_folder(float cx, float cy, float s, uint32_t c) {
    gfx_rect(cx - s, cy - s * 0.55f, s * 0.8f, s * 0.3f, c);
    gfx_rect(cx - s, cy - s * 0.35f, s * 2, s * 1.1f, c);
}

void icon_note(float cx, float cy, float s, uint32_t c) {
    gfx_circle(cx - s * 0.35f, cy + s * 0.55f, s * 0.32f, c);
    gfx_rect(cx - s * 0.08f, cy - s * 0.8f, s * 0.14f, s * 1.38f, c);
    GVert v[6] = {{cx - s * 0.08f, cy - s * 0.8f, c}, {cx + s * 0.6f, cy - s * 0.45f, c}, {cx - s * 0.08f, cy - s * 0.5f, c},
                  {cx + s * 0.6f, cy - s * 0.45f, c}, {cx + s * 0.6f, cy - s * 0.2f, c}, {cx - s * 0.08f, cy - s * 0.5f, c}};
    gfx_tris(v, 6);
}

void icon_dots(float cx, float cy, float s, uint32_t c) {
    for (int i = -1; i <= 1; i++) gfx_circle(cx + i * s, cy, s * 0.22f, c);
}

void icon_gear(float cx, float cy, float s, float th, uint32_t c, uint32_t bg) {
    for (int i = 0; i < 8; i++) {
        float a = i * 0.785f;
        gfx_line(cx + cosf(a) * s * 0.5f, cy + sinf(a) * s * 0.5f, cx + cosf(a) * s * 0.95f, cy + sinf(a) * s * 0.95f, s * 0.28f, c);
    }
    gfx_circle(cx, cy, s * 0.68f, c);
    gfx_circle(cx, cy, s * 0.3f, bg);
}

void icon_battery(float x, float y, float w, float h, int pct, uint32_t c) {
    gfx_frame(x, y, w, h, 1.5f, c);
    gfx_rect(x + w, y + h * 0.3f, 2.5f, h * 0.4f, c);
    gfx_rect(x + 3, y + 3, (w - 6) * ui_clamp(pct / 100.0f, 0, 1), h - 6, c);
}
