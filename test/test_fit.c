/* A4 fit and centring of a picture, the rows it makes, the PNG header read. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "pcl.h"
#include "util.h"

static void inside_page(const fit_t *f)
{
    CHECK(f->x >= IMG_MARGIN - 8 && f->y >= IMG_MARGIN);
    CHECK(f->x + f->w <= PCL_PAGE_W - IMG_MARGIN + 8 && f->y + f->h <= PCL_PAGE_H - IMG_MARGIN);
    CHECK(f->w % 8 == 0);
    CHECK(abs(f->x - (PCL_PAGE_W - f->x - f->w)) <= 1);   /* centred sideways */
    CHECK(abs(f->y - (PCL_PAGE_H - f->y - f->h)) <= 1);   /* and up and down */
}

static void sizes(void)
{
    fit_t f;
    /* Sketch's page 1872 x 1404 is wide: turned, it fills the width of the area (2280 px) */
    CHECK(fit_compute(1872, 1404, PCL_PAGE_W, PCL_PAGE_H, &f) == 0);
    CHECK(f.rotate == 1);
    CHECK(f.w == 2280);
    CHECK(f.h == 3040);              /* 1872 * 2280 / 1404 */
    inside_page(&f);
    /* a tall picture is not turned, and is limited by the height */
    CHECK(fit_compute(1000, 3000, PCL_PAGE_W, PCL_PAGE_H, &f) == 0);
    CHECK(f.rotate == 0 && f.h == 3308 && f.w == 1102 / 8 * 8);
    inside_page(&f);
    /* a square one is limited by the width */
    CHECK(fit_compute(500, 500, PCL_PAGE_W, PCL_PAGE_H, &f) == 0);
    CHECK(f.rotate == 0 && f.w == 2280 && f.h == 2280);
    inside_page(&f);
    /* aspect kept for odd sizes */
    CHECK(fit_compute(331, 977, PCL_PAGE_W, PCL_PAGE_H, &f) == 0);
    CHECK(abs((int)((long)f.w * 977 - (long)f.h * 331)) < 977 * 8);
    inside_page(&f);
    CHECK(fit_compute(1, 1, PCL_PAGE_W, PCL_PAGE_H, &f) == 0);
    inside_page(&f);
    CHECK(fit_compute(1, 4096, PCL_PAGE_W, PCL_PAGE_H, &f) == 0 && f.w >= 8);
    CHECK(fit_compute(0, 5, PCL_PAGE_W, PCL_PAGE_H, &f) < 0);
    CHECK(fit_compute(5, 0, PCL_PAGE_W, PCL_PAGE_H, &f) < 0);
    CHECK(fit_compute(IMG_MAX_DIM + 1, 5, PCL_PAGE_W, PCL_PAGE_H, &f) < 0);
}

static void corner_dot(uint8_t *px, int stride, int w, int h)
{
    (void)w, (void)h;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) set_ink(px, stride, x, y); /* top-left 8x8 block */
}

static int ink_at(const uint8_t *row, int x) { return !(row[x >> 3] & (0x80 >> (x & 7))); }

static void rows(void)
{
    /* a 160 x 80 picture with a top-left block, wide: turned clockwise, the top-left goes to the top RIGHT */
    int w = 160, h = 80, stride = 20;
    uint8_t px[20 * 80];
    memset(px, 0xff, sizeof(px));
    corner_dot(px, stride, w, h);
    img_t im = { px, w, h, stride };
    fit_t f;
    fit_compute(w, h, PCL_PAGE_W, PCL_PAGE_H, &f);
    CHECK(f.rotate);
    uint8_t row[PCL_PAGE_W / 8];
    fit_row(&im, &f, 0, row);
    CHECK(ink_at(row, f.w - 1) && !ink_at(row, 0) && !ink_at(row, f.w / 2));
    fit_row(&im, &f, f.h - 1, row);
    CHECK(!ink_at(row, f.w - 1));          /* the bottom row is white at that side */
    /* not wide, not turned: top-left stays top-left */
    uint8_t px2[20 * 160];
    memset(px2, 0xff, sizeof(px2));
    corner_dot(px2, 20, 80, 160);
    img_t im2 = { px2, 80, 160, 20 };
    fit_compute(80, 160, PCL_PAGE_W, PCL_PAGE_H, &f);
    CHECK(!f.rotate);
    fit_row(&im2, &f, 0, row);
    CHECK(ink_at(row, 0) && !ink_at(row, f.w - 1));
    /* all-white picture: every row white; solid black: every row ink */
    memset(px2, 0xff, sizeof(px2));
    int white = 1;
    for (int y = 0; y < f.h; y += 97) {
        fit_row(&im2, &f, y, row);
        for (int x = 0; x < f.w; x++) white &= !ink_at(row, x);
    }
    CHECK(white);
    memset(px2, 0x00, sizeof(px2));
    int black = 1;
    for (int y = 0; y < f.h; y += 97) {
        fit_row(&im2, &f, y, row);
        for (int x = 0; x < f.w; x++) black &= ink_at(row, x);
    }
    CHECK(black);
}

static void thin_line_survives_downscale(void)
{
    /* a 2 px high line in a 100 x 4000 picture (not turned) shrunk to 3308 rows: kept, in one or two rows */
    int w = 100, h = 4000, stride = (w + 7) / 8;
    uint8_t *px = malloc((size_t)stride * (size_t)h);
    memset(px, 0xff, (size_t)stride * (size_t)h);
    for (int x = 0; x < w; x++) set_ink(px, stride, x, 2000), set_ink(px, stride, x, 2001);
    img_t im = { px, w, h, stride };
    fit_t f;
    fit_compute(w, h, PCL_PAGE_W, PCL_PAGE_H, &f);
    CHECK(!f.rotate && f.h == 3308);
    uint8_t row[PCL_PAGE_W / 8];
    int inks = 0;
    for (int y = 0; y < f.h; y++) {
        fit_row(&im, &f, y, row);
        inks += ink_at(row, 3);
    }
    CHECK(inks >= 1 && inks <= 3);
    free(px);
}

static void dims(void)
{
    size_t n;
    uint8_t *p = make_png(123, 45, 0, &n);
    int w = 0, h = 0;
    CHECK(png_dims(p, n, &w, &h) == 0 && w == 123 && h == 45);
    CHECK(png_dims(p, 20, &w, &h) < 0);          /* cut short */
    p[1] = 'X';
    CHECK(png_dims(p, n, &w, &h) < 0);           /* not a PNG */
    p[1] = 'P';
    p[16] = 0x10;                                 /* a width of 268 million */
    CHECK(png_dims(p, n, &w, &h) < 0);
    free(p);
}

int main(void)
{
    sizes();
    rows();
    thin_line_survives_downscale();
    dims();
    return CHECK_DONE("test_fit");
}
