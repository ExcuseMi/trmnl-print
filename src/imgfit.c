/* SPDX-License-Identifier: MIT */
#include "imgfit.h"
#include <string.h>
#include "pcl.h"

int png_dims(const uint8_t *in, size_t n, int *w, int *h)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    if (n < 24 || memcmp(in, sig, 8) || memcmp(in + 12, "IHDR", 4)) return -1;
    uint32_t a = (uint32_t)in[16] << 24 | (uint32_t)in[17] << 16 | (uint32_t)in[18] << 8 | in[19];
    uint32_t b = (uint32_t)in[20] << 24 | (uint32_t)in[21] << 16 | (uint32_t)in[22] << 8 | in[23];
    if (a == 0 || b == 0 || a > IMG_MAX_DIM || b > IMG_MAX_DIM) return -1;
    *w = (int)a;
    *h = (int)b;
    return 0;
}

int fit_compute(int sw, int sh, int pw, int ph, fit_t *f)
{
    if (sw <= 0 || sh <= 0 || sw > IMG_MAX_DIM || sh > IMG_MAX_DIM) return -1;
    f->rotate = sw > sh;
    int ew = f->rotate ? sh : sw, eh = f->rotate ? sw : sh;
    int m = IMG_MARGIN * pw / 2480; /* the margin is in 300 dpi pixels; a 600 dpi page keeps the same millimetres */
    int aw = pw - 2 * m, ah = ph - 2 * m, dw, dh;
    if ((long)ew * ah > (long)eh * aw) { /* limited by the width */
        dw = aw & ~7;
        dh = (int)(((long)eh * dw + ew / 2) / ew);
    } else {
        dh = ah;
        dw = (int)(((long)ew * dh + eh / 2) / eh) & ~7;
    }
    if (dw < 8) dw = 8;
    if (dh < 1) dh = 1;
    f->w = dw;
    f->h = dh;
    f->x = (pw - dw) / 2;
    f->y = (ph - dh) / 2;
    return 0;
}

/* The picture rows that printer row y covers: [*y0, *y1) */
static void src_rows(const fit_t *f, int eh, int y, int *y0, int *y1)
{
    int a = (int)((long)y * eh / f->h), b = (int)(((long)(y + 1) * eh + f->h - 1) / f->h);
    if (b <= a) b = a + 1;
    if (b > eh) b = eh;
    *y0 = a, *y1 = b;
}

/* P[ex] = the ink pixels of picture columns 0..ex-1 over the rows y0..y1-1 (as the printer sees the picture: turned) */
static uint32_t P[IMG_MAX_DIM + 1];

static uint8_t bits[256]; /* the set bits of each byte */

static void prefix(const img_t *im, int rotate, int ew, int y0, int y1)
{
    if (!bits[255])
        for (int v = 1; v < 256; v++) bits[v] = (uint8_t)(bits[v >> 1] + (v & 1));
    P[0] = 0;
    if (rotate) { /* printer column ex is picture row h-1-ex; printer rows y0..y1-1 are picture columns */
        int b0 = y0 >> 3, b1 = (y1 - 1) >> 3;
        if (b0 == b1) { /* the usual: the columns are in one byte, counted through a table */
            uint8_t m = (uint8_t)((0xff >> (y0 & 7)) & (0xff << (7 - ((y1 - 1) & 7))));
            const uint8_t *r = im->px + (size_t)(im->h - 1) * (size_t)im->stride + b0;
            for (int ex = 0; ex < ew; ex++, r -= im->stride) P[ex + 1] = bits[(uint8_t)~*r & m];
        } else {
            for (int ex = 0; ex < ew; ex++) {
                const uint8_t *r = im->px + (size_t)(im->h - 1 - ex) * (size_t)im->stride;
                uint32_t c = 0;
                for (int sx = y0; sx < y1; sx++) c += !(r[sx >> 3] & (0x80 >> (sx & 7)));
                P[ex + 1] = c;
            }
        }
    } else {
        memset(P + 1, 0, (size_t)ew * sizeof(P[0]));
        for (int sy = y0; sy < y1; sy++) {
            const uint8_t *r = im->px + (size_t)sy * (size_t)im->stride;
            for (int b = 0; b < (ew + 7) / 8; b++) {
                uint8_t v = (uint8_t)~r[b];
                if (!v) continue;
                for (int k = 0; k < 8 && b * 8 + k < ew; k++) P[b * 8 + k + 1] += (v >> (7 - k)) & 1;
            }
        }
    }
    for (int ex = 0; ex < ew; ex++) P[ex + 1] += P[ex];
}

