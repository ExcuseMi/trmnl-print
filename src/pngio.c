/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "pngio.h"
#include <stdlib.h>
#include <string.h>

static uint32_t crc_tab[256];
static int crc_ready;

static uint32_t crc32(uint32_t c, const uint8_t *p, size_t n)
{
    if (!crc_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) v = v & 1 ? 0xedb88320u ^ (v >> 1) : v >> 1;
            crc_tab[i] = v;
        }
        crc_ready = 1;
    }
    c = ~c;
    while (n--) c = crc_tab[(c ^ *p++) & 0xff] ^ (c >> 8);
    return ~c;
}

static void be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24), p[1] = (uint8_t)(v >> 16), p[2] = (uint8_t)(v >> 8), p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static const uint8_t SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
static const uint16_t len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

size_t png_max_size(int w, int h)
{
    size_t row = (size_t)(w + 7) / 8 + 1;
    return row * (size_t)h * 9 / 8 + 256;
}

/* ---- writer ---- */

typedef struct {
    uint8_t *out;
    size_t pos, cap;
    uint32_t acc;
    int nb, bad;
    int last, run; /* the byte run being collected */
    uint32_t a, b; /* adler32 */
    int rowlen, same_rows; /* a filtered row, and how many in a row repeat the one before */
    const uint8_t *prev;   /* the row they repeat */
} enc_t;

static void put_byte(enc_t *e, uint8_t v)
{
    if (e->pos >= e->cap) e->bad = 1;
    else e->out[e->pos++] = v;
}
static void put_bits(enc_t *e, uint32_t v, int n)
{
    e->acc |= v << e->nb;
    e->nb += n;
    while (e->nb >= 8) {
        put_byte(e, (uint8_t)e->acc);
        e->acc >>= 8;
        e->nb -= 8;
    }
}
static void put_code(enc_t *e, uint32_t code, int len) /* Huffman codes go most significant bit first */
{
    uint32_t r = 0;
    for (int i = 0; i < len; i++) r |= ((code >> i) & 1u) << (len - 1 - i);
    put_bits(e, r, len);
}
static void put_lit(enc_t *e, int v)
{
    if (v < 144) put_code(e, 0x30u + (uint32_t)v, 8);
    else put_code(e, 0x190u + (uint32_t)(v - 144), 9);
}
static void put_copy(enc_t *e, int len, int dist) /* a copy of `len` (3..258) bytes from `dist` back (1..32768) */
{
    int s = 28;
    while (len_base[s] > len) s--;
    uint32_t sym = 257u + (uint32_t)s;
    if (sym < 280) put_code(e, sym - 256, 7);
    else put_code(e, 0xc0u + (sym - 280), 8);
    if (len_extra[s]) put_bits(e, (uint32_t)(len - len_base[s]), len_extra[s]);
    int dc = 29;
    while (dist_base[dc] > dist) dc--;
    put_code(e, (uint32_t)dc, 5);
    if (dist_extra[dc]) put_bits(e, (uint32_t)(dist - dist_base[dc]), dist_extra[dc]);
}

static void flush_run(enc_t *e)
{
    if (e->run <= 0) return;
    put_lit(e, e->last);
    int rest = e->run - 1;
    while (rest >= 3) {
        int l = rest > 258 ? 258 : rest;
        put_copy(e, l, 1);
        rest -= l;
    }
    while (rest-- > 0) put_lit(e, e->last);
    e->run = 0;
}

/* The rows that repeated the one before: copies of up to 258 bytes from one row back (flush_run first) */
static void flush_rows(enc_t *e)
{
    if (e->same_rows == 0) return;
    flush_run(e);
    long left = (long)e->same_rows * e->rowlen;
    if (left < 3) { /* one tiny row: literals */
        for (long i = 0; i < left; i++) put_lit(e, e->prev[i]);
    }
    while (left >= 3) {
        int l = left > 258 ? 258 : (int)left;
        if (left > 258 && left - 258 < 3) l = (int)left - 3; /* never leave a tail a copy cannot take */
        put_copy(e, l, e->rowlen);
        left -= l;
    }
    e->same_rows = 0;
}

static void feed(enc_t *e, uint8_t v)
{
    e->a = (e->a + v) % 65521u;
    e->b = (e->b + e->a) % 65521u;
    if (e->run > 0 && v == e->last) {
        e->run++;
        return;
    }
    flush_run(e);
    e->last = v;
    e->run = 1;
}

