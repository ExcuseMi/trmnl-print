/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "ipp.h"
#include <string.h>

typedef struct {
    uint8_t *p;
    size_t cap, n;
    int bad;
} wr_t;

static void wput(wr_t *w, const void *b, size_t n)
{
    if (w->bad || w->n + n > w->cap) { w->bad = 1; return; }
    memcpy(w->p + w->n, b, n);
    w->n += n;
}
static void put8(wr_t *w, int v) { uint8_t b = (uint8_t)v; wput(w, &b, 1); }
static void put16(wr_t *w, int v) { put8(w, v >> 8); put8(w, v); }

/* tag, name, value */
static void attr(wr_t *w, int tag, const char *name, const char *val)
{
    size_t ln = strlen(name), lv = strlen(val);
    if (ln > 255 || lv > 255) { w->bad = 1; return; }
    put8(w, tag);
    put16(w, (int)ln);
    wput(w, name, ln);
    put16(w, (int)lv);
    wput(w, val, lv);
}

size_t ipp_print_job_header(uint8_t *out, size_t cap, const char *host, int port, const char *path, const char *job_name, const char *mime)
{
    wr_t w = { out, cap, 0, 0 };
    char uri[200];
    size_t hl = strlen(host), pl = path && path[0] ? strlen(path) : sizeof("ipp/print") - 1;
    if (!hl || hl > 100 || pl > 60 || port < 1 || port > 65535) return 0;
    char d[6];
    int m = 0;
    for (int v = port; v; v /= 10) d[m++] = (char)('0' + v % 10);
    size_t u = 0;
    memcpy(uri, "ipp://", 6), u = 6;
    memcpy(uri + u, host, hl), u += hl;
    uri[u++] = ':';
    while (m) uri[u++] = d[--m];
    uri[u++] = '/';
    memcpy(uri + u, path && path[0] ? path : "ipp/print", pl), u += pl;
    uri[u] = 0;
    put16(&w, 0x0101); /* IPP 1.1 */
    put16(&w, 0x0002); /* Print-Job */
    put16(&w, 0); put16(&w, 1); /* request id 1 */
    put8(&w, 0x01);    /* operation attributes */
    attr(&w, 0x47, "attributes-charset", "utf-8");
    attr(&w, 0x48, "attributes-natural-language", "en");
    attr(&w, 0x45, "printer-uri", uri);
    attr(&w, 0x42, "requesting-user-name", "tiny-paper");
    attr(&w, 0x42, "job-name", job_name);
    attr(&w, 0x49, "document-format", mime);
    put8(&w, 0x03);    /* end of attributes */
    return w.bad ? 0 : w.n;
}

int ipp_status(const uint8_t *r, size_t n)
{
    if (n < 8 || r[0] != 1) return -1;
    return (r[2] << 8) | r[3];
}
