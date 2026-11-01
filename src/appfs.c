/* SPDX-License-Identifier: MIT */
#include "appfs.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    fl_cb_t cb;
    void *u;
} ctx_t;

static bool on_plain(const char *name, bool is_dir, uint32_t size, void *ctx)
{
    ctx_t *c = ctx;
    return c->cb(name, is_dir, size, 0, c->u);
}
static bool on_ex(const char *name, bool is_dir, uint32_t size, int64_t mtime, void *ctx)
{
    ctx_t *c = ctx;
    return c->cb(name, is_dir, size, mtime, c->u);
}

static int f_list(void *u, const char *app, const char *dir, fl_cb_t cb, void *cx)
{
    appfs_t *a = u;
    ctx_t c = { cb, cx };
    return app[0] ? a->api->file_list_app(app, dir, on_ex, &c) : a->api->file_list(dir, on_plain, &c);
}

static int f_list_apps(void *u, fl_cb_t cb, void *cx)
{
    appfs_t *a = u;
    ctx_t c = { cb, cx };
    return a->api->file_list_apps(on_plain, &c);
}

void appfs_init(appfs_t *a, const app_api_t *api)
{
    a->api = api;
    a->fs.u = a;
    a->fs.list = f_list;
    a->fs.list_apps = api->size >= APP_API_SLOT(53) ? f_list_apps : 0;
}

int appfs_read(void *u, const fl_entry_t *e, uint8_t **buf, size_t *n)
{
    appfs_t *a = u;
    app_file_t *f;
    if (e->app[0]) {
        if (a->api->size < APP_API_SLOT(53)) return -1;
        f = a->api->file_open_app(e->app, e->name);
    } else {
        char path[FL_GROUP + FL_NAME + 2];
        size_t g = strlen(e->group), m = strlen(e->name);
        if (g + m + 2 > sizeof(path)) return -1;
        memcpy(path, e->group, g);
        path[g] = '/';
        memcpy(path + g + 1, e->name, m + 1);
        f = a->api->file_open(path, APP_FILE_READ);
    }
    if (!f) return -1;
    size_t cap = 16 * 1024, len = 0;
    uint8_t *b = malloc(cap);
    int rc = b ? 0 : -1;
    while (rc == 0 && len < APPFS_READ_MAX) {
        if (len == cap) {
            uint8_t *nb = realloc(b, cap *= 2);
            if (!nb) {
                rc = -1;
                break;
            }
            b = nb;
        }
        size_t want = cap - len;
        if (len + want > APPFS_READ_MAX) want = APPFS_READ_MAX - len;
        int k = a->api->file_read(f, b + len, want);
        if (k < 0) rc = -1;
        else if (k == 0) break;
        else len += (size_t)k;
    }
    a->api->file_close(f);
    if (rc < 0) return free(b), -1;
    *buf = b;
    *n = len;
    return 0;
}
