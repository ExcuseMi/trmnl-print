/* A 1-bit grayscale PNG writer and reader for the page files, small and with no library: rows are filtered "Up",
   deflate is one fixed-Huffman block of literals and byte runs (a white page with a sketch is a few KB). The reader
   takes what the writer makes (and any PNG of the same kind: stored or fixed blocks, filters none, sub, up); any
   other file is an error and nothing is changed. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef PNGIO_H
#define PNGIO_H
#include <stddef.h>
#include <stdint.h>

/* The most bytes png_encode can need for a w x h page */
size_t png_max_size(int w, int h);
/* px: h rows of `stride` bytes, 1 = white. Returns the file's size in out, or < 0 (out too small) */
long png_encode(const uint8_t *px, int w, int h, int stride, uint8_t *out, size_t cap);
/* Reads a w x h page into px (stride bytes per row; written only on success). 0, or < 0: not a PNG we read, wrong
   size, damaged (checksums), or out of memory */
int png_decode(const uint8_t *in, size_t n, uint8_t *px, int w, int h, int stride);
#endif
