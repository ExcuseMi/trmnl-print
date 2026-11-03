/* Other apps' files through a fake of the firmware's API 2.8 calls: grouping, newest first, depth, filters, reading, and the
   fallback to the own folder on older firmware. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "appfs.h"

typedef struct {
    const char *app, *path;
    uint32_t size;
    int64_t mtime;
    int fail_read;
} node_t;

static node_t tree[] = {
    { "trmnl-sketch", "sketch/sk0001.png", 100, 1000, 0 },
    { "trmnl-sketch", "sketch/sk0002.png", 100, 3000, 0 },
    { "trmnl-sketch", "sketch/sk0003.png", 100, 2000, 0 },
    { "trmnl-sketch", "sketch/sk0003-bad.png", 100, 5000, 0 },
    { "trmnl-sketch", "sketch/sk0004-cleared.png", 100, 5000, 0 },
    { "trmnl-writer", "documents/diary/day1.md", 40, 50, 0 },
    { "trmnl-writer", "documents/diary/day2.md", 40, 60, 0 },
    { "trmnl-writer", "documents/a/b/deep.md", 40, 70, 0 },
    { "trmnl-writer", "settings/settings.json", 40, 70, 0 },
    { "gameboy", "shots/g1.png", 80, 0, 0 },
    { "gameboy", "shots/g2.png", 80, 0, 0 },
    { "gameboy", "shots/mixed-known.png", 80, 9, 0 },
    { "trmnl-fpv", "logs/flight.txt", 10, 77, 1 },
    { "trmnl-print", "own/mine.txt", 10, 5, 0 },
};
#define NT ((int)(sizeof(tree) / sizeof(tree[0])))

static int calls_old_api; /* a call that old firmware does not have */
static const char *own_files[] = { "sketch/own1.png", "sketch/own2.png" };
static int hide_app; /* file_list_app says no such folder for this tree entry's app: "trmnl-fpv" */

static int fa_list_apps(app_list_cb_t cb, void *ctx)
{
    const char *seen[16];
    int ns = 0, n = 0;
    for (int i = 0; i < NT; i++) {
        int dup = 0;
        for (int k = 0; k < ns; k++) dup |= !strcmp(seen[k], tree[i].app);
        if (dup) continue;
        seen[ns++] = tree[i].app;
        n++;
        if (!cb(tree[i].app, true, 0, ctx)) break;
    }
    return n;
}

static int fa_list_app(const char *app, const char *dir, app_list_ex_cb_t cb, void *ctx)
{
    if (hide_app && !strcmp(app, "trmnl-fpv")) return APP_ENOENT;
    size_t dl = strlen(dir);
    char seen[16][40];
    int ns = 0, n = 0;
    for (int i = 0; i < NT; i++) {
        if (strcmp(tree[i].app, app)) continue;
        const char *p = tree[i].path;
        if (dl) {
            if (strncmp(p, dir, dl) || p[dl] != '/') continue;
            p += dl + 1;
        }
        const char *sl = strchr(p, '/');
        if (!sl) {
            n++;
            if (!cb(p, false, tree[i].size, tree[i].mtime, ctx)) return n;
            continue;
        }
        char d[40] = { 0 };
        memcpy(d, p, (size_t)(sl - p));
        int dup = 0;
        for (int k = 0; k < ns; k++) dup |= !strcmp(seen[k], d);
        if (dup) continue;
        strcpy(seen[ns++], d);
        n++;
        if (!cb(d, true, 0, 0, ctx)) return n;
    }
    return n;
}

typedef struct {
    int idx;
    size_t pos;
    size_t total;
} handle_t;
static handle_t handles[4];
static int open_count, close_count;

static app_file_t *fa_open_app(const char *app, const char *path)
{
    for (int i = 0; i < NT; i++)
        if (!strcmp(tree[i].app, app) && !strcmp(tree[i].path, path)) {
            handle_t *h = &handles[open_count++ % 4];
            h->idx = i, h->pos = 0, h->total = tree[i].size;
            return (app_file_t *)h;
        }
    return 0;
}

static app_file_t *fa_open_own(const char *path, int mode)
{
    (void)mode;
    for (size_t i = 0; i < sizeof(own_files) / sizeof(own_files[0]); i++)
        if (!strcmp(own_files[i], path)) {
            handle_t *h = &handles[open_count++ % 4];
            h->idx = -1 - (int)i, h->pos = 0, h->total = 7;
            return (app_file_t *)h;
        }
    return 0;
}

static int fa_read(app_file_t *f, void *buf, size_t len)
{
    handle_t *h = (handle_t *)f;
    if (h->idx >= 0 && tree[h->idx].fail_read) return APP_EIO;
    size_t k = h->total - h->pos < len ? h->total - h->pos : len;
    memset(buf, h->idx >= 0 ? 'A' + h->idx : 'o', k);
    h->pos += k;
    return (int)k;
}
static int fa_close(app_file_t *f) { return (void)f, close_count++, 0; }

static int fa_list_own(const char *dir, app_list_cb_t cb, void *ctx)
{
    if (!dir[0]) return cb("sketch", true, 0, ctx), 1;
    if (strcmp(dir, "sketch")) return APP_ENOENT;
    for (size_t i = 0; i < sizeof(own_files) / sizeof(own_files[0]); i++) cb(own_files[i] + 7, false, 7, ctx);
    return 2;
}
static int fa_boom_apps(app_list_cb_t cb, void *ctx) { return (void)cb, (void)ctx, calls_old_api++, 0; }
static int fa_boom_app(const char *a, const char *d, app_list_ex_cb_t cb, void *ctx) { return (void)a, (void)d, (void)cb, (void)ctx, calls_old_api++, 0; }
static app_file_t *fa_boom_open(const char *a, const char *p) { return (void)a, (void)p, calls_old_api++, (app_file_t *)0; }

