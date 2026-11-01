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
    int aw = pw - 2 * IMG_MARGIN, ah = ph - 2 * IMG_MARGIN, dw, dh;
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

static int ink(const img_t *im, int rotate, int x, int y)
{
    int sx = rotate ? y : x, sy = rotate ? im->h - 1 - x : y;
    return !(im->px[(size_t)sy * (size_t)im->stride + (size_t)(sx >> 3)] & (0x80 >> (sx & 7)));
}

void fit_row(const img_t *im, const fit_t *f, int y, uint8_t *out)
{
    int ew = f->rotate ? im->h : im->w, eh = f->rotate ? im->w : im->h;
    int y0 = (int)((long)y * eh / f->h), y1 = (int)(((long)(y + 1) * eh + f->h - 1) / f->h);
    if (y1 <= y0) y1 = y0 + 1;
    if (y1 > eh) y1 = eh;
    memset(out, 0xff, (size_t)f->w / 8);
    for (int x = 0; x < f->w; x++) {
        int x0 = (int)((long)x * ew / f->w), x1 = (int)(((long)(x + 1) * ew + f->w - 1) / f->w);
        if (x1 <= x0) x1 = x0 + 1;
        if (x1 > ew) x1 = ew;
        int black = 0, all = (x1 - x0) * (y1 - y0);
        for (int sy = y0; sy < y1; sy++)
            for (int sx = x0; sx < x1; sx++) black += ink(im, f->rotate, sx, sy);
        if (black * 2 >= all) out[x >> 3] &= (uint8_t)~(0x80 >> (x & 7));
    }
}
