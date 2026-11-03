/* A 1-bit picture scaled to fit an A4 page (300 dpi) with its aspect kept, centred, a landscape picture turned to
   fill the portrait page. Rows are made one at a time from the picture in memory. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef IMGFIT_H
#define IMGFIT_H
#include <stdint.h>
#include <stddef.h>

#define IMG_MARGIN 100      /* paper px (8.5 mm) kept free on each side: the printers' unprintable edge is ~4 mm */
#define IMG_MAX_DIM 4096
#define IMG_MAX_BYTES (1024 * 1024)

typedef struct {
    int rotate;   /* 1: the picture is turned 90 degrees clockwise */
    int x, y;     /* the printed region's top-left on the paper */
    int w, h;     /* its size; w is a multiple of 8 */
} fit_t;

typedef struct {
    const uint8_t *px; /* 1 = white, MSB leftmost */
    int w, h, stride;
} img_t;

/* 0, or < 0 for a size that cannot be printed (empty, over IMG_MAX_DIM) */
int fit_compute(int sw, int sh, int page_w, int page_h, fit_t *f);
/* Row y (0..f->h-1) of the printed region into out (f->w / 8 bytes, 1 = white). A destination pixel is ink when at
   least half of the picture pixels it covers are ink. */
void fit_row(const img_t *im, const fit_t *f, int y, uint8_t *out);
/* fit_row with the last row kept: a printer row of the same picture rows as the row before is a copy (Sketch's page
   enlarged to A4 at 600 dpi repeats each row about 3 times). Zero `valid` before the first row of a picture. */
#define IMG_ROW_BYTES 620 /* a PWG A4 row (4960 px) */
typedef struct {
    int valid, y0, y1;
    uint8_t row[IMG_ROW_BYTES];
    uint16_t x0[IMG_ROW_BYTES * 8], x1[IMG_ROW_BYTES * 8]; /* each printer column's picture columns */
} fit_cache_t;
void fit_row_cached(const img_t *im, const fit_t *f, fit_cache_t *c, int y, uint8_t *out);
/* The size of a PNG from its header; 0, or < 0 if it is not a PNG */
int png_dims(const uint8_t *in, size_t n, int *w, int *h);
#endif
