/* SPDX-License-Identifier: MIT */
#include "pcl.h"
#include <string.h>

static void pcl_emit(pcl_out_t *o, const void *b, size_t n)
{
    if (o->err) return;
    int rc = o->write(o->u, b, n);
    if (rc < 0) o->err = rc;
    else o->bytes += n;
}

/* ESC, `pre`, the number, `post`: "\x1b*p" 12 "X" (the loader has no snprintf) */
static void pcl_emitf(pcl_out_t *o, const char *pre, int a, char post)
{
    char t[24];
    size_t n = 0;
    while (*pre) t[n++] = *pre++;
    char d[12];
    int m = 0;
    do d[m++] = (char)('0' + a % 10), a /= 10; while (a);
    while (m) t[n++] = d[--m];
    t[n++] = post;
    pcl_emit(o, t, n);
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
    pcl_emit(o, "\x1b" "E", 2);          /* reset */
    pcl_emit(o, "\x1b&u300D", 7);        /* units: 300 per inch */
    pcl_emit(o, "\x1b&l26A", 6);         /* A4 */
    pcl_emit(o, "\x1b&l0O", 5);          /* portrait */
}

void pcl_job_end(pcl_out_t *o) { pcl_emit(o, "\x1b" "E", 2); }

int pcl_page_begin(pcl_run_t *p, pcl_out_t *o, const pcl_region_t *r)
{
    if (r->w <= 0 || r->h <= 0 || r->w > PCL_PAGE_W || r->h > PCL_PAGE_H) return -1;
    int x = r->x - PCL_OFF_X, y = r->y - PCL_OFF_Y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p->o = o, p->r = *r, p->j = 0;
    pcl_emitf(o, "\x1b*p", x, 'X');
    pcl_emitf(o, "\x1b*p", y, 'Y');
    pcl_emit(o, "\x1b*t300R", 7);        /* 300 dpi */
    pcl_emit(o, "\x1b*r1A", 5);          /* start raster at the cursor */
    pcl_emit(o, "\x1b*b2M", 5);          /* PackBits rows */
    return o->err;
}

int pcl_page_step(pcl_run_t *p, int rows)
{
    pcl_out_t *o = p->o;
    int rb = p->r.w / 8;
    uint8_t row[PCL_PAGE_W / 8], enc[PCL_PAGE_W / 8 + PCL_PAGE_W / 8 / 128 + 2];
    for (int n = 0; n < rows && p->j < p->r.h && !o->err; n++, p->j++) {
        int rc = p->r.get_row(p->r.u, p->j, row);
        if (rc < 0) return rc;
        for (int i = 0; i < rb; i++) row[i] = (uint8_t)~row[i]; /* PCL: 1 = ink */
        size_t k = packbits_encode(row, (size_t)rb, enc);
        pcl_emitf(o, "\x1b*b", (int)k, 'W');
        pcl_emit(o, enc, k);
    }
    if (o->err) return o->err;
    if (p->j < p->r.h) return 1;
    pcl_emit(o, "\x1b*rB", 4);           /* end raster */
    pcl_emit(o, "\f", 1);
    return o->err;
}

int pcl_page(pcl_out_t *o, const pcl_region_t *r)
{
    pcl_run_t p;
    int rc = pcl_page_begin(&p, o, r);
    if (rc < 0) return rc;
    while ((rc = pcl_page_step(&p, PCL_PAGE_H)) > 0) {}
    return rc;
}
