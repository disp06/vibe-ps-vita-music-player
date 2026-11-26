/* FreeType glyph cache in one 1024x1024 A8 atlas, keyed by (font, size, codepoint).
 * A glyph missing from the skin face falls back to Regular, then to the script
 * face that covers it (CJK, Devanagari, Arabic). Text goes through shape.c first. */
#include "font.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "shape.h"

#define ATLAS 1024
#define HSIZE 4096
#define MAXRASTER 160
#define MAXTEXT 512

typedef struct { uint32_t key; int16_t x, y, w, h, l, t; float adv; } Glyph;

static FT_Library g_ft;
static FT_Face g_face[FONT_COUNT];
static int g_cur_size[FONT_COUNT];
static GTex *g_atlas;
static Glyph g_tab[HSIZE];
static int g_used;
static int g_px, g_py, g_rowh;

static const char *g_files[FONT_COUNT] = {"OpenSans-Light.ttf", "OpenSans-Regular.ttf", "ShareTechMono.ttf",
                                          "NotoSansSC.ttf", "NotoSansDevanagari.ttf", "NotoSansArabic.ttf"};

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
        if (FT_New_Face(g_ft, path, 0, &g_face[i]) && i <= FONT_REGULAR) return -1;
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

void font_frame_begin(void) {
    if (g_atlas && (g_used > HSIZE * 3 / 4 || g_py > ATLAS * 3 / 4)) atlas_reset();
}

