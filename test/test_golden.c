/* The printed bytes stay the same when the scaling or the raster loop is made faster: the whole posted job (IPP header,
   PWG or PCL raster) of busy pictures, turned and not, enlarged and shrunk, hashed against the bytes the first, plain
   per-pixel box filter made (2026-11-03). SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "util.h"

static uint64_t fnv(const uint8_t *b, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull;
    return h;
}

/* a busy picture: noise, a grey dither, thin lines, rings and solid blocks */
static uint8_t *busy(int w, int h, int *stride)
{
    int st = (w + 7) / 8;
    uint8_t *px = malloc((size_t)st * (size_t)h);
    memset(px, 0xff, (size_t)st * (size_t)h);
    uint32_t r = 12345;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            r = r * 1103515245u + 12345u;
            int ink = 0;
            if (y < h / 4) ink = (r >> 16) % 3 == 0;                           /* noise */
            else if (y < h / 2) ink = ((x ^ y) & 1) || ((x & 3) == 0 && (y & 1)); /* dither */
            else if (x % 37 == 0 || y % 41 == 0 || (x + y) % 53 == 0) ink = 1;  /* lines */
            else {
                int dx = x - w / 2, dy = y - 3 * h / 4, d = dx * dx + dy * dy;
                ink = (d / 400) % 3 == 0 || (x > w - w / 8 && y > h - h / 8);    /* rings, a block */
            }
            if (ink) set_ink(px, st, x, y);
        }
    *stride = st;
    return px;
}

static uint64_t job(int fmt, int w, int h)
{
    int st;
    uint8_t *px = busy(w, h, &st);
    printer_t pr;
    printer_init(&pr);
    pj_net_t net = { fmt, fake_post, &pr };
    pj_result_t r;
    uint64_t sum = 0;
    pj_job_bytes = 64u << 20; /* a busy page at 600 dpi is more than the X's job buffer: the bytes are what is compared */
    if (pj_print_bitmap(&net, tp("192.0.2.7"), px, w, h, st, &r) == 0 && pr.posts == 1) sum = fnv(pr.body[0], pr.len[0]);
    else fprintf(stderr, "job %d %dx%d: %s\n", fmt, w, h, r.msg);
    printer_free(&pr);
    free(px);
    return sum;
}

int main(int argc, char **argv)
{
    static const struct { int fmt, w, h; uint64_t sum; } c[] = {
        { PJ_PWG, 1872, 1404, 0x0faa341993c67effull }, /* Sketch's page on the X: turned, enlarged 3.4 times */
        { PJ_PWG, 1404, 1872, 0x5797105ccc332776ull }, /* the X in portrait: not turned */
        { PJ_PCL, 1872, 1404, 0x203c2f0cf51912f5ull },
        { PJ_PCL, 3000, 2600, 0xcba76134ef174c2bull }, /* shrunk: a printer pixel covers 1 or 2 picture pixels */
        { PJ_PWG, 333, 77, 0xac20504baf2abef1ull },    /* enlarged many times, odd sizes */
    };
    for (size_t i = 0; i < sizeof(c) / sizeof(c[0]); i++) {
        uint64_t s = job(c[i].fmt, c[i].w, c[i].h);
        if (argc > 1) printf("%d %dx%d 0x%016llxull\n", c[i].fmt, c[i].w, c[i].h, (unsigned long long)s);
        else CHECK(s == c[i].sum);
    }
    (void)argv;
    return CHECK_DONE("golden");
}
