/* PCL byte stream: exact bytes for a tiny bitmap, PackBits round trip, failures of the output. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "pcl.h"

typedef struct {
    uint8_t b[4096];
    size_t n;
    int fail_after; /* bytes accepted before the sink fails; -1 never */
    int calls;
} sink_t;

static int sink_write(void *u, const void *d, size_t n)
{
    sink_t *s = u;
    s->calls++;
    if (s->fail_after >= 0 && s->n + n > (size_t)s->fail_after) return -5;
    memcpy(s->b + s->n, d, n);
    s->n += n;
    return 0;
}

static int rows2(void *u, int y, uint8_t *row)
{
    (void)u;
    row[0] = y == 0 ? 0x0f : 0xff; /* row 0: left half ink; row 1: all white */
    return 0;
}

static void exact_bytes(void)
{
    sink_t s = { .fail_after = -1 };
    pcl_out_t o = { sink_write, &s, 0, 0 };
    pcl_region_t r = { 100, 100, 8, 2, rows2, 0 };
    pcl_job_begin(&o);
    CHECK(pcl_page(&o, &r) == 0);
    pcl_job_end(&o);
    static const char want[] = "\x1b" "E" "\x1b&u300D" "\x1b&l26A" "\x1b&l0O"
                               "\x1b*p29X" "\x1b*p50Y" "\x1b*t300R" "\x1b*r1A" "\x1b*b2M"
                               "\x1b*b2W" "\x00\xf0" "\x1b*b2W" "\x00\x00" "\x1b*rB" "\f" "\x1b" "E";
    CHECK(s.n == sizeof(want) - 1);
    CHECK(s.n == o.bytes);
    CHECK(s.n == sizeof(want) - 1 && !memcmp(s.b, want, sizeof(want) - 1));
}

static void packbits_roundtrip(void)
{
    srand(7);
    for (int round = 0; round < 400; round++) {
        size_t n = (size_t)(rand() % 700);
        uint8_t in[700], enc[700 + 6], dec[700];
        for (size_t i = 0; i < n; i++) in[i] = (uint8_t)(round % 3 == 0 ? rand() : round % 3 == 1 ? (i / 37) & 1 ? 0xff : 0 : rand() % 3 ? 0 : rand());
        size_t m = packbits_encode(in, n, enc);
        CHECK(m <= n + n / 128 + 1);
        long k = packbits_decode(enc, m, dec, sizeof(dec));
        CHECK(k == (long)n);
        CHECK(k == (long)n && !memcmp(in, dec, n));
    }
    for (size_t n = 120; n < 520; n++) { /* runs across the 128 limit, exactly as the standard cuts them */
        uint8_t *in = calloc(n, 1), *enc = malloc(n + n / 128 + 2), *dec = malloc(n);
        size_t m = packbits_encode(in, n, enc);
        CHECK(m == 2 * ((n + 127) / 128));
        CHECK(packbits_decode(enc, m, dec, n) == (long)n && !memcmp(in, dec, n));
        for (size_t i = 0; i < m; i += 2) CHECK((int8_t)enc[i] >= -127);   /* -128 is no run: the standard keeps it free */
        free(in), free(enc), free(dec);
    }
    uint8_t white[310] = { 0 }, enc[320];
    CHECK(packbits_encode(white, sizeof(white), enc) == 6); /* 128 + 128 + 54 as three runs */
    uint8_t one = 9, e1[4];
    CHECK(packbits_encode(&one, 1, e1) == 2 && e1[0] == 0 && e1[1] == 9);
    CHECK(packbits_encode(&one, 0, e1) == 0);
}

static void packbits_damaged(void)
{
    uint8_t out[8];
    const uint8_t cut_literal[] = { 5, 1, 2 };       /* promises 6 bytes, has 2 */
    const uint8_t cut_run[] = { 0xfd };              /* a run without its byte */
    const uint8_t too_long[] = { 0xe0, 7 };          /* 33 bytes into 8 */
    CHECK(packbits_decode(cut_literal, 3, out, sizeof(out)) < 0);
    CHECK(packbits_decode(cut_run, 1, out, sizeof(out)) < 0);
    CHECK(packbits_decode(too_long, 2, out, sizeof(out)) < 0);
}

static void failures(void)
{
    uint8_t row[4];
    (void)row;
    for (int cut = 0; cut < 60; cut += 7) { /* the sink dies somewhere in the page: the error comes back, nothing more is written */
        sink_t s = { .fail_after = cut };
        pcl_out_t o = { sink_write, &s, 0, 0 };
        pcl_region_t r = { 100, 100, 8, 40, rows2, 0 };
        pcl_job_begin(&o);
        int rc = pcl_page(&o, &r);
        CHECK(rc < 0);
        CHECK(o.err == -5);
        int calls = s.calls;
        pcl_job_end(&o);
        CHECK(s.calls == calls); /* once failed, the sink is not called again */
    }
    sink_t s = { .fail_after = -1 };
    pcl_out_t o = { sink_write, &s, 0, 0 };
    pcl_region_t bad[] = { { 0, 0, 0, 5, rows2, 0 }, { 0, 0, 8, 0, rows2, 0 }, { 0, 0, PCL_PAGE_W + 8, 5, rows2, 0 }, { 0, 0, 8, PCL_PAGE_H + 1, rows2, 0 } };
    for (int i = 0; i < 4; i++) CHECK(pcl_page(&o, &bad[i]) < 0);
    CHECK(s.n == 0);
}

static int abort_row(void *u, int y, uint8_t *row)
{
    (void)u, (void)row;
    return y == 3 ? -9 : 0;
}

static void row_abort(void)
{
    sink_t s = { .fail_after = -1 };
    pcl_out_t o = { sink_write, &s, 0, 0 };
    pcl_region_t r = { 0, 0, 8, 10, abort_row, 0 };
    CHECK(pcl_page(&o, &r) == -9);
}

int main(void)
{
    exact_bytes();
    packbits_roundtrip();
    packbits_damaged();
    failures();
    row_abort();
    return CHECK_DONE("test_pcl");
}
