/* FreeType glyph cache in one 1024x1024 A8 atlas, keyed by (font, size, codepoint).
 * Unknown glyphs fall back to the Regular face (e.g. Cyrillic in the mono font). */
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS 1024
#define HSIZE 4096
#define MAXRASTER 160

typedef struct { uint32_t key; int16_t x, y, w, h, l, t; float adv; } Glyph;

static FT_Library g_ft;
static FT_Face g_face[FONT_COUNT];
static int g_cur_size[FONT_COUNT];
static GTex *g_atlas;
static Glyph g_tab[HSIZE];
static int g_used;
static int g_px, g_py, g_rowh;

static const char *g_files[FONT_COUNT] = {"OpenSans-Light.ttf", "OpenSans-Regular.ttf", "ShareTechMono.ttf"};

int utf8_next(const char **ps) {
    const unsigned char *s = (const unsigned char *)*ps;
    int c = *s;
    if (!c) return 0;
    int n = 1;
    if (c >= 0xF0 && s[1] && s[2] && s[3]) { c = ((c & 7) << 18) | ((s[1] & 63) << 12) | ((s[2] & 63) << 6) | (s[3] & 63); n = 4; }
    else if (c >= 0xE0 && s[1] && s[2]) { c = ((c & 15) << 12) | ((s[1] & 63) << 6) | (s[2] & 63); n = 3; }
    else if (c >= 0xC0 && s[1]) { c = ((c & 31) << 6) | (s[1] & 63); n = 2; }
    else if (c >= 0x80) c = '?';
    *ps += n;
    return c;
}

int font_init(const char *dir) {
    if (FT_Init_FreeType(&g_ft)) return -1;
    char path[256];
    for (int i = 0; i < FONT_COUNT; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, g_files[i]);
        if (FT_New_Face(g_ft, path, 0, &g_face[i])) return -1;
    }
    g_atlas = gfx_tex_create(ATLAS, ATLAS, 0);
    return g_atlas ? 0 : -1;
}

void font_fini(void) {
    for (int i = 0; i < FONT_COUNT; i++) if (g_face[i]) FT_Done_Face(g_face[i]);
    FT_Done_FreeType(g_ft);
    gfx_tex_free(g_atlas);
}

static void set_size(int f, int size) {
    if (g_cur_size[f] != size) { FT_Set_Pixel_Sizes(g_face[f], 0, size); g_cur_size[f] = size; }
}

static void atlas_reset(void) {
    gfx_wait_idle();
    memset(g_tab, 0, sizeof g_tab);
    memset(gfx_tex_data(g_atlas), 0, gfx_tex_stride(g_atlas) * ATLAS);
    g_used = 0;
    g_px = g_py = g_rowh = 0;
}

