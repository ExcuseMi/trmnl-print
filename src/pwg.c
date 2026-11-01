/* SPDX-License-Identifier: MIT */
#include "pwg.h"
#include <string.h>

static void out(pcl_out_t *o, const void *b, size_t n)
{
    if (o->err) return;
    int rc = o->write(o->u, b, n);
    if (rc < 0) o->err = rc;
    else o->bytes += n;
}

size_t pwg_encode_line(const uint8_t *s, size_t n, uint8_t *d)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && s[i + run] == s[i]) run++;
        if (run >= 2 || i + 1 == n) {
            d[o++] = (uint8_t)(run - 1);
            d[o++] = s[i];
            i += run;
            continue;
        }
        size_t st = i;
        i++;
        while (i < n && i - st < 128 && !(i + 1 < n && s[i] == s[i + 1])) i++;
        if (i - st == 1) { /* a lone pixel before a run */
            d[o++] = 0;
            d[o++] = s[st];
            continue;
        }
        d[o++] = (uint8_t)(257 - (i - st));
        memcpy(d + o, s + st, i - st);
        o += i - st;
    }
    return o;
}

long pwg_decode_line(const uint8_t *s, size_t len, size_t *used, uint8_t *d, size_t n)
{
    size_t i = 0, o = 0;
    while (o < n) {
        if (i >= len) return -1;
        unsigned c = s[i++];
        if (c == 128) return -1;
        if (c < 128) {
            size_t k = c + 1;
            if (i >= len || o + k > n) return -1;
            memset(d + o, s[i++], k);
            o += k;
        } else {
            size_t k = 257 - c;
            if (i + k > len || o + k > n) return -1;
            memcpy(d + o, s + i, k);
            i += k;
            o += k;
        }
    }
    *used = i;
    return (long)o;
}

void pwg_job_begin(pcl_out_t *o) { out(o, "RaS2", 4); }
void pwg_job_end(pcl_out_t *o) { (void)o; }

static void put32(uint8_t *h, int off, uint32_t v)
{
    h[off] = (uint8_t)(v >> 24), h[off + 1] = (uint8_t)(v >> 16), h[off + 2] = (uint8_t)(v >> 8), h[off + 3] = (uint8_t)v;
}

static void header(uint8_t *h)
{
    memset(h, 0, PWG_HEADER);
    memcpy(h, "PwgRaster", 10);
    put32(h, 272, 0);                 /* Duplex off */
    put32(h, 276, PWG_DPI), put32(h, 280, PWG_DPI);
    put32(h, 340, 1);                 /* NumCopies */
    put32(h, 352, 595), put32(h, 356, 842); /* PageSize in points: A4 */
    put32(h, 372, PWG_PAGE_W), put32(h, 376, PWG_PAGE_H);
    put32(h, 384, 8), put32(h, 388, 8);     /* 8 bits, 8 bits per pixel */
    put32(h, 392, PWG_PAGE_W);              /* bytes per line */
    put32(h, 396, 0);                       /* chunky */
    put32(h, 400, 18);                      /* sGray */
    put32(h, 420, 1);                       /* colours */
    put32(h, 452, 0);                       /* TotalPageCount: unknown */
    put32(h, 456, 1), put32(h, 460, 1);     /* cross feed, feed transform */
    put32(h, 464, 0), put32(h, 468, 0), put32(h, 472, PWG_PAGE_W), put32(h, 476, PWG_PAGE_H); /* image box */
    put32(h, 480, 0x00ffffff);              /* alternate primary: white */
}

typedef struct {
    pcl_out_t *o;
    uint8_t prev[PWG_PAGE_W], enc[PWG_ENC_MAX(PWG_PAGE_W)];
    size_t enc_n;
    int repeat; /* how many times the held line is wanted, 0: none held */
} lines_t;

static void flush(lines_t *l)
{
    if (!l->repeat) return;
    uint8_t c = (uint8_t)(l->repeat - 1);
    out(l->o, &c, 1);
    out(l->o, l->enc, l->enc_n);
    l->repeat = 0;
}

static void line(lines_t *l, const uint8_t *cur)
{
    if (l->repeat && l->repeat < 256 && !memcmp(l->prev, cur, PWG_PAGE_W)) {
        l->repeat++;
        return;
    }
    flush(l);
    memcpy(l->prev, cur, PWG_PAGE_W);
    l->enc_n = pwg_encode_line(cur, PWG_PAGE_W, l->enc);
    l->repeat = 1;
}

int pwg_page(pcl_out_t *o, const pcl_region_t *r, int scale)
{
    long rw = (long)r->w * scale;
    if (scale < 1 || scale > 2 || r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0 || (long)r->x * scale + rw > PWG_PAGE_W || ((long)r->y + r->h) * scale > PWG_PAGE_H) return -1;
    static uint8_t cur[PWG_PAGE_W], src[PWG_PAGE_W / 8];
    static lines_t L;
    uint8_t h[PWG_HEADER];
    header(h);
    out(o, h, sizeof(h));
    L.o = o;
    L.repeat = 0;
    long y0 = (long)r->y * scale, y1 = y0 + (long)r->h * scale;
    long x0 = (long)r->x * scale;
    int have_row = -1;
    for (long y = 0; y < PWG_PAGE_H && !o->err; y++) {
        if (y < y0 || y >= y1) {
            memset(cur, 0xff, sizeof(cur));
        } else {
            int j = (int)((y - y0) / scale);
            if (j != have_row) {
                int rc = r->get_row(r->u, j, src);
                if (rc < 0) return flush(&L), rc;
                have_row = j;
                memset(cur, 0xff, sizeof(cur));
                for (long px = 0; px < rw; px++)
                    if (!(src[(px / scale) >> 3] & (0x80 >> ((px / scale) & 7)))) cur[x0 + px] = 0;
            }
        }
        line(&L, cur);
    }
    flush(&L);
    return o->err;
}
