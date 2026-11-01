/* SPDX-License-Identifier: MIT */
#include "printer.h"
#include <stdio.h>
#include <string.h>

static int path_ok(const char *s)
{
    size_t n = strlen(s);
    if (n >= sizeof(((pj_printer_t *)0)->path)) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '~' || c == '/' || c == '-';
        if (!ok || (c == '.' && s[i + 1] == '.')) return 0;
    }
    return 1;
}

static int host_ok(const char *s, size_t n, size_t cap)
{
    if (n == 0 || n >= cap) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) return 0;
    }
    return 1;
}

static int port_parse(const char *s, size_t n)
{
    long p = 0;
    if (n == 0 || n > 5) return -1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        p = p * 10 + (s[i] - '0');
    }
    return p >= 1 && p <= 65535 ? (int)p : -1;
}

static void init(pj_printer_t *p)
{
    memset(p, 0, sizeof(*p));
    p->port = PJ_DEFAULT_PORT;
}

int pj_printer_parse(const char *s, pj_printer_t *p)
{
    init(p);
    if (!s) return -1;
    const char *slash = strchr(s, '/');
    size_t an = slash ? (size_t)(slash - s) : strlen(s);
    const char *c = memchr(s, ':', an);
    size_t hl = c ? (size_t)(c - s) : an;
    if (c) {
        p->port = port_parse(c + 1, an - hl - 1);
        if (p->port < 0) return init(p), -1;
    }
    if (!host_ok(s, hl, sizeof(p->host))) return init(p), -1;
    memcpy(p->host, s, hl);
    if (slash) {
        const char *q = slash;
        while (*q == '/') q++;
        if (*q && !path_ok(q)) return init(p), -1;
        snprintf(p->path, sizeof(p->path), "%s", q);
    }
    return 0;
}

void pj_printer_addr(const pj_printer_t *p, char *out, size_t cap)
{
    int n = snprintf(out, cap, "%s", p->host);
    if (p->port != PJ_DEFAULT_PORT && n > 0 && (size_t)n < cap) n += snprintf(out + n, cap - (size_t)n, ":%d", p->port);
    if (p->path[0] && strcmp(p->path, PJ_DEFAULT_PATH) != 0 && n > 0 && (size_t)n < cap) snprintf(out + n, cap - (size_t)n, "/%s", p->path);
}

int pj_cfg_format(const pj_printer_t *p, char *out, size_t cap)
{
    int n = snprintf(out, cap, "name=%s\nhost=%s\nport=%d\npath=%s\nformat=%s\n", p->name, p->host, p->port,
                     p->path[0] ? p->path : PJ_DEFAULT_PATH, p->format ? "pcl" : "pwg");
    return n > 0 && (size_t)n < cap ? n : -1;
}

int pj_cfg_parse(const char *text, size_t n, pj_printer_t *p)
{
    init(p);
    size_t i = 0;
    int have_host = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        size_t len = e - i;
        if (len && text[e - 1] == '\r') len--;
        const char *eq = memchr(text + i, '=', len);
        if (eq) {
            size_t kl = (size_t)(eq - (text + i)), vl = len - kl - 1;
            const char *v = eq + 1;
            if (kl == 4 && !memcmp(text + i, "name", 4)) {
                size_t k = 0;
                for (size_t j = 0; j < vl && k + 1 < sizeof(p->name); j++)
                    if ((unsigned char)v[j] >= 0x20) p->name[k++] = v[j];
                p->name[k] = 0;
            } else if (kl == 4 && !memcmp(text + i, "host", 4)) {
                if (host_ok(v, vl, sizeof(p->host))) memcpy(p->host, v, vl), p->host[vl] = 0, have_host = 1;
                else have_host = 0, p->host[0] = 0;
            } else if (kl == 4 && !memcmp(text + i, "port", 4)) {
                int q = port_parse(v, vl);
                if (q > 0) p->port = q;
            } else if (kl == 4 && !memcmp(text + i, "path", 4)) {
                char t[sizeof(p->path)];
                size_t s = 0;
                while (s < vl && v[s] == '/') s++;
                if (vl - s < sizeof(t)) {
                    memcpy(t, v + s, vl - s), t[vl - s] = 0;
                    if (path_ok(t)) memcpy(p->path, t, vl - s + 1);
                }
            } else if (kl == 6 && !memcmp(text + i, "format", 6)) {
                p->format = vl == 3 && !memcmp(v, "pcl", 3) ? 1 : 0;
            }
        }
        i = e + 1;
    }
    return have_host ? 0 : (init(p), PJ_NO_PRINTER);
}

int pj_default_printer(const app_api_t *api, pj_printer_t *out)
{
    if (!api || !out) return APP_EINVAL;
    if (api->size < APP_API_SLOT(53)) return APP_ENOTSUP;
    app_file_t *f = api->file_open_app(PJ_CFG_APP, PJ_CFG_FILE);
    if (!f) return PJ_NO_PRINTER;
    char buf[PJ_CFG_MAX];
    size_t len = 0;
    int k = 0;
    while (len < sizeof(buf) && (k = api->file_read(f, buf + len, sizeof(buf) - len)) > 0) len += (size_t)k;
    api->file_close(f);
    if (k < 0) return APP_EIO;
    return pj_cfg_parse(buf, len, out);
}
