/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "gfx.h"
#include "fmath.h"

void gfx_init(gfx_t *g, uint8_t *fb, int fb_w, int fb_h, int stride, int orient)
{
    g->fb = fb;
    g->fb_w = fb_w;
    g->fb_h = fb_h;
    g->stride = stride;
    gfx_set_orient(g, orient);
}

void gfx_set_orient(gfx_t *g, int orient)
{
    g->orient = orient;
    g->SW = orient == APP_ORIENT_BAR_LEFT || orient == APP_ORIENT_BAR_RIGHT ? g->fb_h : g->fb_w;
    g->SH = orient == APP_ORIENT_BAR_LEFT || orient == APP_ORIENT_BAR_RIGHT ? g->fb_w : g->fb_h;
    gfx_screen(g);
}

void gfx_screen(gfx_t *g)
{
    g->ox = g->oy = 0;
    g->W = g->SW;
    g->H = g->SH;
    gfx_noclip(g);
}

void gfx_content(gfx_t *g, int depth, int reserve)
{
    gfx_screen(g);
    switch (g->orient) {
    case APP_ORIENT_BAR_LEFT: g->ox = depth; g->W = g->SW - depth; g->H = g->SH + reserve; break;
    case APP_ORIENT_BAR_RIGHT: g->W = g->SW - depth; g->H = g->SH + reserve; break;
    case APP_ORIENT_BAR_TOP: g->oy = depth; break;
    default: break;
    }
    gfx_noclip(g);
}

void gfx_fill_all(gfx_t *g, int c)
{
    for (int y = 0; y < g->fb_h; y++)
        for (int b = 0; b < (g->fb_w + 7) / 8; b++) g->fb[y * g->stride + b] = c ? 0xff : 0;
}

void gfx_noclip(gfx_t *g)
{
    g->cx0 = 0;
    g->cy0 = 0;
    g->cx1 = g->W - 1;
    g->cy1 = g->H - 1;
}

void gfx_clip(gfx_t *g, int x0, int y0, int x1, int y1)
{
    g->cx0 = x0 < 0 ? 0 : x0;
    g->cy0 = y0 < 0 ? 0 : y0;
    g->cx1 = x1 > g->W - 1 ? g->W - 1 : x1;
    g->cy1 = y1 > g->H - 1 ? g->H - 1 : y1;
}

static inline void fb_set(gfx_t *g, int fx, int fy, int c)
{
    uint8_t *p = g->fb + fy * g->stride + (fx >> 3);
    uint8_t m = (uint8_t)(0x80 >> (fx & 7));
    if (c) *p |= m;
    else *p &= (uint8_t)~m;
}

/* canvas (x, y) to the framebuffer; 0 when it falls outside the screen */
static inline int to_fb(const gfx_t *g, int x, int y, int *fx, int *fy)
{
    int ax = x + g->ox, ay = y + g->oy;
    if (ax < 0 || ay < 0 || ax >= g->SW || ay >= g->SH) return 0;
    app_orient_to_fb(g->orient, g->fb_w, g->fb_h, ax, ay, fx, fy);
    return 1;
}

void gfx_px(gfx_t *g, int x, int y, int c)
{
    int fx, fy;
    if (c < 0 || x < g->cx0 || x > g->cx1 || y < g->cy0 || y > g->cy1 || !to_fb(g, x, y, &fx, &fy)) return;
    fb_set(g, fx, fy, c);
}

int gfx_get(const gfx_t *g, int x, int y)
{
    int fx, fy;
    if (x < 0 || y < 0 || x >= g->W || y >= g->H || !to_fb(g, x, y, &fx, &fy)) return 0;
    return (g->fb[fy * g->stride + (fx >> 3)] >> (7 - (fx & 7))) & 1;
}

void gfx_hspan(gfx_t *g, int x0, int x1, int y, int c)
{
    if (c < 0 || y < g->cy0 || y > g->cy1) return;
    if (x0 < g->cx0) x0 = g->cx0;
    if (x1 > g->cx1) x1 = g->cx1;
    if (x0 > x1) return;
    if (g->orient != APP_ORIENT_BAR_BOTTOM || g->ox || g->oy) {
        for (int x = x0; x <= x1; x++) gfx_px(g, x, y, c);
        return;
    }
    uint8_t *row = g->fb + y * g->stride;
    while (x0 <= x1 && (x0 & 7)) fb_set(g, x0++, y, c);
    while (x0 + 7 <= x1) {
        row[x0 >> 3] = c ? 0xff : 0;
        x0 += 8;
    }
    while (x0 <= x1) fb_set(g, x0++, y, c);
}

void gfx_fill(gfx_t *g, int c) { gfx_fill_rect(g, 0, 0, (float)g->W - 1, (float)g->H - 1, c); }

