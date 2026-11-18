/* Vibe - thin 2D drawing layer.
 * Two backends: gfx_vita.c (vita2d / GXM) and gfx_host.c (Cairo, desktop preview).
 * All coordinates are LOGICAL: (gfx_w() x gfx_h()), which is 960x544 in landscape
 * and 544x960 in portrait. Rotation and burn-in pixel shift are applied by the
 * projection matrix on the Vita, so they cost nothing per draw call. */
#ifndef VIBE_GFX_H
#define VIBE_GFX_H
#include <stdint.h>

/* Colours are 0xAABBGGRR (vita2d byte order) */
#define RGBA(r, g, b, a) ((uint32_t)(((a) & 0xFF) << 24 | ((b) & 0xFF) << 16 | ((g) & 0xFF) << 8 | ((r) & 0xFF)))
#define RGB(r, g, b) RGBA(r, g, b, 255)
#define COL_A(c) ((c) >> 24)
#define COL_WITH_A(c, a) (((c) & 0x00FFFFFFu) | ((uint32_t)((a) & 0xFF) << 24))
uint32_t col_mix(uint32_t a, uint32_t b, float t);
uint32_t col_alpha(uint32_t c, float k); /* multiply alpha */
uint32_t col_hex(uint32_t rgb);          /* 0xRRGGBB -> RGB() */

enum { ORIENT_LANDSCAPE = 0, ORIENT_PORTRAIT = 1, ORIENT_PORTRAIT_FLIP = 2 };

typedef struct GTex GTex;
typedef struct { float x, y; uint32_t c; } GVert;

int gfx_init(void);
void gfx_fini(void);
void gfx_set_orientation(int o);
int gfx_orientation(void);
void gfx_set_shift(float dx, float dy); /* burn-in protection offset */
int gfx_w(void);
int gfx_h(void);

void gfx_begin(uint32_t clear);
void gfx_end(void); /* end + present */
void gfx_wait_idle(void);

void gfx_rect(float x, float y, float w, float h, uint32_t c);
void gfx_rect_grad_v(float x, float y, float w, float h, uint32_t top, uint32_t bot);
void gfx_rect_grad_h(float x, float y, float w, float h, uint32_t l, uint32_t r);
void gfx_frame(float x, float y, float w, float h, float t, uint32_t c);
void gfx_line(float x0, float y0, float x1, float y1, float t, uint32_t c);
void gfx_tris(const GVert *v, int n);   /* triangle list */
void gfx_circle(float cx, float cy, float r, uint32_t c);
void gfx_arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t c);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c);

void gfx_clip(float x, float y, float w, float h);   /* intersected with the outer clip */
void gfx_noclip(void);                                 /* back to the outer clip */
void gfx_clip_outer(float x, float y, float w, float h); /* set the outer clip (w <= 0: none) */
void gfx_clip_raw(float x, float y, float w, float h);
void gfx_noclip_raw(void);

/* textures: fmt 0 = A8 (tintable mask), 1 = RGBA8888 */
GTex *gfx_tex_create(int w, int h, int fmt);
GTex *gfx_tex_load(const void *data, int size); /* jpeg / png, downscaled to <= 512 */
void gfx_tex_free(GTex *t);
uint8_t *gfx_tex_data(GTex *t);
int gfx_tex_stride(GTex *t); /* bytes per row */
int gfx_tex_w(GTex *t);
int gfx_tex_h(GTex *t);
void gfx_tex_dirty(GTex *t);
void gfx_tex_draw(GTex *t, float sx, float sy, float sw, float sh,
                  float dx, float dy, float dw, float dh, uint32_t tint);
void gfx_tex_draw_rot(GTex *t, float cx, float cy, float scale, float rad, uint32_t tint);

#endif
