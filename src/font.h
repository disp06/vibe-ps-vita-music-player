#ifndef VIBE_FONT_H
#define VIBE_FONT_H
#include <stdint.h>
#include "gfx.h"

/* the first three are the skin faces; the rest are script fallbacks */
enum { FONT_LIGHT = 0, FONT_REGULAR = 1, FONT_MONO = 2, FONT_CJK = 3, FONT_DEVA = 4, FONT_ARAB = 5, FONT_COUNT };

int font_init(const char *dir);
void font_fini(void);
/* call before gfx_begin: the glyph atlas is only compacted between frames */
void font_frame_begin(void);
/* y is the top of the line box; returns the advance width */
float font_draw(int f, float px, float x, float y, uint32_t c, const char *s);
float font_draw_ex(int f, float px, float x, float y, uint32_t c, const char *s, float tracking);
float font_width(int f, float px, const char *s);
float font_width_ex(int f, float px, const char *s, float tracking);
/* draws, truncating with an ellipsis to fit maxw */
float font_draw_fit(int f, float px, float x, float y, float maxw, uint32_t c, const char *s);
float font_draw_fit_ex(int f, float px, float x, float y, float maxw, uint32_t c, const char *s, float tracking);
float font_draw_right(int f, float px, float xr, float y, uint32_t c, const char *s);
float font_draw_center(int f, float px, float xc, float y, uint32_t c, const char *s);
float font_ascent(int f, float px);
/* renders text once into an RGBA texture with a horizontal colour gradient */
GTex *font_render_gradient(int f, int px, const char *s, uint32_t c0, uint32_t cm, uint32_t c1, int *w, int *h);
int utf8_next(const char **s);

#endif