void gfx_fill_rect(gfx_t *g, float x0, float y0, float x1, float y1, int c)
{
    int a = fm_round(x0), b = fm_round(x1), t = fm_round(y0), u = fm_round(y1);
    if (t < g->cy0) t = g->cy0;
    if (u > g->cy1) u = g->cy1;
    for (int y = t; y <= u; y++) gfx_hspan(g, a, b, y, c);
}

/* Row extent of a rounded box (pixel centers inside [x0, x1+1] with corner radius r); 0 if the row misses it */
static int rr_extent(int x0, int y0, int x1, int y1, float r, int y, int *l, int *rt)
{
    if (y < y0 || y > y1) return 0;
    float yc = (float)y + 0.5f, dy = 0;
    if (yc < y0 + r) dy = y0 + r - yc;
    else if (yc > y1 + 1 - r) dy = yc - (y1 + 1 - r);
    float dx = r;
    if (dy > 0) {
        if (dy > r) return 0;
        dx = fm_sqrt(r * r - dy * dy);
    }
    float left = x0 + r - dx, right = x1 + 1 - r + dx;
    *l = -fm_floor(0.5f - left);
    *rt = fm_floor(right - 0.5f);
    return *l <= *rt;
}

void gfx_rrect(gfx_t *g, float fx0, float fy0, float fx1, float fy1, float r, int w, int fill, int outline)
{
    int x0 = fm_round(fx0), y0 = fm_round(fy0), x1 = fm_round(fx1), y1 = fm_round(fy1);
    float rmax = (float)((x1 - x0 + 1) < (y1 - y0 + 1) ? (x1 - x0 + 1) : (y1 - y0 + 1)) / 2;
    if (r > rmax) r = rmax;
    if (r < 0) r = 0;
    float ri = r - (float)w;
    if (ri < 0) ri = 0;
    int ya = y0 < g->cy0 ? g->cy0 : y0, yb = y1 > g->cy1 ? g->cy1 : y1;
    for (int y = ya; y <= yb; y++) {
        int l, rt, il, ir;
        if (!rr_extent(x0, y0, x1, y1, r, y, &l, &rt)) continue;
        int in = w > 0 ? rr_extent(x0 + w, y0 + w, x1 - w, y1 - w, ri, y, &il, &ir) : 0;
        if (w <= 0) {
            gfx_hspan(g, l, rt, y, fill);
            continue;
        }
        if (!in) {
            gfx_hspan(g, l, rt, y, outline);
            continue;
        }
        gfx_hspan(g, l, il - 1, y, outline);
        gfx_hspan(g, il, ir, y, fill);
        gfx_hspan(g, ir + 1, rt, y, outline);
    }
}

static int el_extent(float cx, float cy, float a, float b, int y, int *l, int *rt)
{
    if (a <= 0 || b <= 0) return 0;
    float dy = ((float)y + 0.5f - cy) / b;
    if (dy * dy > 1) return 0;
    float dx = a * fm_sqrt(1 - dy * dy);
    *l = -fm_floor(0.5f - (cx - dx));
    *rt = fm_floor(cx + dx - 0.5f);
    return *l <= *rt;
}

void gfx_ellipse(gfx_t *g, float fx0, float fy0, float fx1, float fy1, int w, int fill, int outline)
{
    int x0 = fm_round(fx0), y0 = fm_round(fy0), x1 = fm_round(fx1), y1 = fm_round(fy1);
    float cx = (float)(x0 + x1 + 1) / 2, cy = (float)(y0 + y1 + 1) / 2;
    float a = (float)(x1 + 1 - x0) / 2, b = (float)(y1 + 1 - y0) / 2;
    int ya = y0 < g->cy0 ? g->cy0 : y0, yb = y1 > g->cy1 ? g->cy1 : y1;
    for (int y = ya; y <= yb; y++) {
        int l, rt, il, ir;
        if (!el_extent(cx, cy, a, b, y, &l, &rt)) continue;
        if (w <= 0) {
            gfx_hspan(g, l, rt, y, fill);
            continue;
        }
        if (!el_extent(cx, cy, a - (float)w, b - (float)w, y, &il, &ir)) {
            gfx_hspan(g, l, rt, y, outline);
            continue;
        }
        gfx_hspan(g, l, il - 1, y, outline);
        gfx_hspan(g, il, ir, y, fill);
        gfx_hspan(g, ir + 1, rt, y, outline);
    }
}

#define POLY_MAX_X 64

