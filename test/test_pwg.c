/* PWG raster: exact header and tiny page bytes, the row coding with repeats, a decoded page against its source, failures. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "pwg.h"

typedef struct {
    uint8_t *b;
    size_t n, cap;
    long fail_after;
} sink_t;

static int sink_write(void *u, const void *d, size_t n)
{
    sink_t *s = u;
    if (s->fail_after >= 0 && s->n + n > (size_t)s->fail_after) return -5;
    if (s->n + n > s->cap) s->b = realloc(s->b, s->cap = (s->n + n) * 2);
    memcpy(s->b + s->n, d, n);
    s->n += n;
    return 0;
}

static uint32_t be(const uint8_t *p, int off) { return (uint32_t)p[off] << 24 | (uint32_t)p[off + 1] << 16 | (uint32_t)p[off + 2] << 8 | p[off + 3]; }

static int rows2(void *u, int y, uint8_t *row)
{
    (void)u;
    row[0] = y == 0 ? 0x0f : 0xff;
    return 0;
}

static void put_run(uint8_t *d, size_t *o, int n, uint8_t px)
{
    while (n > 0) {
        int k = n > 128 ? 128 : n;
        d[(*o)++] = (uint8_t)(k - 1), d[(*o)++] = px;
        n -= k;
    }
}

static void exact_bytes(void)
{
    sink_t s = { 0, 0, 0, -1 };
    pcl_out_t o = { sink_write, &s, 0, 0 };
    pcl_region_t r = { 8, 3, 8, 2, rows2, 0 };
    pwg_job_begin(&o);
    CHECK(pwg_page(&o, &r, 1) == 0);
    pwg_job_end(&o);
    CHECK(!memcmp(s.b, "RaS2", 4));
    const uint8_t *h = s.b + 4;
    /* the header, field by field (PWG 5102.4 table 2; offsets as cups_page_header2_t); all else zero */
    uint8_t want[PWG_HEADER];
    memset(want, 0, sizeof(want));
    memcpy(want, "PwgRaster", 10);
    struct { int off; uint32_t v; } f[] = { { 276, 600 }, { 280, 600 }, { 340, 1 }, { 352, 595 }, { 356, 842 }, { 372, 4960 }, { 376, 7016 },
        { 384, 8 }, { 388, 8 }, { 392, 4960 }, { 400, 18 }, { 420, 1 }, { 456, 1 }, { 460, 1 }, { 472, 4960 }, { 476, 7016 }, { 480, 0xffffff } };
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        want[f[i].off] = (uint8_t)(f[i].v >> 24), want[f[i].off + 1] = (uint8_t)(f[i].v >> 16);
        want[f[i].off + 2] = (uint8_t)(f[i].v >> 8), want[f[i].off + 3] = (uint8_t)f[i].v;
    }
    CHECK(!memcmp(h, want, PWG_HEADER));
    CHECK(be(h, 372) == 4960 && be(h, 376) == 7016 && be(h, 400) == 18);
    /* the rows, built by hand: 3 white lines, the marked line (8 white, 4 ink, then white to the end), white again in groups of 256 lines */
    uint8_t exp[40000];
    size_t e = 0;
    exp[e++] = 2;                                    /* white line, 3 times */
    put_run(exp, &e, 4960, 0xff);
    exp[e++] = 0;                                    /* row 3 */
    put_run(exp, &e, 8, 0xff), put_run(exp, &e, 4, 0x00), put_run(exp, &e, 4948, 0xff);
    for (int left = 7012; left > 0; left -= 256) {   /* rows 4 .. 7015 */
        int k = left > 256 ? 256 : left;
        exp[e++] = (uint8_t)(k - 1);
        put_run(exp, &e, 4960, 0xff);
    }
    CHECK(s.n == 4 + PWG_HEADER + e);
    CHECK(s.n == 4 + PWG_HEADER + e && !memcmp(s.b + 4 + PWG_HEADER, exp, e));
    CHECK(o.bytes == s.n);
    free(s.b);
}

static void rle(void)
{
    srand(3);
    for (int round = 0; round < 600; round++) {
        size_t n = 1 + (size_t)(rand() % 700);
        uint8_t in[700], enc[PWG_ENC_MAX(700)], dec[700];
        for (size_t i = 0; i < n; i++)
            in[i] = (uint8_t)(round % 4 == 0 ? rand() : round % 4 == 1 ? (int)((i / 37) & 1) * 255 : round % 4 == 2 ? 255 : (rand() % 3 ? 0 : rand()));
        size_t m = pwg_encode_line(in, n, enc);
        CHECK(m <= PWG_ENC_MAX(n));
        size_t used = 0;
        CHECK(pwg_decode_line(enc, m, &used, dec, n) == (long)n);
        CHECK(used == m && !memcmp(in, dec, n));
        for (size_t i = 0; i < m;) {                  /* never the byte 128; steps follow the count byte */
            CHECK(enc[i] != 128);
            i += enc[i] < 128 ? 2u : 1u + (257u - enc[i]);
        }
    }
    uint8_t one = 7, e2[4], white[300];
    memset(white, 255, sizeof(white));
    CHECK(pwg_encode_line(&one, 1, e2) == 2 && e2[0] == 0 && e2[1] == 7);
    uint8_t e3[8];
    CHECK(pwg_encode_line(white, 128, e3) == 2 && e3[0] == 127 && e3[1] == 255);
    const uint8_t two[] = { 1, 2 };
    CHECK(pwg_encode_line(two, 2, e3) == 3 && e3[0] == 255 && e3[1] == 1 && e3[2] == 2);   /* literal of 2: 257 - 2 */
    /* damaged lines */
    uint8_t d[10];
    size_t u;
    const uint8_t b128[] = { 128, 1 }, cut_run[] = { 4 }, cut_lit[] = { 250, 1, 2 }, over[] = { 20, 0 };
    CHECK(pwg_decode_line(b128, 2, &u, d, 10) < 0);
    uint8_t b128long[130];
    memset(b128long, 7, sizeof(b128long)), b128long[0] = 128;
    uint8_t big[129];
    CHECK(pwg_decode_line(b128long, sizeof(b128long), &u, big, 129) < 0);   /* 128 is no literal of 129 */
    CHECK(pwg_decode_line(cut_run, 1, &u, d, 10) < 0);
    CHECK(pwg_decode_line(cut_lit, 3, &u, d, 10) < 0);
    CHECK(pwg_decode_line(over, 2, &u, d, 10) < 0);
}