long png_encode(const uint8_t *px, int w, int h, int stride, uint8_t *out, size_t cap)
{
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return -1;
    int rb = (w + 7) / 8;
    if (stride < rb || cap < 64) return -1;
    enc_t e = { out, 0, cap, 0, 0, 0, 0, 0, 1, 0, rb + 1, 0, 0 };
    for (int i = 0; i < 8; i++) put_byte(&e, SIG[i]);
    uint8_t ih[25] = { 0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    be32(ih + 8, (uint32_t)w), be32(ih + 12, (uint32_t)h);
    ih[16] = 1; /* 1 bit, gray, deflate, adaptive filtering, no interlace */
    be32(ih + 21, crc32(0, ih + 4, 17));
    for (int i = 0; i < 25; i++) put_byte(&e, ih[i]);
    size_t idat = e.pos;
    for (int i = 0; i < 8; i++) put_byte(&e, 0); /* length and type, filled in below */
    size_t data0 = e.pos;
    put_byte(&e, 0x78), put_byte(&e, 0x01);
    put_bits(&e, 1, 1); /* the last block */
    put_bits(&e, 1, 2); /* fixed Huffman */
    size_t rl = (size_t)rb + 1; /* a filtered row */
    uint8_t *row = malloc(rl), *prev = malloc(rl);
    if (!row || !prev) return free(row), free(prev), -1;
    e.prev = prev;
    for (int y = 0; y < h && !e.bad; y++) {
        row[0] = 2; /* filter Up */
        for (int x = 0; x < rb; x++) row[1 + x] = (uint8_t)(px[y * stride + x] - (y ? px[(y - 1) * stride + x] : 0));
        if (y && !memcmp(row, prev, rl)) { /* the same row again: a copy from one row back */
            for (int i = 0; i <= rb; i++) {
                e.a = (e.a + row[i]) % 65521u;
                e.b = (e.b + e.a) % 65521u;
            }
            e.same_rows++;
            continue;
        }
        flush_rows(&e);
        for (int i = 0; i <= rb; i++) feed(&e, row[i]);
        memcpy(prev, row, rl);
    }
    flush_rows(&e);
    free(row), free(prev);
    flush_run(&e);
    put_code(&e, 0, 7); /* end of block */
    if (e.nb) put_bits(&e, 0, 8 - e.nb);
    uint32_t ad = e.b << 16 | e.a;
    for (int i = 3; i >= 0; i--) put_byte(&e, (uint8_t)(ad >> (8 * i)));
    if (e.bad) return -1;
    be32(out + idat, (uint32_t)(e.pos - data0));
    memcpy(out + idat + 4, "IDAT", 4);
    uint32_t c = crc32(0, out + idat + 4, e.pos - idat - 4);
    for (int i = 3; i >= 0; i--) put_byte(&e, (uint8_t)(c >> (8 * i)));
    static const uint8_t end[12] = { 0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82 };
    for (int i = 0; i < 12; i++) put_byte(&e, end[i]);
    return e.bad ? -1 : (long)e.pos;
}

/* ---- reader ---- */

typedef struct {
    const uint8_t *in;
    size_t n, pos;
    uint32_t bitbuf;
    int nb, bad;
    uint8_t *win; /* the last 32 KB of the unfiltered-deflate output, for matches */
    uint32_t total;
    uint8_t *px;
    int stride, rb, h, row, col, filter;
} dec_t;

static int get_bit(dec_t *d)
{
    if (d->nb == 0) {
        if (d->pos >= d->n) return d->bad = 1, 0;
        d->bitbuf = d->in[d->pos++];
        d->nb = 8;
    }
    int v = (int)(d->bitbuf & 1);
    d->bitbuf >>= 1;
    d->nb--;
    return v;
}
static uint32_t get_bits(dec_t *d, int n) /* least significant first */
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)get_bit(d) << i;
    return v;
}

static void emit(dec_t *d, uint8_t b)
{
    d->win[d->total++ & 0x7fff] = b;
    if (d->row >= d->h) return (void)(d->bad = 1);
    if (d->col == 0) {
        d->filter = b;
        if (b > 2) d->bad = 1;
        d->col = 1;
        return;
    }
    int i = d->col - 1;
    uint8_t *r = d->px + (size_t)d->row * (size_t)d->stride;
    uint8_t left = i ? r[i - 1] : 0, up = d->row ? r[i - d->stride] : 0;
    r[i] = (uint8_t)(b + (d->filter == 1 ? left : d->filter == 2 ? up : 0));
    if (++d->col > d->rb) d->col = 0, d->row++;
}