void gfx_poly(gfx_t *g, const float *xy, int n, int c)
{
    if (n < 3 || c < 0) return;
    float ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) {
        if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
        if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
    }
    int ya = fm_floor(ymin), yb = fm_floor(ymax) + 1;
    if (ya < g->cy0) ya = g->cy0;
    if (yb > g->cy1) yb = g->cy1;
    for (int y = ya; y <= yb; y++) {
        float xs[POLY_MAX_X];
        int k = 0;
        float yc = (float)y;
        for (int i = 0; i < n && k < POLY_MAX_X; i++) {
            float ax = xy[2 * i], ay = xy[2 * i + 1];
            float bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
            if ((ay <= yc && yc < by) || (by <= yc && yc < ay)) xs[k++] = ax + (yc - ay) * (bx - ax) / (by - ay);
        }
        for (int i = 1; i < k; i++)
            for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
                float t = xs[j];
                xs[j] = xs[j - 1];
                xs[j - 1] = t;
            }
        for (int i = 0; i + 1 < k; i += 2) gfx_hspan(g, -fm_floor(-xs[i]), fm_floor(xs[i + 1]), y, c);
    }
}

void gfx_line(gfx_t *g, float x0, float y0, float x1, float y1, int w, int c)
{
    float dx = x1 - x0, dy = y1 - y0, len = fm_sqrt(dx * dx + dy * dy);
    if (w < 1) w = 1;
    if (len < 1e-3f) {
        gfx_fill_rect(g, x0 - (float)(w - 1) / 2, y0 - (float)(w - 1) / 2, x0 + (float)w / 2, y0 + (float)w / 2, c);
        return;
    }
    if (w == 1) { /* thin: one pixel per step */
        int steps = (int)(fm_abs(dx) > fm_abs(dy) ? fm_abs(dx) : fm_abs(dy)) + 1;
        for (int i = 0; i <= steps; i++)
            gfx_px(g, fm_round(x0 + dx * (float)i / (float)steps), fm_round(y0 + dy * (float)i / (float)steps), c);
        return;
    }
    float nx = -dy / len * (float)w / 2, ny = dx / len * (float)w / 2;
    float q[8] = { x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny };
    gfx_poly(g, q, 4, c);
}

void gfx_poly_outline(gfx_t *g, const float *xy, int n, int w, int c)
{
    for (int i = 0; i < n; i++)
        gfx_line(g, xy[2 * i], xy[2 * i + 1], xy[2 * ((i + 1) % n)], xy[2 * ((i + 1) % n) + 1], w, c);
}

int32_t gfx_text(gfx_t *g, const font_face_t *f, int32_t x64, int y, const char *s, int tab, int c)
{
    int32_t start = x64, dw = 0;
    if (tab) {
        for (char d = '0'; d <= '9'; d++) {
            const font_glyph_t *q = font_glyph(f, (uint32_t)d);
            if (q && (int32_t)q->adv64 > dw) dw = (int32_t)q->adv64;
        }
    }
    uint32_t cp;
    while ((cp = font_next_cp(&s))) {
        const font_glyph_t *q = font_glyph(f, cp);
        if (!q) continue;
        int digit = tab && cp >= '0' && cp <= '9';
        int32_t pen = digit ? x64 + (dw - (int32_t)q->adv64) / 2 : x64;
        int px = (int)((pen + 32) >> 6) + q->xoff, py = y + q->yoff;
        const uint8_t *d = f->data + q->off;
        int wide = q->w > 255;
        for (int row = 0; row < q->h;) {
            int rep = *d++, n = *d++;
            const uint8_t *runs = d;
            for (int r = 0; r < rep; r++) {
                const uint8_t *p = runs;
                for (int i = 0; i < n; i++) {
                    int rx, rl;
                    if (wide) {
                        rx = p[0] | p[1] << 8;
                        rl = p[2] | p[3] << 8;
                        p += 4;
                    } else {
                        rx = p[0];
                        rl = p[1];
                        p += 2;
                    }
                    gfx_hspan(g, px + rx, px + rx + rl - 1, py + row + r, c);
                }
            }
            d = runs + n * (wide ? 4 : 2);
            row += rep;
        }
        x64 += digit ? dw : (int32_t)q->adv64;
    }
    return x64 - start;
}

static const uint8_t BAYER[8][8] = { { 0, 32, 8, 40, 2, 34, 10, 42 }, { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44, 4, 36, 14, 46, 6, 38 }, { 60, 28, 52, 20, 62, 30, 54, 22 }, { 3, 35, 11, 43, 1, 33, 9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 }, { 15, 47, 7, 39, 13, 45, 5, 37 }, { 63, 31, 55, 23, 61, 29, 53, 21 } };

void gfx_dither_ring(gfx_t *g, float cx, float cy, float rout, float rin, float density)
{
    int lim = (int)(density * 64);
    for (int y = fm_floor(cy - rout); y <= fm_floor(cy + rout) + 1; y++)
        for (int x = fm_floor(cx - rout); x <= fm_floor(cx + rout) + 1; x++) {
            float dx = (float)x - cx, dy = (float)y - cy, d2 = dx * dx + dy * dy;
            if (d2 <= rout * rout && d2 > rin * rin && BAYER[y & 7][x & 7] < lim) gfx_px(g, x, y, GFX_BLACK);
        }
}
