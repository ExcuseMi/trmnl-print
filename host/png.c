/* A 1-bit grayscale PNG of a canvas as the person sees it (turned to its position). SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include "gfx.h"

static void chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
    uint8_t h[8] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n, (uint8_t)type[0],
        (uint8_t)type[1], (uint8_t)type[2], (uint8_t)type[3] };
    fwrite(h, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uLong c = crc32(0, h + 4, 4);
    c = crc32(c, d, n);
    uint8_t t[4] = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
    fwrite(t, 1, 4, f);
}

int png_write(const char *path, const gfx_t *canvas)
{
    gfx_t sc = *canvas, *g = &sc;
    gfx_screen(g); /* the whole screen, the hint strip too */
    int W = g->W, H = g->H, rb = (W + 7) / 8;
    uLong raw_n = (uLong)(rb + 1) * (uLong)H, z_n = compressBound(raw_n);
    uint8_t *raw = calloc(raw_n, 1), *z = malloc(z_n);
    if (!raw || !z) return free(raw), free(z), -1;
    for (int y = 0; y < H; y++) {
        uint8_t *r = raw + (size_t)y * (size_t)(rb + 1) + 1;
        for (int x = 0; x < W; x++)
            if (gfx_get(g, x, y)) r[x >> 3] |= (uint8_t)(0x80 >> (x & 7));
    }
    if (compress2(z, &z_n, raw, raw_n, 9) != Z_OK) return free(raw), free(z), -1;
    FILE *f = fopen(path, "wb");
    if (!f) return free(raw), free(z), -1;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ih[13] = { (uint8_t)(W >> 24), (uint8_t)(W >> 16), (uint8_t)(W >> 8), (uint8_t)W, (uint8_t)(H >> 24),
        (uint8_t)(H >> 16), (uint8_t)(H >> 8), (uint8_t)H, 1, 0, 0, 0, 0 };
    chunk(f, "IHDR", ih, 13);
    chunk(f, "IDAT", z, (uint32_t)z_n);
    chunk(f, "IEND", 0, 0);
    free(raw);
    free(z);
    return fclose(f) ? -1 : 0;
}