/* O(picture row + printer row): the box of each printer pixel stepped in integers, its ink from the prefix sums. The
   same pixels as counting every box pixel (test_golden: the whole job's bytes) */
void fit_row(const img_t *im, const fit_t *f, int y, uint8_t *out)
{
    int ew = f->rotate ? im->h : im->w, eh = f->rotate ? im->w : im->h, y0, y1;
    src_rows(f, eh, y, &y0, &y1);
    prefix(im, f->rotate, ew, y0, y1);
    memset(out, 0xff, (size_t)f->w / 8);
    int w = f->w, dq = ew / w, dr = ew % w, rows = y1 - y0;
    int q0 = 0, r0 = 0;                     /* x * ew = q0 * w + r0: x0 = q0 */
    int q1 = (ew + w - 1) / w, r1 = (ew + w - 1) % w; /* (x + 1) * ew + w - 1 = q1 * w + r1: x1 = q1 */
    for (int x = 0; x < w; x++) {
        int x0 = q0, x1 = q1;
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > ew) x1 = ew;
        int black = (int)(P[x1] - P[x0]), all = (x1 - x0) * rows;
        if (black * 2 >= all) out[x >> 3] &= (uint8_t)~(0x80 >> (x & 7));
        q0 += dq, r0 += dr;
        if (r0 >= w) r0 -= w, q0++;
        q1 += dq, r1 += dr;
        if (r1 >= w) r1 -= w, q1++;
    }
}

/* fit_row with the boxes' columns from the map */
static void map_row(const img_t *im, const fit_t *f, fit_cache_t *c, int y0, int y1)
{
    int ew = f->rotate ? im->h : im->w, rows = y1 - y0;
    prefix(im, f->rotate, ew, y0, y1);
    memset(c->row, 0xff, (size_t)f->w / 8);
    for (int x = 0; x < f->w; x++) {
        int x0 = c->x0[x], x1 = c->x1[x];
        if ((int)(P[x1] - P[x0]) * 2 >= (x1 - x0) * rows) c->row[x >> 3] &= (uint8_t)~(0x80 >> (x & 7));
    }
}

void fit_row_cached(const img_t *im, const fit_t *f, fit_cache_t *c, int y, uint8_t *out)
{
    int ew = f->rotate ? im->h : im->w, eh = f->rotate ? im->w : im->h, y0, y1;
    size_t n = (size_t)f->w / 8;
    src_rows(f, eh, y, &y0, &y1);
    if (n > sizeof(c->row)) return fit_row(im, f, y, out);
    if (!c->valid) { /* each printer column's box, once a picture: the steps of fit_row */
        int w = f->w, dq = ew / w, dr = ew % w, q0 = 0, r0 = 0, q1 = (ew + w - 1) / w, r1 = (ew + w - 1) % w;
        for (int x = 0; x < w; x++) {
            int x0 = q0, x1 = q1;
            if (x1 <= x0) x1 = x0 + 1;
            if (x1 > ew) x1 = ew;
            c->x0[x] = (uint16_t)x0, c->x1[x] = (uint16_t)x1;
            q0 += dq, r0 += dr;
            if (r0 >= w) r0 -= w, q0++;
            q1 += dq, r1 += dr;
            if (r1 >= w) r1 -= w, q1++;
        }
        c->y0 = -1;
        c->valid = 1;
    }
    if (y0 != c->y0 || y1 != c->y1) map_row(im, f, c, y0, y1), c->y0 = y0, c->y1 = y1;
    memcpy(out, c->row, n);
}
