/* vita2d backend. Rotation for portrait mode and the OLED burn-in pixel shift are
 * folded into vita2d's projection matrix, so every primitive vita2d draws lands
 * rotated for free (no render-to-texture pass). */
#include "gfx.h"
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdio.h>
#include <jpeglib.h>
#include <vita2d.h>

extern float _vita2d_ortho_matrix[16]; /* global in libvita2d */

struct GTex { vita2d_texture *t; int fmt; };

static int g_orient = ORIENT_LANDSCAPE;
static float g_dx, g_dy;
static float g_base[16];

static void apply_matrix(int orient, float dx, float dy) {
    float *m = _vita2d_ortho_matrix;
    memcpy(m, g_base, sizeof g_base);
    m[0x0] = m[0x1] = m[0x4] = m[0x5] = 0;
    switch (orient) {
    case ORIENT_PORTRAIT: /* console turned 90 deg counter-clockwise (buttons up) */
        m[0x4] = -2.0f / 960.0f; m[0xC] = 1.0f - 2.0f * dy / 960.0f;
        m[0x1] = -2.0f / 544.0f; m[0xD] = 1.0f - 2.0f * dx / 544.0f;
        break;
    case ORIENT_PORTRAIT_FLIP: /* console turned 90 deg clockwise (d-pad up) */
        m[0x4] = 2.0f / 960.0f; m[0xC] = -1.0f + 2.0f * dy / 960.0f;
        m[0x1] = 2.0f / 544.0f; m[0xD] = -1.0f + 2.0f * dx / 544.0f;
        break;
    default:
        m[0x0] = 2.0f / 960.0f; m[0xC] = -1.0f + 2.0f * dx / 960.0f;
        m[0x5] = -2.0f / 544.0f; m[0xD] = 1.0f - 2.0f * dy / 544.0f;
        break;
    }
}

int gfx_init(void) {
    vita2d_init_advanced_with_msaa(1 * 1024 * 1024, SCE_GXM_MULTISAMPLE_4X);
    vita2d_set_vblank_wait(1);
    memcpy(g_base, _vita2d_ortho_matrix, sizeof g_base);
    apply_matrix(g_orient, 0, 0);
    return 0;
}

void gfx_fini(void) {
    vita2d_wait_rendering_done();
    vita2d_fini();
}

void gfx_set_orientation(int o) { g_orient = o; apply_matrix(g_orient, g_dx, g_dy); }
int gfx_orientation(void) { return g_orient; }
void gfx_set_shift(float dx, float dy) { g_dx = dx; g_dy = dy; apply_matrix(g_orient, g_dx, g_dy); }
int gfx_w(void) { return g_orient == ORIENT_LANDSCAPE ? 960 : 544; }
int gfx_h(void) { return g_orient == ORIENT_LANDSCAPE ? 544 : 960; }

void gfx_begin(uint32_t clear) {
    vita2d_set_clear_color(clear);
    vita2d_start_drawing();
    vita2d_clear_screen();
}