/* Decodes a whole page stream into 8-bit rows and compares it with the region's pixels */
static int page_matches(const uint8_t *b, size_t n, const pcl_region_t *r, int scale)
{
    static uint8_t line[PWG_PAGE_W], src[PWG_PAGE_W / 8];
    if (n < 4 + PWG_HEADER || memcmp(b, "RaS2", 4)) return 0;
    size_t pos = 4 + PWG_HEADER;
    int y = 0, ok = 1;
    while (y < PWG_PAGE_H) {
        if (pos >= n) return 0;
        int rep = b[pos++] + 1;
        size_t used;
        if (pwg_decode_line(b + pos, n - pos, &used, line, PWG_PAGE_W) < 0) return fprintf(stderr, "decode fail y=%d\n", y), 0;
        pos += used;
        for (int k = 0; k < rep && y < PWG_PAGE_H; k++, y++) {
            for (int x = 0; x < PWG_PAGE_W; x++) {
                int inside = y >= r->y * scale && y < (r->y + r->h) * scale && x >= r->x * scale && x < (r->x + r->w) * scale;
                int want = 255;
                if (inside) {
                    r->get_row(r->u, (y - r->y * scale) / scale, src);
                    int px = (x - r->x * scale) / scale;
                    want = src[px >> 3] & (0x80 >> (px & 7)) ? 255 : 0;
                }
                if (line[x] != want && ok) { fprintf(stderr, "diff y=%d x=%d got %d want %d\n", y, x, line[x], want); ok = 0; }
            }
        }
    }
    if (pos != n) fprintf(stderr, "pos %zu n %zu\n", pos, n);
    return ok && pos == n;
}

static int busy_row(void *u, int y, uint8_t *row)
{
    (void)u;
    for (int i = 0; i < 40; i++) row[i] = (uint8_t)((y / 5 % 3 == 0) ? 0xff : (y / 5 % 3 == 1) ? 0x3c : (i * 31 + y / 10) & 0xff);
    return 0;
}

static void whole_page(void)
{
    pcl_region_t r1 = { 100, 50, 320, 200, busy_row, 0 };
    for (int scale = 1; scale <= 2; scale++) {
        sink_t s = { 0, 0, 0, -1 };
        pcl_out_t o = { sink_write, &s, 0, 0 };
        pcl_region_t r = r1;
        if (scale == 2) r.x = 200, r.y = 120;
        pwg_job_begin(&o);
        CHECK(pwg_page(&o, &r, scale) == 0);
        CHECK(page_matches(s.b, s.n, &r, scale));
        CHECK(s.n < 400000);
        free(s.b);
    }
}

static int abort_row(void *u, int y, uint8_t *row) { (void)u, (void)row; return y == 3 ? -9 : 0; }

static void failures(void)
{
    for (long cut = 0; cut < 3000; cut += 701) {
        sink_t s = { 0, 0, 0, cut };
        pcl_out_t o = { sink_write, &s, 0, 0 };
        pcl_region_t r = { 0, 0, 8, 10, rows2, 0 };
        pwg_job_begin(&o);
        CHECK(pwg_page(&o, &r, 1) < 0 && o.err == -5);
        free(s.b);
    }
    sink_t s = { 0, 0, 0, -1 };
    pcl_out_t o = { sink_write, &s, 0, 0 };
    pcl_region_t a = { 0, 0, 8, 10, abort_row, 0 };
    CHECK(pwg_page(&o, &a, 1) == -9);
    pcl_region_t bad[] = { { 0, 0, 0, 5, rows2, 0 }, { 0, 0, 8, 0, rows2, 0 }, { 4960, 0, 8, 5, rows2, 0 }, { 0, 7015, 8, 2, rows2, 0 }, { -1, 0, 8, 5, rows2, 0 }, { 2401, 0, 80, 5, rows2, 0 } };
    size_t before = s.n;
    for (int i = 0; i < 5; i++) CHECK(pwg_page(&o, &bad[i], 1) < 0);
    CHECK(pwg_page(&o, &bad[5], 2) < 0);              /* doubled it runs off the page */
    CHECK(pwg_page(&o, &a, 3) < 0);
    CHECK(s.n == before);                              /* a refused page writes nothing */
    free(s.b);
}

int main(void)
{
    exact_bytes();
    rle();
    whole_page();
    failures();
    return CHECK_DONE("test_pwg");
}