static Glyph *get_glyph(int f, int size, int cp) {
    uint32_t key = ((uint32_t)(f + 1) << 28) | ((uint32_t)size << 20) | (cp & 0xFFFFF);
    uint32_t h = (key * 2654435761u) >> 20;
    for (int probe = 0; probe < HSIZE; probe++) {
        Glyph *g = &g_tab[(h + probe) & (HSIZE - 1)];
        if (g->key == key) return g;
        if (g->key == 0) break;
    }
    if (g_used > HSIZE * 3 / 4) atlas_reset();

    int ff = f;
    if (FT_Get_Char_Index(g_face[ff], cp) == 0 && ff != FONT_REGULAR) ff = FONT_REGULAR;
    set_size(ff, size);
    if (FT_Load_Char(g_face[ff], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return NULL;
    FT_GlyphSlot gs = g_face[ff]->glyph;
    int w = gs->bitmap.width, hh = gs->bitmap.rows;
    if (g_px + w + 1 > ATLAS) { g_px = 0; g_py += g_rowh + 1; g_rowh = 0; }
    if (g_py + hh + 1 > ATLAS) {
        atlas_reset();
        return get_glyph(f, size, cp);
    }
    uint8_t *d = gfx_tex_data(g_atlas);
    int st = gfx_tex_stride(g_atlas);
    for (int y = 0; y < hh; y++) memcpy(d + (g_py + y) * st + g_px, gs->bitmap.buffer + y * gs->bitmap.pitch, w);
    gfx_tex_dirty(g_atlas);

    Glyph *g = NULL;
    for (int probe = 0; probe < HSIZE; probe++) {
        g = &g_tab[(h + probe) & (HSIZE - 1)];
        if (g->key == 0) break;
    }
    g->key = key;
    g->x = g_px; g->y = g_py; g->w = w; g->h = hh;
    g->l = gs->bitmap_left; g->t = gs->bitmap_top;
    g->adv = gs->advance.x / 64.0f;
    g_px += w + 1;
    if (hh > g_rowh) g_rowh = hh;
    g_used++;
    return g;
}

static void raster_for(float px, int *size, float *scale) {
    int s = (int)(px + 0.5f);
    float k = 1.0f;
    if (s > MAXRASTER) { k = px / MAXRASTER; s = MAXRASTER; }
    if (s < 6) s = 6;
    *size = s;
    *scale = k;
}

float font_ascent(int f, float px) {
    int size; float k;
    raster_for(px, &size, &k);
    set_size(f, size);
    return g_face[f]->size->metrics.ascender / 64.0f * k;
}

static float run(int f, float px, float x, float y, uint32_t c, const char *s, float tracking, int draw, float maxw, int *cut) {
    int size; float k;
    raster_for(px, &size, &k);
    float base = y + font_ascent(f, px);
    float pen = x;
    const char *p = s;
    int cp;
    while ((cp = utf8_next(&p))) {
        Glyph *g = get_glyph(f, size, cp);
        if (!g) continue;
        float adv = g->adv * k + tracking;
        if (maxw > 0 && pen + adv - x > maxw) { if (cut) *cut = (int)(p - s) - 1; break; }
        if (draw && g->w > 0)
            gfx_tex_draw(g_atlas, g->x, g->y, g->w, g->h, roundf(pen + g->l * k), roundf(base - g->t * k),
                         g->w * k, g->h * k, c);
        pen += adv;
    }
    return pen - x;
}

float font_draw_ex(int f, float px, float x, float y, uint32_t c, const char *s, float tr) { return run(f, px, x, y, c, s, tr, 1, 0, NULL); }
float font_draw(int f, float px, float x, float y, uint32_t c, const char *s) { return run(f, px, x, y, c, s, 0, 1, 0, NULL); }
float font_width_ex(int f, float px, const char *s, float tr) { return run(f, px, 0, 0, 0, s, tr, 0, 0, NULL); }
float font_width(int f, float px, const char *s) { return run(f, px, 0, 0, 0, s, 0, 0, 0, NULL); }
float font_draw_right(int f, float px, float xr, float y, uint32_t c, const char *s) { float w = font_width(f, px, s); font_draw(f, px, xr - w, y, c, s); return w; }
float font_draw_center(int f, float px, float xc, float y, uint32_t c, const char *s) { float w = font_width(f, px, s); font_draw(f, px, xc - w / 2, y, c, s); return w; }

float font_draw_fit_ex(int f, float px, float x, float y, float maxw, uint32_t c, const char *s, float tr) {
    float w = font_width_ex(f, px, s, tr);
    if (w <= maxw) return font_draw_ex(f, px, x, y, c, s, tr);
    float ell = font_width_ex(f, px, "...", tr);
    char buf[512];
    size_t n = strlen(s);
    if (n >= sizeof buf) n = sizeof buf - 1;
    /* find the longest UTF-8 prefix that fits */
    const char *p = s, *last = s;
    float pen = 0;
    int size; float k;
    raster_for(px, &size, &k);
    int cp;
    while ((cp = utf8_next(&p))) {
        Glyph *g = get_glyph(f, size, cp);
        float adv = g ? g->adv * k + tr : 0;
        if (pen + adv + ell > maxw) break;
        pen += adv;
        last = p;
    }
    n = (size_t)(last - s);
    if (n > sizeof buf - 4) n = sizeof buf - 4;
    memcpy(buf, s, n);
    while (n > 0 && buf[n - 1] == ' ') n--;
    strcpy(buf + n, "...");
    return font_draw_ex(f, px, x, y, c, buf, tr);
}
float font_draw_fit(int f, float px, float x, float y, float maxw, uint32_t c, const char *s) { return font_draw_fit_ex(f, px, x, y, maxw, c, s, 0); }

GTex *font_render_gradient(int f, int px, const char *s, uint32_t c0, uint32_t cm, uint32_t c1, int *ow, int *oh) {
    set_size(f, px);
    FT_Face fc = g_face[f];
    int asc = (int)ceilf(fc->size->metrics.ascender / 64.0f);
    int desc = (int)ceilf(-fc->size->metrics.descender / 64.0f);
    int w = (int)ceilf(font_width(f, (float)px, s)) + 4, h = asc + desc;
    GTex *t = gfx_tex_create(w, h, 1);
    if (!t) return NULL;
    uint8_t *d = gfx_tex_data(t);
    int st = gfx_tex_stride(t);
    memset(d, 0, st * h);
    float pen = 0;
    const char *p = s;
    int cp;
    while ((cp = utf8_next(&p))) {
        set_size(f, px);
        if (FT_Load_Char(fc, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) continue;
        FT_GlyphSlot gs = fc->glyph;
        int gx = (int)roundf(pen) + gs->bitmap_left + 1, gy = asc - gs->bitmap_top;
        for (int y = 0; y < (int)gs->bitmap.rows; y++)
            for (int x = 0; x < (int)gs->bitmap.width; x++) {
                int X = gx + x, Y = gy + y;
                if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
                uint8_t a = gs->bitmap.buffer[y * gs->bitmap.pitch + x];
                if (!a) continue;
                float u = (float)X / (w - 1);
                uint32_t c = u < 0.5f ? col_mix(c0, cm, u * 2) : col_mix(cm, c1, u * 2 - 1);
                uint8_t *q = d + Y * st + X * 4;
                q[0] = c & 0xFF; q[1] = (c >> 8) & 0xFF; q[2] = (c >> 16) & 0xFF;
                if (a > q[3]) q[3] = a;
            }
        pen += gs->advance.x / 64.0f;
    }
    gfx_tex_dirty(t);
    *ow = w;
    *oh = h;
    g_cur_size[f] = 0;
    return t;
}
