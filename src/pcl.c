/* SPDX-License-Identifier: MIT */
#include "pcl.h"
#include <string.h>

static void emit(pcl_out_t *o, const void *b, size_t n)
{
    if (o->err) return;
    int rc = o->write(o->u, b, n);
    if (rc < 0) o->err = rc;
    else o->bytes += n;
}

/* ESC, `pre`, the number, `post`: "\x1b*p" 12 "X" (the loader has no snprintf) */
static void emitf(pcl_out_t *o, const char *pre, int a, char post)
{
    char t[24];
    size_t n = 0;
    while (*pre) t[n++] = *pre++;
    char d[12];
    int m = 0;
    do d[m++] = (char)('0' + a % 10), a /= 10; while (a);
    while (m) t[n++] = d[--m];
    t[n++] = post;
    emit(o, t, n);
}

size_t packbits_encode(const uint8_t *s, size_t n, uint8_t *d)
{
    size_t i = 0, o = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && s[i + run] == s[i]) run++;
        if (run >= 2) { /* a run: 1 - run, then the byte */
            d[o++] = (uint8_t)(1 - (int)run);
            d[o++] = s[i];
            i += run;
            continue;
        }
        size_t st = i; /* literals until a run of 3 starts (a run of 2 is no gain inside literals) */
        i++;
        while (i < n && i - st < 128 && !(i + 2 < n && s[i] == s[i + 1] && s[i] == s[i + 2])) i++;
        d[o++] = (uint8_t)(i - st - 1);
        memcpy(d + o, s + st, i - st);
        o += i - st;
    }
    return o;
}

long packbits_decode(const uint8_t *s, size_t n, uint8_t *d, size_t cap)
{
    size_t i = 0, o = 0;
    while (i < n) {
        int c = (int8_t)s[i++];
        if (c >= 0) {
            size_t k = (size_t)c + 1;
            if (i + k > n || o + k > cap) return -1;
            memcpy(d + o, s + i, k);
            i += k;
            o += k;
        } else if (c != -128) {
            size_t k = (size_t)(1 - c);
            if (i >= n || o + k > cap) return -1;
            memset(d + o, s[i++], k);
            o += k;
        }
    }
    return (long)o;
}

void pcl_job_begin(pcl_out_t *o)
{
    emit(o, "\x1b" "E", 2);          /* reset */
    emit(o, "\x1b&u300D", 7);        /* units: 300 per inch */
    emit(o, "\x1b&l26A", 6);         /* A4 */
    emit(o, "\x1b&l0O", 5);          /* portrait */
}

void pcl_job_end(pcl_out_t *o) { emit(o, "\x1b" "E", 2); }

int pcl_page(pcl_out_t *o, const pcl_region_t *r)
{
    if (r->w <= 0 || r->h <= 0 || r->w > PCL_PAGE_W || r->h > PCL_PAGE_H) return -1;
    int rb = r->w / 8, x = r->x - PCL_OFF_X, y = r->y - PCL_OFF_Y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    uint8_t row[PCL_PAGE_W / 8], enc[PCL_PAGE_W / 8 + PCL_PAGE_W / 8 / 128 + 2];
    emitf(o, "\x1b*p", x, 'X');
    emitf(o, "\x1b*p", y, 'Y');
    emit(o, "\x1b*t300R", 7);        /* 300 dpi */
    emit(o, "\x1b*r1A", 5);          /* start raster at the cursor */
    emit(o, "\x1b*b2M", 5);          /* PackBits rows */
    for (int j = 0; j < r->h && !o->err; j++) {
        int rc = r->get_row(r->u, j, row);
        if (rc < 0) return rc;
        for (int i = 0; i < rb; i++) row[i] = (uint8_t)~row[i]; /* PCL: 1 = ink */
        size_t n = packbits_encode(row, (size_t)rb, enc);
        emitf(o, "\x1b*b", (int)n, 'W');
        emit(o, enc, n);
    }
    emit(o, "\x1b*rB", 4);           /* end raster */
    emit(o, "\f", 1);
    return o->err;
}
