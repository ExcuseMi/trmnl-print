/* Fakes for the host tests: a printer that records what is posted, a file system of a few files, pictures. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef UTIL_H
#define UTIL_H
#include <stdlib.h>
#include <string.h>
#include "flist.h"
#include "ipp.h"
#include "pngio.h"
#include "printjob.h"

/* ---- the printer ---- */
typedef struct {
    int status;        /* what post returns: HTTP status or PJ_NET_* */
    int ipp;           /* the IPP status in the answer, -1: an answer that is not IPP */
    int posts;
    char url[96];
    uint8_t *body[8];  /* copies of the first posted bodies */
    size_t len[8];
} printer_t;

static inline int fake_post(void *u, const char *url, const void *body, size_t n, uint8_t *resp, size_t cap, size_t *rn)
{
    printer_t *p = u;
    if (p->posts < 8) {
        p->body[p->posts] = malloc(n);
        memcpy(p->body[p->posts], body, n);
        p->len[p->posts] = n;
    }
    p->posts++;
    strncpy(p->url, url, sizeof(p->url) - 1);
    *rn = 0;
    if (p->status == 200 && cap >= 8) {
        memset(resp, 0, 8);
        if (p->ipp >= 0) resp[0] = 1, resp[1] = 1, resp[2] = (uint8_t)(p->ipp >> 8), resp[3] = (uint8_t)p->ipp, *rn = 8;
        else resp[0] = 'x', *rn = 8;
    }
    return p->status;
}
static inline void printer_init(printer_t *p) { memset(p, 0, sizeof(*p)); p->status = 200; }
static inline void printer_free(printer_t *p)
{
    for (int i = 0; i < 8; i++) free(p->body[i]);
}

/* A printer from "host[:port][/path]" for the tests (a few static slots; a bad address gives an empty host) */
static inline const pj_printer_t *tp(const char *addr)
{
    static pj_printer_t slot[4];
    static int next;
    pj_printer_t *p = &slot[next++ & 3];
    if (pj_printer_parse(addr, p) < 0) memset(p, 0, sizeof(*p));
    return p;
}

/* ---- a file system: "group/name" files ---- */
#define FS_N 256
typedef struct {
    char path[160];
    uint8_t *data;
    size_t n;
    int unreadable;
} ffile_t;
typedef struct {
    ffile_t f[FS_N];
    int count;
    int fail_list_root; /* the listing fails */
} ffs_t;

static inline void ffs_put(ffs_t *s, const char *path, const void *d, size_t n)
{
    ffile_t *f = &s->f[s->count++];
    strcpy(f->path, path);
    f->data = malloc(n ? n : 1);
    memcpy(f->data, d, n);
    f->n = n;
}
static inline void ffs_free(ffs_t *s)
{
    for (int i = 0; i < s->count; i++) free(s->f[i].data);
}
static inline int ffs_list(void *u, const char *app, const char *dir, fl_cb_t cb, void *ctx)
{
    (void)app;
    ffs_t *s = u;
    if (s->fail_list_root && !dir[0]) return -1;
    size_t dl = strlen(dir);
    char seen[16][24];
    int ns = 0;
    for (int i = 0; i < s->count; i++) {
        const char *p = s->f[i].path;
        if (!dir[0]) {
            const char *sl = strchr(p, '/');
            if (!sl) continue;
            char g[24] = { 0 };
            memcpy(g, p, (size_t)(sl - p));
            int dup = 0;
            for (int k = 0; k < ns; k++) dup |= !strcmp(seen[k], g);
            if (dup) continue;
            strcpy(seen[ns++], g);
            if (!cb(g, true, 0, 0, ctx)) break;
        } else if (!strncmp(p, dir, dl) && p[dl] == '/') {
            if (!cb(p + dl + 1, false, s->f[i].n, 0, ctx)) break;
        }
    }
    return 0;
}
static inline int ffs_read(void *u, const fl_entry_t *e, uint8_t **buf, size_t *n)
{
    ffs_t *s = u;
    char path[128];
    strcpy(path, e->group), strcat(path, "/"), strcat(path, e->name);
    for (int i = 0; i < s->count; i++)
        if (!strcmp(s->f[i].path, path)) {
            if (s->f[i].unreadable) return -1;
            *buf = malloc(s->f[i].n ? s->f[i].n : 1);
            memcpy(*buf, s->f[i].data, s->f[i].n);
            *n = s->f[i].n;
            return 0;
        }
    return -1;
}

/* ---- a 1-bit picture (1 = white) as a PNG ---- */
static inline uint8_t *make_png(int w, int h, void (*paint)(uint8_t *px, int stride, int w, int h), size_t *n)
{
    int stride = (w + 7) / 8;
    uint8_t *px = malloc((size_t)stride * (size_t)h);
    memset(px, 0xff, (size_t)stride * (size_t)h);
    if (paint) paint(px, stride, w, h);
    size_t cap = png_max_size(w, h);
    uint8_t *f = malloc(cap);
    long k = png_encode(px, w, h, stride, f, cap);
    free(px);
    *n = k > 0 ? (size_t)k : 0;
    return f;
}
static inline void set_ink(uint8_t *px, int stride, int x, int y) { px[y * stride + (x >> 3)] &= (uint8_t)~(0x80 >> (x & 7)); }
#endif