static int inflate_block(dec_t *d, int fixed)
{
    if (!fixed) { /* stored */
        d->nb = 0;
        if (d->pos + 4 > d->n) return -1;
        unsigned len = d->in[d->pos] | d->in[d->pos + 1] << 8, nlen = d->in[d->pos + 2] | d->in[d->pos + 3] << 8;
        d->pos += 4;
        if ((len ^ 0xffffu) != nlen || d->pos + len > d->n) return -1;
        for (unsigned i = 0; i < len && !d->bad; i++) emit(d, d->in[d->pos++]);
        return d->bad ? -1 : 0;
    }
    for (;;) {
        uint32_t code = 0;
        for (int i = 0; i < 7; i++) code = code << 1 | (uint32_t)get_bit(d);
        int sym;
        if (code <= 0x17) sym = 256 + (int)code;
        else {
            code = code << 1 | (uint32_t)get_bit(d);
            if (code >= 0x30 && code <= 0xbf) sym = (int)code - 0x30;
            else if (code >= 0xc0 && code <= 0xc7) sym = 280 + (int)code - 0xc0;
            else {
                code = code << 1 | (uint32_t)get_bit(d);
                if (code < 0x190 || code > 0x1ff) return -1;
                sym = 144 + (int)code - 0x190;
            }
        }
        if (d->bad) return -1;
        if (sym < 256) emit(d, (uint8_t)sym);
        else if (sym == 256) return 0;
        else {
            int s = sym - 257;
            if (s > 28) return -1;
            int len = len_base[s] + (int)get_bits(d, len_extra[s]);
            uint32_t dc = 0;
            for (int i = 0; i < 5; i++) dc = dc << 1 | (uint32_t)get_bit(d);
            if (dc > 29) return -1;
            uint32_t dist = dist_base[dc] + get_bits(d, dist_extra[dc]);
            if (dist > d->total || dist > 32768) return -1;
            for (int i = 0; i < len && !d->bad; i++) emit(d, d->win[(d->total - dist) & 0x7fff]);
        }
        if (d->bad) return -1;
    }
}

int png_decode(const uint8_t *in, size_t n, uint8_t *px, int w, int h, int stride)
{
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return -1;
    int rb = (w + 7) / 8;
    if (n < 8 + 25 || memcmp(in, SIG, 8) || stride < rb) return -1;
    const uint8_t *ih = in + 8;
    if (rd32(ih) != 13 || memcmp(ih + 4, "IHDR", 4) || rd32(ih + 8) != (uint32_t)w || rd32(ih + 12) != (uint32_t)h ||
        ih[16] != 1 || ih[17] != 0 || ih[18] != 0 || ih[19] != 0 || ih[20] != 0 || rd32(ih + 21) != crc32(0, ih + 4, 17))
        return -1;
    size_t p = 8 + 25;
    const uint8_t *idat = 0;
    size_t idat_n = 0;
    int ended = 0;
    while (p + 12 <= n) {
        uint32_t len = rd32(in + p);
        if (len > n || p + 12 + len > n) return -1;
        const uint8_t *type = in + p + 4, *data = in + p + 8;
        if (!memcmp(type, "IDAT", 4)) {
            if (idat) return -1; /* one chunk only */
            if (rd32(data + len) != crc32(0, type, 4 + len)) return -1;
            idat = data, idat_n = len;
        } else if (!memcmp(type, "IEND", 4)) {
            ended = 1;
            break;
        }
        p += 12 + len;
    }
    if (!idat || !ended || idat_n < 6) return -1;
    if ((idat[0] & 0x0f) != 8 || ((idat[0] << 8) | idat[1]) % 31 || (idat[1] & 0x20)) return -1;
    uint8_t *tmp = malloc((size_t)stride * (size_t)h), *win = malloc(32768);
    if (!tmp || !win) return free(tmp), free(win), -1;
    dec_t d = { idat, idat_n - 4, 2, 0, 0, 0, win, 0, tmp, stride, rb, h, 0, 0, 0 };
    int rc = 0, last = 0;
    while (!last && rc == 0) {
        last = get_bit(&d);
        uint32_t t = get_bits(&d, 2);
        rc = d.bad || t > 1 ? -1 : inflate_block(&d, (int)t);
    }
    if (rc == 0 && (d.bad || d.row != h || d.col != 0)) rc = -1;
    if (rc == 0) memcpy(px, tmp, (size_t)stride * (size_t)h); /* the IDAT chunk's CRC covered the bytes */
    free(tmp);
    free(win);
    return rc;
}