static int script_face(uint32_t cp) {
    if ((cp >= 0x0900 && cp <= 0x097F) || cp == 0x200C || cp == 0x200D) return FONT_DEVA;
    if ((cp >= 0x0600 && cp <= 0x06FF) || (cp >= 0x0750 && cp <= 0x077F) ||
        (cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) return FONT_ARAB;
    if ((cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFF00 && cp <= 0xFFEF)) return FONT_CJK;
    return -1;
}

/* the face that will draw cp, or -1 if none has it */
static int resolve(int f, int cp) {
    if (g_face[f] && FT_Get_Char_Index(g_face[f], cp)) return f;
    if (FT_Get_Char_Index(g_face[FONT_REGULAR], cp)) return FONT_REGULAR;
    int s = script_face((uint32_t)cp);
    if (s >= 0 && g_face[s] && FT_Get_Char_Index(g_face[s], cp)) return s;
    return -1;
}

static Glyph *get_glyph(int f, int size, int cp) {
    uint32_t key = ((uint32_t)(f + 1) << 28) | ((uint32_t)size << 20) | (cp & 0xFFFFF);
    uint32_t h = (key * 2654435761u) >> 20;
    for (int probe = 0; probe < HSIZE; probe++) {
        Glyph *g = &g_tab[(h + probe) & (HSIZE - 1)];
        if (g->key == key) return g;
        if (g->key == 0) break;
    }
    /* full caches are cleared by font_frame_begin, never in the middle of a frame:
     * glyphs already queued for this frame still point into the atlas */
    if (g_used > HSIZE * 3 / 4) return NULL;

    int ff = resolve(f, cp);
    int w = 0, hh = 0, l = 0, t = 0;
    float adv = 0;
    unsigned char *src = NULL;
    int pitch = 0;
    if (ff >= 0) {
        set_size(ff, size);
        if (FT_Load_Char(g_face[ff], cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) return NULL;
        FT_GlyphSlot gs = g_face[ff]->glyph;
        w = gs->bitmap.width; hh = gs->bitmap.rows;
        l = gs->bitmap_left; t = gs->bitmap_top;
        adv = gs->advance.x / 64.0f;
        src = gs->bitmap.buffer;
        pitch = gs->bitmap.pitch;
    }
    if (g_px + w + 1 > ATLAS) { g_px = 0; g_py += g_rowh + 1; g_rowh = 0; }
    if (g_py + hh + 1 > ATLAS) return NULL;
    if (w > 0 && hh > 0) {
        uint8_t *d = gfx_tex_data(g_atlas);
        int st = gfx_tex_stride(g_atlas);
        for (int y = 0; y < hh; y++) memcpy(d + (g_py + y) * st + g_px, src + y * pitch, w);
        gfx_tex_dirty(g_atlas);
    }

    Glyph *g = NULL;
    for (int probe = 0; probe < HSIZE; probe++) {
        g = &g_tab[(h + probe) & (HSIZE - 1)];
        if (g->key == 0) break;
    }
    g->key = key;
    g->x = g_px; g->y = g_py; g->w = w; g->h = hh;
    g->l = l; g->t = t;
    g->adv = adv;
    if (w > 0 && hh > 0) {
        g_px += w + 1;
        if (hh > g_rowh) g_rowh = hh;
    }
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

static float run_cp(int f, float px, float x, float y, uint32_t c, const uint32_t *cps, int n, float tr, int draw) {
    int size; float k;
    raster_for(px, &size, &k);
    float base = draw ? y + font_ascent(f, px) : 0;
    float pen = x;
    for (int i = 0; i < n; i++) {
        Glyph *g = get_glyph(f, size, (int)cps[i]);
        if (!g) continue;
        if (draw && g->w > 0)
            gfx_tex_draw(g_atlas, g->x, g->y, g->w, g->h, roundf(pen + g->l * k), roundf(base - g->t * k),
                         g->w * k, g->h * k, c);
        pen += g->adv * k + tr;
    }
    return pen - x;
}

float font_draw_ex(int f, float px, float x, float y, uint32_t c, const char *s, float tr) {
    uint32_t cps[MAXTEXT];
    int n = shape_text(s, cps, MAXTEXT);
    return run_cp(f, px, x, y, c, cps, n, tr, 1);
}
float font_draw(int f, float px, float x, float y, uint32_t c, const char *s) { return font_draw_ex(f, px, x, y, c, s, 0); }

float font_width_ex(int f, float px, const char *s, float tr) {
    uint32_t cps[MAXTEXT];
    int n = shape_text(s, cps, MAXTEXT);
    return run_cp(f, px, 0, 0, 0, cps, n, tr, 0);
}
float font_width(int f, float px, const char *s) { return font_width_ex(f, px, s, 0); }

float font_draw_right(int f, float px, float xr, float y, uint32_t c, const char *s) { float w = font_width(f, px, s); font_draw(f, px, xr - w, y, c, s); return w; }
float font_draw_center(int f, float px, float xc, float y, uint32_t c, const char *s) { float w = font_width(f, px, s); font_draw(f, px, xc - w / 2, y, c, s); return w; }

float font_draw_fit_ex(int f, float px, float x, float y, float maxw, uint32_t c, const char *s, float tr) {
    uint32_t cps[MAXTEXT];
    int n = shape_text(s, cps, MAXTEXT);
    if (run_cp(f, px, 0, 0, 0, cps, n, tr, 0) <= maxw) return run_cp(f, px, x, y, c, cps, n, tr, 1);
    uint32_t ell[3] = {'.', '.', '.'};
    float ew = run_cp(f, px, 0, 0, 0, ell, 3, tr, 0);
    int size; float k;
    raster_for(px, &size, &k);
    float pen = 0;
    int keep = 0;
    for (int i = 0; i < n; i++) {
        Glyph *g = get_glyph(f, size, (int)cps[i]);
        float adv = g ? g->adv * k + tr : 0;
        if (pen + adv + ew > maxw) break;
        pen += adv;
        keep = i + 1;
    }
    while (keep > 0 && cps[keep - 1] == ' ') keep--;
    uint32_t out[MAXTEXT];
    memcpy(out, cps, (size_t)keep * sizeof *out);
    memcpy(out + keep, ell, sizeof ell);
    return run_cp(f, px, x, y, c, out, keep + 3, tr, 1);
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
    uint32_t cps[MAXTEXT];
    int n = shape_text(s, cps, MAXTEXT);
    float pen = 0;
    for (int i = 0; i < n; i++) {
        int ff = resolve(f, (int)cps[i]);
        if (ff < 0) continue;
        set_size(ff, px);
        FT_Face fx = g_face[ff];
        if (FT_Load_Char(fx, cps[i], FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT)) continue;
        FT_GlyphSlot gs = fx->glyph;
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
    memset(g_cur_size, 0, sizeof g_cur_size);
    return t;
}
