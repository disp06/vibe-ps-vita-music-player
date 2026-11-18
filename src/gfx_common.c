/* Backend-independent shape helpers, all built on gfx_tris(). */
#include "gfx.h"
#include <math.h>

uint32_t col_mix(uint32_t a, uint32_t b, float t) {
    if (t <= 0) return a;
    if (t >= 1) return b;
    uint32_t r = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
        r |= ((uint32_t)(ca + (cb - ca) * t + 0.5f) & 0xFF) << s;
    }
    return r;
}

uint32_t col_alpha(uint32_t c, float k) {
    if (k < 0) k = 0;
    if (k > 1) k = 1;
    return COL_WITH_A(c, (uint32_t)(COL_A(c) * k + 0.5f));
}

uint32_t col_hex(uint32_t rgb) { return RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }

void gfx_rect_grad_v(float x, float y, float w, float h, uint32_t top, uint32_t bot) {
    GVert v[6] = {{x, y, top}, {x + w, y, top}, {x, y + h, bot},
                  {x + w, y, top}, {x + w, y + h, bot}, {x, y + h, bot}};
    gfx_tris(v, 6);
}

void gfx_rect_grad_h(float x, float y, float w, float h, uint32_t l, uint32_t r) {
    GVert v[6] = {{x, y, l}, {x + w, y, r}, {x, y + h, l},
                  {x + w, y, r}, {x + w, y + h, r}, {x, y + h, l}};
    gfx_tris(v, 6);
}

void gfx_frame(float x, float y, float w, float h, float t, uint32_t c) {
    gfx_rect(x, y, w, t, c);
    gfx_rect(x, y + h - t, w, t, c);
    gfx_rect(x, y + t, t, h - 2 * t, c);
    gfx_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void gfx_line(float x0, float y0, float x1, float y1, float t, uint32_t c) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.0001f) return;
    float nx = -dy / len * t * 0.5f, ny = dx / len * t * 0.5f;
    GVert v[6] = {{x0 + nx, y0 + ny, c}, {x1 + nx, y1 + ny, c}, {x0 - nx, y0 - ny, c},
                  {x1 + nx, y1 + ny, c}, {x1 - nx, y1 - ny, c}, {x0 - nx, y0 - ny, c}};
    gfx_tris(v, 6);
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    GVert v[3] = {{x0, y0, c}, {x1, y1, c}, {x2, y2, c}};
    gfx_tris(v, 3);
}

static int segs_for(float r, float span) {
    int n = (int)(r * span * 0.35f);
    if (n < 8) n = 8;
    if (n > 96) n = 96;
    return n;
}

void gfx_circle(float cx, float cy, float r, uint32_t c) {
    GVert v[96 * 3];
    int n = segs_for(r, 6.2832f);
    float pa = 0, px = cx + r, py = cy;
    for (int i = 1; i <= n; i++) {
        pa = 6.2832f * i / n;
        float x = cx + cosf(pa) * r, y = cy + sinf(pa) * r;
        v[(i - 1) * 3 + 0] = (GVert){cx, cy, c};
        v[(i - 1) * 3 + 1] = (GVert){px, py, c};
        v[(i - 1) * 3 + 2] = (GVert){x, y, c};
        px = x;
        py = y;
    }
    gfx_tris(v, n * 3);
}

void gfx_arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t c) {
    GVert v[96 * 6];
    float span = a1 - a0;
    if (span < 0) span = -span;
    int n = segs_for(r, span);
    float ri = r - thick * 0.5f, ro = r + thick * 0.5f;
    if (ri < 0) ri = 0;
    float ca = cosf(a0), sa = sinf(a0);
    for (int i = 1; i <= n; i++) {
        float a = a0 + (a1 - a0) * i / n;
        float cb = cosf(a), sb = sinf(a);
        GVert *q = &v[(i - 1) * 6];
        q[0] = (GVert){cx + ca * ri, cy + sa * ri, c};
        q[1] = (GVert){cx + ca * ro, cy + sa * ro, c};
        q[2] = (GVert){cx + cb * ro, cy + sb * ro, c};
        q[3] = (GVert){cx + ca * ri, cy + sa * ri, c};
        q[4] = (GVert){cx + cb * ro, cy + sb * ro, c};
        q[5] = (GVert){cx + cb * ri, cy + sb * ri, c};
        ca = cb;
        sa = sb;
    }
    gfx_tris(v, n * 6);
}

static float o_x, o_y, o_w = -1, o_h;

void gfx_clip_outer(float x, float y, float w, float h) {
    o_x = x; o_y = y; o_w = w; o_h = h;
    if (w <= 0) gfx_noclip_raw();
    else gfx_clip_raw(x, y, w, h);
}

void gfx_clip(float x, float y, float w, float h) {
    if (o_w > 0) {
        float x1 = fminf(x + w, o_x + o_w), y1 = fminf(y + h, o_y + o_h);
        x = fmaxf(x, o_x); y = fmaxf(y, o_y);
        w = fmaxf(0, x1 - x); h = fmaxf(0, y1 - y);
    }
    gfx_clip_raw(x, y, w, h);
}

void gfx_noclip(void) {
    if (o_w > 0) gfx_clip_raw(o_x, o_y, o_w, o_h);
    else gfx_noclip_raw();
}