void gfx_end(void) {
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void gfx_wait_idle(void) { vita2d_wait_rendering_done(); }

void gfx_rect(float x, float y, float w, float h, uint32_t c) { vita2d_draw_rectangle(x, y, w, h, c); }

void gfx_tris(const GVert *v, int n) {
    if (n < 3) return;
    vita2d_color_vertex *cv = vita2d_pool_memalign(n * sizeof(vita2d_color_vertex), sizeof(vita2d_color_vertex));
    if (!cv) return;
    for (int i = 0; i < n; i++) {
        cv[i].x = v[i].x;
        cv[i].y = v[i].y;
        cv[i].z = 0.5f;
        cv[i].color = v[i].c;
    }
    vita2d_draw_array(SCE_GXM_PRIMITIVE_TRIANGLES, cv, n);
}

/* the stencil clip is drawn through the projection, so set it up in plain
 * screen space and map the logical rectangle by hand */
void gfx_clip_raw(float x, float y, float w, float h) {
    float sx0, sy0, sx1, sy1;
    switch (g_orient) {
    case ORIENT_PORTRAIT: sx0 = 960 - (y + h); sx1 = 960 - y; sy0 = x; sy1 = x + w; break;
    case ORIENT_PORTRAIT_FLIP: sx0 = y; sx1 = y + h; sy0 = 544 - (x + w); sy1 = 544 - x; break;
    default: sx0 = x; sx1 = x + w; sy0 = y; sy1 = y + h; break;
    }
    apply_matrix(ORIENT_LANDSCAPE, 0, 0);
    vita2d_set_clip_rectangle((int)sx0, (int)sy0, (int)sx1, (int)sy1);
    vita2d_enable_clipping();
    apply_matrix(g_orient, g_dx, g_dy);
}

void gfx_noclip_raw(void) {
    apply_matrix(ORIENT_LANDSCAPE, 0, 0);
    vita2d_disable_clipping();
    apply_matrix(g_orient, g_dx, g_dy);
}

GTex *gfx_tex_create(int w, int h, int fmt) {
    vita2d_texture *t = fmt == 0 ? vita2d_create_empty_texture_format(w, h, SCE_GXM_TEXTURE_FORMAT_U8_R111)
                                 : vita2d_create_empty_texture(w, h);
    if (!t) return NULL;
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    GTex *g = calloc(1, sizeof *g);
    g->t = t;
    g->fmt = fmt;
    return g;
}

static GTex *wrap(vita2d_texture *t) {
    if (!t) return NULL;
    vita2d_texture_set_filters(t, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
    GTex *g = calloc(1, sizeof *g);
    g->t = t;
    g->fmt = 1;
    return g;
}

static jmp_buf g_jb; /* covers are decoded on the UI thread only */
static void jerr_exit(j_common_ptr c) { longjmp(g_jb, 1); }

GTex *gfx_tex_load(const void *data, int size) {
    const uint8_t *b = data;
    if (size > 8 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return wrap(vita2d_load_PNG_buffer(data));
    if (size < 4 || b[0] != 0xFF || b[1] != 0xD8) return NULL;
    /* JPEG: decode at the smallest DCT scale (1/1..1/8) that still covers 512 px,
     * which keeps big embedded covers cheap in both time and memory */
    struct jpeg_decompress_struct ci;
    struct jpeg_error_mgr je;
    ci.err = jpeg_std_error(&je);
    je.error_exit = jerr_exit;
    vita2d_texture *t = NULL;
    uint8_t *row = NULL;
    if (setjmp(g_jb)) {
        jpeg_destroy_decompress(&ci);
        if (t) vita2d_free_texture(t);
        free(row);
        return NULL;
    }
    jpeg_create_decompress(&ci);
    jpeg_mem_src(&ci, (const unsigned char *)data, size);
    jpeg_read_header(&ci, TRUE);
    ci.out_color_space = JCS_RGB;
    ci.scale_num = 1;
    ci.scale_denom = 1;
    while (ci.scale_denom < 8 && ci.image_width / (ci.scale_denom * 2) >= 512 && ci.image_height / (ci.scale_denom * 2) >= 512)
        ci.scale_denom *= 2;
    ci.dct_method = JDCT_IFAST;
    jpeg_start_decompress(&ci);
    int w = ci.output_width, h = ci.output_height;
    t = vita2d_create_empty_texture(w, h);
    if (!t) longjmp(g_jb, 1);
    uint8_t *dst = vita2d_texture_get_datap(t);
    int st = vita2d_texture_get_stride(t);
    row = malloc(w * 3);
    while (ci.output_scanline < (unsigned)h) {
        int y = ci.output_scanline;
        jpeg_read_scanlines(&ci, &row, 1);
        uint8_t *d = dst + y * st;
        for (int x = 0; x < w; x++) { d[x * 4] = row[x * 3]; d[x * 4 + 1] = row[x * 3 + 1]; d[x * 4 + 2] = row[x * 3 + 2]; d[x * 4 + 3] = 255; }
    }
    jpeg_finish_decompress(&ci);
    jpeg_destroy_decompress(&ci);
    free(row);
    return wrap(t);
}

void gfx_tex_free(GTex *t) {
    if (!t) return;
    vita2d_wait_rendering_done();
    vita2d_free_texture(t->t);
    free(t);
}

uint8_t *gfx_tex_data(GTex *t) { return vita2d_texture_get_datap(t->t); }
int gfx_tex_stride(GTex *t) { return vita2d_texture_get_stride(t->t); }
int gfx_tex_w(GTex *t) { return vita2d_texture_get_width(t->t); }
int gfx_tex_h(GTex *t) { return vita2d_texture_get_height(t->t); }
void gfx_tex_dirty(GTex *t) { (void)t; } /* GPU reads CPU memory directly */

void gfx_tex_draw(GTex *t, float sx, float sy, float sw, float sh,
                  float dx, float dy, float dw, float dh, uint32_t tint) {
    if (!t || sw <= 0 || sh <= 0) return;
    vita2d_draw_texture_tint_part_scale(t->t, dx, dy, sx, sy, sw, sh, dw / sw, dh / sh, tint);
}

void gfx_tex_draw_rot(GTex *t, float cx, float cy, float scale, float rad, uint32_t tint) {
    if (!t) return;
    vita2d_draw_texture_tint_scale_rotate(t->t, cx, cy, scale, scale, rad, tint);
}
