/* PWG raster (PWG 5102.4, "RaS2"): what AirPrint-class printers take as image/pwg-raster. 8-bit sGray at 600 dpi on A4
   (4960 x 7016), rows PackBits-like encoded with a line repeat, so a white page is a few KB. Rows are encoded as they come,
   one distinct line in memory at a time. SPDX-License-Identifier: MIT */
#ifndef PWG_H
#define PWG_H
#include <stddef.h>
#include <stdint.h>
#include "pcl.h" /* pcl_out_t, pcl_region_t: the byte sink and the 1-bit region of a page */

#define PWG_DPI 600
#define PWG_PAGE_W 4960
#define PWG_PAGE_H 7016
#define PWG_HEADER 1796

/* The most bytes pwg_encode_line makes for n pixels (a lone pixel before a run costs 2: 4 bytes for 3 pixels at worst) */
#define PWG_ENC_MAX(n) ((n) + (n) / 2 + 2)
/* Encodes n 8-bit pixels (dst needs PWG_ENC_MAX(n) bytes); returns the size. Runs 2..128 as n-1, literals 2..128 as
   257-n, a single pixel as a run of 1 (the byte 128 is never made). */
size_t pwg_encode_line(const uint8_t *src, size_t n, uint8_t *dst);
/* Returns the pixels decoded (n expected), < 0 on a damaged line or the byte 128 */
long pwg_decode_line(const uint8_t *src, size_t len, size_t *used, uint8_t *dst, size_t n);

void pwg_job_begin(pcl_out_t *o); /* the sync word */
void pwg_job_end(pcl_out_t *o);   /* nothing: the stream ends with the last page */
/* One A4 page. The region is 1 bit (1 = white) in units of `scale` pixels (1: 600 dpi, 2: a 300 dpi region doubled).
   Returns 0 or < 0 (output error or a row callback's). */
int pwg_page(pcl_out_t *o, const pcl_region_t *r, int scale);
#endif