static void make_api(app_api_t *api, size_t slots)
{
    memset(api, 0, sizeof(*api));
    api->size = (uint32_t)APP_API_SLOT(slots);
    api->file_open = fa_open_own;
    api->file_read = fa_read;
    api->file_close = fa_close;
    api->file_list = fa_list_own;
    api->file_list_apps = slots >= 53 ? fa_list_apps : fa_boom_apps;
    api->file_list_app = slots >= 53 ? fa_list_app : fa_boom_app;
    api->file_open_app = slots >= 53 ? fa_open_app : fa_boom_open;
}

static const fl_entry_t *find(const fl_t *l, const char *name)
{
    for (int i = 0; i < l->n; i++)
        if (!strcmp(l->e[i].name, name)) return &l->e[i];
    return 0;
}

static void grouping_and_order(void)
{
    static app_api_t api;
    static appfs_t a;
    static fl_t l;
    make_api(&api, 53);
    appfs_init(&a, &api);
    CHECK(a.fs.list_apps != 0);
    CHECK(fl_scan(&l, &a.fs) == 0 && l.dropped == 0);
    /* groups alphabetical by what they show ("trmnl-" left off); newest first by time, no time: by name, highest first, last */
    const char *want[][2] = {
        { "fpv", "logs/flight.txt" }, { "gameboy", "shots/mixed-known.png" }, { "gameboy", "shots/g2.png" }, { "gameboy", "shots/g1.png" },
        { "print", "own/mine.txt" }, { "sketch", "sketch/sk0002.png" }, { "sketch", "sketch/sk0003.png" }, { "sketch", "sketch/sk0001.png" },
        { "writer", "documents/diary/day2.md" }, { "writer", "documents/diary/day1.md" },
    };
    CHECK(l.n == 10);
    for (int i = 0; i < 10 && i < l.n; i++) {
        CHECK(!strcmp(l.e[i].group, want[i][0]));
        CHECK(!strcmp(l.e[i].name, want[i][1]));
    }
    const fl_entry_t *e = find(&l, "sketch/sk0002.png");
    CHECK(e && !strcmp(e->app, "trmnl-sketch") && e->mtime == 3000 && e->size == 100 && e->kind == FL_PNG);
    CHECK(find(&l, "documents/diary/day1.md")->kind == FL_TEXT);
    CHECK(!find(&l, "sketch/sk0003-bad.png") && !find(&l, "sketch/sk0004-cleared.png"));  /* the app's own copies */
    CHECK(!find(&l, "documents/a/b/deep.md"));                                         /* deeper than FL_DEPTH folders */
    CHECK(!find(&l, "settings/settings.json"));
    /* a folder that is gone: the others still list, and it is counted */
    hide_app = 1;
    CHECK(fl_scan(&l, &a.fs) == 0 && l.dropped == 1 && !find(&l, "logs/flight.txt") && l.n == 9);
    hide_app = 0;
}

static void reading(void)
{
    static app_api_t api;
    static appfs_t a;
    static fl_t l;
    make_api(&api, 53);
    appfs_init(&a, &api);
    fl_scan(&l, &a.fs);
    uint8_t *b = 0;
    size_t n = 0;
    open_count = close_count = 0;
    CHECK(appfs_read(&a, find(&l, "sketch/sk0002.png"), &b, &n) == 0 && n == 100 && b[0] == 'A' + 1 && b[99] == 'A' + 1);
    free(b);
    CHECK(close_count == 1);
    b = 0;
    CHECK(appfs_read(&a, find(&l, "logs/flight.txt"), &b, &n) < 0 && !b && close_count == 2); /* a read error: nothing kept, the file closed */
    fl_entry_t gone = *find(&l, "sketch/sk0002.png");
    strcpy(gone.name, "sketch/vanished.png");
    CHECK(appfs_read(&a, &gone, &b, &n) < 0);                                              /* the file is gone since the listing */
    tree[0].size = 1500000;                                                                  /* a big text is cut at the read limit */
    CHECK(appfs_read(&a, find(&l, "sketch/sk0001.png"), &b, &n) == 0 && n == APPFS_READ_MAX);
    free(b);
    tree[0].size = 100;
}

static void old_firmware(void)
{
    static app_api_t api;
    static appfs_t a;
    static fl_t l;
    make_api(&api, 50); /* API 2.7: no calls for other apps */
    appfs_init(&a, &api);
    CHECK(a.fs.list_apps == 0);
    calls_old_api = 0;
    CHECK(fl_scan(&l, &a.fs) == 0 && l.n == 2);
    CHECK(!strcmp(l.e[0].group, "sketch") && !strcmp(l.e[0].name, "own2.png") && l.e[0].app[0] == 0);   /* by name: no times in the own folder */
    uint8_t *b = 0;
    size_t n = 0;
    CHECK(appfs_read(&a, &l.e[1], &b, &n) == 0 && n == 7 && b[0] == 'o');
    free(b);
    fl_entry_t foreign = l.e[0];
    strcpy(foreign.app, "trmnl-sketch");
    CHECK(appfs_read(&a, &foreign, &b, &n) < 0);                 /* never calls what the firmware does not have */
    CHECK(calls_old_api == 0);
    /* one slot short of 2.8 is still old */
    make_api(&api, 52);
    appfs_init(&a, &api);
    CHECK(a.fs.list_apps == 0 && fl_scan(&l, &a.fs) == 0 && l.n == 2 && calls_old_api == 0);
}

int main(void)
{
    grouping_and_order();
    reading();
    old_firmware();
    return CHECK_DONE("test_appfs");
}
