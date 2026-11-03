/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
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

static void flush(pwg_lines_t *l)
{
    if (!l->repeat) return;
    uint8_t c = (uint8_t)(l->repeat - 1);
    out(l->o, &c, 1);
    out(l->o, l->enc, l->enc_n);
    l->repeat = 0;
}

/* `same`: the caller knows the line is the one before (the same picture row, or white again) */
static void line(pwg_lines_t *l, const uint8_t *cur, int same)
{
    if (l->repeat && (same || !memcmp(l->prev, cur, PWG_PAGE_W))) {
        if (l->repeat < 256) {
            l->repeat++;
            return;
        }
        flush(l); /* the held line, encoded already, starts again */
        l->repeat = 1;
        return;
    }
    flush(l);
    memcpy(l->prev, cur, PWG_PAGE_W);
    l->enc_n = pwg_encode_line(cur, PWG_PAGE_W, l->enc);
    l->repeat = 1;
}

/* 8 gray bytes of each 1-bit byte */
static uint8_t lut[256][8];
static void lut_init(void)
{
    if (lut[0][0] == 0 && lut[255][0] == 0xff) return;
    for (int b = 0; b < 256; b++)
        for (int k = 0; k < 8; k++) lut[b][k] = (b & (0x80 >> k)) ? 0xff : 0;
}

/* the region's row into cur: 1 bit to 8, `scale` times wide */
static void expand(pwg_run_t *p, const uint8_t *src)
{
    uint8_t *d = p->cur + p->x0;
    int full = p->r.w / 8, scale = p->scale;
    if (scale == 1)
        for (int i = 0; i < full; i++) memcpy(d + 8 * i, lut[src[i]], 8);
    else
        for (int i = 0; i < full; i++)
            for (int k = 0; k < 8; k++) d[16 * i + 2 * k] = d[16 * i + 2 * k + 1] = lut[src[i]][k];
    for (long px = (long)full * 8 * scale; px < p->rw; px++) d[px] = (src[(px / scale) >> 3] & (0x80 >> ((px / scale) & 7))) ? 0xff : 0;
}

int pwg_page_begin(pwg_run_t *p, pcl_out_t *o, const pcl_region_t *r, int scale)
{
    long rw = (long)r->w * scale;
    if (scale < 1 || scale > 2 || r->w <= 0 || r->h <= 0 || r->x < 0 || r->y < 0 || (long)r->x * scale + rw > PWG_PAGE_W || ((long)r->y + r->h) * scale > PWG_PAGE_H) return -1;
    lut_init();
    uint8_t h[PWG_HEADER];
    header(h);
    out(o, h, sizeof(h));
    p->o = o, p->r = *r, p->scale = scale, p->rw = rw;
    p->L.o = o;
    p->L.repeat = 0;
    p->y = 0, p->y0 = (long)r->y * scale, p->y1 = p->y0 + (long)r->h * scale, p->x0 = (long)r->x * scale;
    p->have_row = -1, p->white = 0, p->k = 0;
    return o->err;
}

int pwg_page_step(pwg_run_t *p, int rows)
{
    pcl_out_t *o = p->o;
    for (int n = 0; n < rows && p->y < PWG_PAGE_H && !o->err; n++, p->y++) {
        int same = 0;
        if (p->y < p->y0 || p->y >= p->y1) {
            same = p->white;
            if (!same) memset(p->cur, 0xff, sizeof(p->cur)), p->white = 1, p->have_row = -1;
        } else {
            int j = (int)((p->y - p->y0) / p->scale);
            same = j == p->have_row;
            if (!same) {
                uint8_t *src = p->src[p->k ^ 1];
                int rc = p->r.get_row(p->r.u, j, src);
                if (rc < 0) return flush(&p->L), rc;
                same = p->have_row >= 0 && p->r.w % 8 == 0 && !memcmp(src, p->src[p->k], (size_t)p->r.w / 8);
                if (!same) expand(p, src); /* outside the region cur stays white */
                p->k ^= 1, p->have_row = j, p->white = 0;
            }
        }
        line(&p->L, p->cur, same);
    }
    if (o->err) return o->err;
    if (p->y < PWG_PAGE_H) return 1;
    flush(&p->L);
    return o->err;
}

int pwg_page(pcl_out_t *o, const pcl_region_t *r, int scale)
{
    static pwg_run_t P;
    int rc = pwg_page_begin(&P, o, r, scale);
    if (rc < 0) return rc;
    while ((rc = pwg_page_step(&P, PWG_PAGE_H)) > 0) {}
    return rc;
}
