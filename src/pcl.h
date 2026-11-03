/* PCL5 raster output for an A4 page at 300 dpi, 1 bit: the byte stream a laser printer takes on port 9100 or in an
   IPP job. Rows come from a callback, one at a time, so a page never sits in memory whole. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef PCL_H
#define PCL_H
#include <stddef.h>
#include <stdint.h>

#define PCL_DPI 300
#define PCL_PAGE_W 2480 /* A4, 210 mm */
#define PCL_PAGE_H 3508 /* 297 mm */
/* The logical page of an A4 portrait page in PCL (HP LaserJet 4 class, which Brother's PCL emulation follows): the
   cursor's 0,0 is this far inside the paper's corner. Unproven for the Brother until a page is printed. */
#define PCL_OFF_X 71
#define PCL_OFF_Y 50

/* Where the bytes go: 0 on success, < 0 on failure (the first failure stops all later writes) */
typedef int (*pcl_write_fn)(void *u, const void *buf, size_t n);
typedef struct {
    pcl_write_fn write;
    void *u;
    int err;
    size_t bytes;
} pcl_out_t;

/* PackBits (PCL compression mode 2). dst needs n + n / 128 + 1 bytes; returns the encoded size */
size_t packbits_encode(const uint8_t *src, size_t n, uint8_t *dst);
/* Returns the decoded size, or < 0 if the input is damaged or does not fit in cap */
long packbits_decode(const uint8_t *src, size_t n, uint8_t *dst, size_t cap);

/* A raster region in paper pixels: x, w multiples of 8 are not needed for x; w is rounded down to 8 by the caller.
   get_row fills `w / 8` bytes for row y (0..h-1) with 1 = white, 0 = ink; returns < 0 to abort. */
typedef int (*pcl_row_fn)(void *u, int y, uint8_t *row);
typedef struct {
    int x, y, w, h;
    pcl_row_fn get_row;
    void *u;
} pcl_region_t;

void pcl_job_begin(pcl_out_t *o); /* reset, A4, portrait, 300 dpi units */
void pcl_job_end(pcl_out_t *o);   /* reset */
/* One page: the region as raster graphics, then a form feed. Returns 0 or < 0 (output error or a row callback's) */
int pcl_page(pcl_out_t *o, const pcl_region_t *r);
/* The same page a few rows at a time: begin writes the raster start, each step up to `rows` rows (1 while rows are
   left, 0 after the page's end, < 0 on an error) */
typedef struct {
    pcl_out_t *o;
    pcl_region_t r;
    int j;
} pcl_run_t;
int pcl_page_begin(pcl_run_t *p, pcl_out_t *o, const pcl_region_t *r);
int pcl_page_step(pcl_run_t *p, int rows);
#endif
