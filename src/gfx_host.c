/* Desktop preview backend (Cairo). Renders frames to PNG so layouts and skins can
 * be checked without hardware. Not part of the Vita build. */
#include "gfx.h"
#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct GTex { cairo_surface_t *s; int fmt; int w, h; };

static int g_orient;
static cairo_surface_t *g_surf;
static cairo_t *g_cr;
static int g_frame;
const char *gfx_host_out = "frame.png";

static void src(uint32_t c) {
    cairo_set_source_rgba(g_cr, (c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0,
                          ((c >> 16) & 0xFF) / 255.0, (c >> 24) / 255.0);
}

int gfx_init(void) { return 0; }
void gfx_fini(void) {}
void gfx_set_orientation(int o) { g_orient = o; }
int gfx_orientation(void) { return g_orient; }
void gfx_set_shift(float dx, float dy) { (void)dx; (void)dy; }
int gfx_w(void) { return g_orient == ORIENT_LANDSCAPE ? 960 : 544; }
int gfx_h(void) { return g_orient == ORIENT_LANDSCAPE ? 544 : 960; }

void gfx_begin(uint32_t clear) {
    if (g_cr) { cairo_destroy(g_cr); cairo_surface_destroy(g_surf); }
    g_surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, gfx_w(), gfx_h());
    g_cr = cairo_create(g_surf);
    src(clear | 0xFF000000u);
    cairo_paint(g_cr);
}

void gfx_end(void) {
    cairo_surface_flush(g_surf);
    cairo_surface_write_to_png(g_surf, gfx_host_out);
    g_frame++;
}

void gfx_wait_idle(void) {}

void gfx_rect(float x, float y, float w, float h, uint32_t c) {
    src(c);
    cairo_rectangle(g_cr, x, y, w, h);
    cairo_fill(g_cr);
}

void gfx_tris(const GVert *v, int n) {
    for (int i = 0; i + 2 < n; i += 3) {
        cairo_pattern_t *p = NULL;
        if (v[i].c == v[i + 1].c && v[i].c == v[i + 2].c) {
            src(v[i].c);
        } else {
            p = cairo_pattern_create_mesh();
            cairo_mesh_pattern_begin_patch(p);
            cairo_mesh_pattern_move_to(p, v[i].x, v[i].y);
            cairo_mesh_pattern_line_to(p, v[i + 1].x, v[i + 1].y);
            cairo_mesh_pattern_line_to(p, v[i + 2].x, v[i + 2].y);
            for (int k = 0; k < 3; k++) {
                uint32_t c = v[i + k].c;
                cairo_mesh_pattern_set_corner_color_rgba(p, k, (c & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0,
                                                         ((c >> 16) & 0xFF) / 255.0, (c >> 24) / 255.0);
            }
            cairo_mesh_pattern_end_patch(p);
            cairo_set_source(g_cr, p);
        }
        cairo_move_to(g_cr, v[i].x, v[i].y);
        cairo_line_to(g_cr, v[i + 1].x, v[i + 1].y);
        cairo_line_to(g_cr, v[i + 2].x, v[i + 2].y);
        cairo_close_path(g_cr);
        /* tiny stroke hides the seams Cairo's AA leaves between adjacent triangles */
        cairo_fill_preserve(g_cr);
        cairo_set_line_width(g_cr, 0.35);
        cairo_stroke(g_cr);
        if (p) cairo_pattern_destroy(p);
    }
}

void gfx_clip_raw(float x, float y, float w, float h) {
    cairo_reset_clip(g_cr);
    cairo_rectangle(g_cr, x, y, w, h);
    cairo_clip(g_cr);
}
void gfx_noclip_raw(void) { cairo_reset_clip(g_cr); }

GTex *gfx_tex_create(int w, int h, int fmt) {
    GTex *t = calloc(1, sizeof *t);
    t->s = cairo_image_surface_create(fmt == 0 ? CAIRO_FORMAT_A8 : CAIRO_FORMAT_ARGB32, w, h);
    t->fmt = fmt;
    t->w = w;
    t->h = h;
    memset(cairo_image_surface_get_data(t->s), 0, cairo_image_surface_get_stride(t->s) * h);
    return t;
}

/* host build reads PNG only (enough for previews) */
typedef struct { const uint8_t *p; int n, off; } Rd;
static cairo_status_t rd(void *c, unsigned char *d, unsigned int len) {
    Rd *r = c;
    if (r->off + (int)len > r->n) return CAIRO_STATUS_READ_ERROR;
    memcpy(d, r->p + r->off, len);
    r->off += len;
    return CAIRO_STATUS_SUCCESS;
}
GTex *gfx_tex_load(const void *data, int size) {
    Rd r = {data, size, 0};
    cairo_surface_t *s = cairo_image_surface_create_from_png_stream(rd, &r);
    if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(s); return NULL; }
    GTex *t = calloc(1, sizeof *t);
    t->s = s;
    t->fmt = 2; /* native cairo, premultiplied */
    t->w = cairo_image_surface_get_width(s);
    t->h = cairo_image_surface_get_height(s);
    return t;
}

