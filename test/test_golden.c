/* The printed bytes stay the same when the scaling or the raster loop is made faster: the whole posted job (IPP header,
   PWG or PCL raster) of busy pictures, turned and not, enlarged and shrunk, hashed against the bytes the first, plain
   per-pixel box filter made (2026-11-03). SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "pwg.h"
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

static uint8_t *png_encode_test(const uint8_t *px, int w, int h, int st, size_t *n)
{
    uint8_t *f = malloc(png_max_size(w, h));
    long k = png_encode(px, w, h, st, f, png_max_size(w, h));
    *n = k > 0 ? (size_t)k : 0;
    return f;
}

/* the same job a few rows a step: the same bytes, one post at the last step */
static void stepped(int fmt, int w, int h, int rows)
{
    int st;
    uint8_t *px = busy(w, h, &st);
    size_t n;
    uint8_t *png = png_encode_test(px, w, h, st, &n);
    printer_t a, b;
    printer_init(&a), printer_init(&b);
    pj_net_t na = { fmt, fake_post, &a }, nb = { fmt, fake_post, &b };
    pj_result_t ra, rb;
    CHECK(pj_print_png(&na, tp("192.0.2.7"), png, n, &ra) == 0);
    pj_job_t *j = pj_png_start(&nb, tp("192.0.2.7"), png, n, &rb);
    CHECK(j);
    int steps = 0, rc = -1;
    while (j && (rc = pj_job_step(j, rows)) > 0) {
        steps++;
        CHECK(b.posts == 0);
    }
    CHECK(rc == 0 && b.posts == 1 && rb.pages == 1 && !strcmp(ra.msg, rb.msg));
    CHECK(steps >= (fmt == PJ_PWG ? PWG_PAGE_H : 1) / rows - 1);
    CHECK(a.len[0] == b.len[0] && !memcmp(a.body[0], b.body[0], a.len[0]));
    CHECK(pj_job_step(j, rows) == 0 && b.posts == 1); /* a step after the end sends nothing again */
    pj_job_free(j);
    printer_free(&a), printer_free(&b);
    free(png), free(px);
}

/* the ways a stepped job fails: as the whole call says them; given up half way, nothing is posted or kept (ASan) */
static void stepped_fails(void)
{
    int st;
    uint8_t *px = busy(1872, 1404, &st);
    size_t n;
    uint8_t *png = png_encode_test(px, 1872, 1404, st, &n);
    printer_t p;
    printer_init(&p);
    pj_net_t net = { PJ_PWG, fake_post, &p };
    pj_result_t r;
    CHECK(!pj_png_start(&net, tp("192.0.2.7"), png, 20, &r) && strstr(r.msg, "Not a PNG"));
    pj_job_t *j = pj_png_start(&net, tp("192.0.2.7"), png, n, &r);
    for (int k = 0; j && k < 5; k++) CHECK(pj_job_step(j, 100) == 1);
    pj_job_free(j); /* left half way */
    CHECK(p.posts == 0);
    pj_job_bytes = 200 * 1024; /* the busy page does not fit: the X's words */
    j = pj_png_start(&net, tp("192.0.2.7"), png, n, &r);
    int rc = -1;
    while (j && (rc = pj_job_step(j, 300)) > 0) {}
    CHECK(rc < 0 && p.posts == 0 && strstr(r.msg, "too big"));
    pj_job_free(j);
    pj_job_bytes = 64u << 20;
    p.status = PJ_NET_TIMEOUT; /* the post fails at the last step */
    j = pj_png_start(&net, tp("192.0.2.7"), png, n, &r);
    while (j && (rc = pj_job_step(j, 3000)) > 0) {}
    CHECK(rc < 0 && p.posts == 1 && strstr(r.msg, "timeout"));
    pj_job_free(j);
    printer_free(&p);
    free(png), free(px);
}

/* the kept row is fit_row's row for every row, turned or not, enlarged or shrunk */
static void cached_rows(void)
{
    static const int sz[][2] = { { 1872, 1404 }, { 1404, 1872 }, { 3000, 2600 }, { 333, 77 }, { 64, 4000 } };
    static uint8_t a[IMG_ROW_BYTES], b[IMG_ROW_BYTES];
    static fit_cache_t c;
    for (int i = 0; i < 5; i++) {
        int st, bad = 0;
        uint8_t *px = busy(sz[i][0], sz[i][1], &st);
        img_t im = { px, sz[i][0], sz[i][1], st };
        fit_t f;
        fit_compute(im.w, im.h, i & 1 ? PCL_PAGE_W : 4960, i & 1 ? PCL_PAGE_H : 7016, &f);
        c.valid = 0;
        for (int y = 0; y < f.h; y++) fit_row(&im, &f, y, a), fit_row_cached(&im, &f, &c, y, b), bad += !!memcmp(a, b, (size_t)f.w / 8);
        CHECK(bad == 0);
        free(px);
    }
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
    if (argc > 1) return 0;
    stepped(PJ_PWG, 1872, 1404, 256);
    stepped(PJ_PCL, 1872, 1404, 97);
    stepped(PJ_PWG, 1404, 1872, 1000);
    stepped_fails();
    cached_rows();
    return CHECK_DONE("golden");
}
