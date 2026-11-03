/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "printer.h"
#include "strbuf.h"
#include <string.h>

static int pc_path_ok(const char *s)
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

static int pc_host_ok(const char *s, size_t n, size_t cap)
{
    if (n == 0 || n >= cap) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (!ok) return 0;
    }
    return 1;
}

static int pc_port_parse(const char *s, size_t n)
{
    long p = 0;
    if (n == 0 || n > 5) return -1;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        p = p * 10 + (s[i] - '0');
    }
    return p >= 1 && p <= 65535 ? (int)p : -1;
}

static const char *pc_find_c(const char *s, size_t n, char c)
{
    for (size_t i = 0; i < n; i++)
        if (s[i] == c) return s + i;
    return 0;
}

static void pc_init(pj_printer_t *p)
{
    memset(p, 0, sizeof(*p));
    p->port = PJ_DEFAULT_PORT;
}

int pj_printer_parse(const char *s, pj_printer_t *p)
{
    pc_init(p);
    if (!s) return -1;
    const char *slash = strchr(s, '/');
    size_t an = slash ? (size_t)(slash - s) : strlen(s);
    const char *c = pc_find_c(s, an, ':');
    size_t hl = c ? (size_t)(c - s) : an;
    if (c) {
        p->port = pc_port_parse(c + 1, an - hl - 1);
        if (p->port < 0) return pc_init(p), -1;
    }
    if (!pc_host_ok(s, hl, sizeof(p->host))) return pc_init(p), -1;
    memcpy(p->host, s, hl);
    if (slash) {
        const char *q = slash;
        while (*q == '/') q++;
        if (*q && !pc_path_ok(q)) return pc_init(p), -1;
        sb_set(p->path, sizeof(p->path), q);
    }
    return 0;
}

void pj_printer_addr(const pj_printer_t *p, char *out, size_t cap)
{
    sb_set(out, cap, p->host);
    if (p->port != PJ_DEFAULT_PORT) sb_cat(out, cap, ":"), sb_int(out, cap, p->port, 0);
    if (p->path[0] && strcmp(p->path, PJ_DEFAULT_PATH) != 0) sb_cat(out, cap, "/"), sb_cat(out, cap, p->path);
}

void pj_printer_label(const pj_printer_t *p, char *out, size_t cap)
{
    if (p->name[0]) sb_set(out, cap, p->name);
    else pj_printer_addr(p, out, cap);
}

int pj_cfg_format(const pj_printer_t *p, char *out, size_t cap)
{
    int bad = sb_set(out, cap, "name=");
    bad |= sb_cat(out, cap, p->name) | sb_cat(out, cap, "\nhost=") | sb_cat(out, cap, p->host) | sb_cat(out, cap, "\nport=");
    bad |= sb_int(out, cap, p->port, 0) | sb_cat(out, cap, "\npath=") | sb_cat(out, cap, p->path[0] ? p->path : PJ_DEFAULT_PATH);
    bad |= sb_cat(out, cap, p->format ? "\nformat=pcl\n" : "\nformat=pwg\n");
    return bad ? -1 : (int)strlen(out);
}

int pj_cfg_parse(const char *text, size_t n, pj_printer_t *p)
{
    pc_init(p);
    size_t i = 0;
    int have_host = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && text[e] != '\n') e++;
        size_t len = e - i;
        if (len && text[e - 1] == '\r') len--;
        const char *eq = pc_find_c(text + i, len, '=');
        if (eq) {
            size_t kl = (size_t)(eq - (text + i)), vl = len - kl - 1;
            const char *v = eq + 1;
            if (kl == 4 && !memcmp(text + i, "name", 4)) {
                size_t k = 0;
                for (size_t j = 0; j < vl && k + 1 < sizeof(p->name); j++)
                    if ((unsigned char)v[j] >= 0x20) p->name[k++] = v[j];
                p->name[k] = 0;
            } else if (kl == 4 && !memcmp(text + i, "host", 4)) {
                if (pc_host_ok(v, vl, sizeof(p->host))) memcpy(p->host, v, vl), p->host[vl] = 0, have_host = 1;
                else have_host = 0, p->host[0] = 0;
            } else if (kl == 4 && !memcmp(text + i, "port", 4)) {
                int q = pc_port_parse(v, vl);
                if (q > 0) p->port = q;
            } else if (kl == 4 && !memcmp(text + i, "path", 4)) {
                char t[sizeof(p->path)];
                size_t s = 0;
                while (s < vl && v[s] == '/') s++;
                if (vl - s < sizeof(t)) {
                    memcpy(t, v + s, vl - s), t[vl - s] = 0;
                    if (pc_path_ok(t)) memcpy(p->path, t, vl - s + 1);
                }
            } else if (kl == 6 && !memcmp(text + i, "format", 6)) {
                p->format = vl == 3 && !memcmp(v, "pcl", 3) ? 1 : 0;
            }
        }
        i = e + 1;
    }
    return have_host ? 0 : (pc_init(p), PJ_NO_PRINTER);
}

int pj_device_text(const app_api_t *api, char *buf, size_t cap)
{
    if (!api || !buf || cap < PJ_CFG_MAX + 1 || api->size < APP_API_SLOT(58) || !api->device_setting_get) return APP_ENOTSUP;
    int n = api->device_setting_get(PJ_DEV_KEY, buf, cap);
    if (n == APP_ENOENT) return PJ_NO_PRINTER;
    if (n < 0 || (size_t)n >= cap) return APP_EIO;
    return n;
}

int pj_default_printer(const app_api_t *api, pj_printer_t *out)
{
    if (!api || !out) return APP_EINVAL;
    char buf[PJ_CFG_MAX + 1];
    if (api->size >= APP_API_SLOT(58)) { /* API 2.11: the firmware owns the default printer */
        int n = pj_device_text(api, buf, sizeof(buf));
        if (n < 0) return n;
        return pj_cfg_parse(buf, (size_t)n, out);
    }
    if (api->size < APP_API_SLOT(53)) return APP_ENOTSUP;
    app_file_t *f = api->file_open_app(PJ_CFG_APP, PJ_CFG_FILE);
    if (!f) return PJ_NO_PRINTER;
    size_t len = 0;
    int k = 0;
    while (len < PJ_CFG_MAX && (k = api->file_read(f, buf + len, PJ_CFG_MAX - len)) > 0) len += (size_t)k;
    api->file_close(f);
    if (k < 0) return APP_EIO;
    return pj_cfg_parse(buf, len, out);
}