void gfx_tex_free(GTex *t) { if (t) { cairo_surface_destroy(t->s); free(t); } }
uint8_t *gfx_tex_data(GTex *t) { cairo_surface_flush(t->s); return cairo_image_surface_get_data(t->s); }
int gfx_tex_stride(GTex *t) { return cairo_image_surface_get_stride(t->s); }
int gfx_tex_w(GTex *t) { return t->w; }
int gfx_tex_h(GTex *t) { return t->h; }

/* RGBA8 data written by the app is straight RGBA bytes; Cairo wants premultiplied BGRA */
void gfx_tex_dirty(GTex *t) {
    if (t->fmt == 1) {
        uint8_t *d = cairo_image_surface_get_data(t->s);
        int st = cairo_image_surface_get_stride(t->s);
        for (int y = 0; y < t->h; y++)
            for (int x = 0; x < t->w; x++) {
                uint8_t *p = d + y * st + x * 4;
                uint8_t r = p[0], g = p[1], b = p[2], a = p[3];
                p[0] = b * a / 255; p[1] = g * a / 255; p[2] = r * a / 255; p[3] = a;
            }
        t->fmt = 3; /* converted */
    }
    cairo_surface_mark_dirty(t->s);
}

void gfx_tex_draw(GTex *t, float sx, float sy, float sw, float sh,
                  float dx, float dy, float dw, float dh, uint32_t tint) {
    if (!t || sw <= 0 || sh <= 0) return;
    cairo_save(g_cr);
    cairo_rectangle(g_cr, dx, dy, dw, dh);
    cairo_clip(g_cr);
    cairo_translate(g_cr, dx, dy);
    cairo_scale(g_cr, dw / sw, dh / sh);
    if (t->fmt == 0) {
        src(tint);
        cairo_mask_surface(g_cr, t->s, -sx, -sy);
    } else {
        cairo_set_source_surface(g_cr, t->s, -sx, -sy);
        cairo_paint_with_alpha(g_cr, (tint >> 24) / 255.0);
    }
    cairo_restore(g_cr);
}

void gfx_tex_draw_rot(GTex *t, float cx, float cy, float scale, float rad, uint32_t tint) {
    if (!t) return;
    cairo_save(g_cr);
    cairo_translate(g_cr, cx, cy);
    cairo_rotate(g_cr, rad);
    cairo_scale(g_cr, scale, scale);
    cairo_translate(g_cr, -t->w / 2.0, -t->h / 2.0);
    if (t->fmt == 0) { src(tint); cairo_mask_surface(g_cr, t->s, 0, 0); }
    else { cairo_set_source_surface(g_cr, t->s, 0, 0); cairo_paint_with_alpha(g_cr, (tint >> 24) / 255.0); }
    cairo_restore(g_cr);
}
