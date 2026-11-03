/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "flist.h"
#include <string.h>

static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int fl_natcmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (is_digit((unsigned char)*a) && is_digit((unsigned char)*b)) {
            while (*a == '0' && is_digit((unsigned char)a[1])) a++;
            while (*b == '0' && is_digit((unsigned char)b[1])) b++;
            const char *ea = a, *eb = b;
            while (is_digit((unsigned char)*ea)) ea++;
            while (is_digit((unsigned char)*eb)) eb++;
            if (ea - a != eb - b) return (ea - a) < (eb - b) ? -1 : 1;
            for (; a < ea; a++, b++)
                if (*a != *b) return *a < *b ? -1 : 1;
            continue;
        }
        int x = lower(*a), y = lower(*b);
        if (x != y) return x < y ? -1 : 1;
        a++, b++;
    }
    return *a ? 1 : *b ? -1 : 0;
}

static int kind_of(const char *n)
{
    size_t l = strlen(n);
    if (l > 4 && !strcmp(n + l - 4, ".png")) {
        if (l > 12 && (!strcmp(n + l - 12, "-cleared.png"))) return -1;
        if (l > 8 && (!strcmp(n + l - 8, "-bad.png"))) return -1;
        return FL_PNG;
    }
    if ((l > 3 && !strcmp(n + l - 3, ".md")) || (l > 4 && !strcmp(n + l - 4, ".txt"))) return FL_TEXT;
    return -1;
}

typedef struct {
    fl_t *l;
    const char *group, *app, *dir, *pre; /* dir: what is listed; pre: the folders in front of the name */
    char sub[8][FL_NAME]; /* folders met, searched after this listing (no listing inside a callback) */
    int nsub;
} scan_t;

static void add(fl_t *l, const char *group, const char *app, const char *dir, const char *name, unsigned long size, int64_t mtime, int k)
{
    size_t dl = dir[0] ? strlen(dir) + 1 : 0, nl = strlen(name);
    if (l->n >= FL_MAX || dl + nl >= FL_NAME) {
        l->dropped++;
        return;
    }
    fl_entry_t *e = &l->e[l->n++];
    memset(e, 0, sizeof(*e));
    memcpy(e->group, group, strlen(group) + 1);
    memcpy(e->app, app, strlen(app) + 1);
    if (dl) memcpy(e->name, dir, dl - 1), e->name[dl - 1] = '/';
    memcpy(e->name + dl, name, nl + 1);
    e->kind = (unsigned char)k;
    e->size = size;
    e->mtime = mtime;
}

static bool on_file(const char *name, bool is_dir, unsigned long size, int64_t mtime, void *ctx)
{
    scan_t *s = ctx;
    if (is_dir) {
        size_t dl = s->pre[0] ? strlen(s->pre) + 1 : 0;
        if (s->nsub < 8 && dl + strlen(name) < FL_NAME) {
            char *d = s->sub[s->nsub++];
            if (dl) memcpy(d, s->pre, dl - 1), d[dl - 1] = '/';
            memcpy(d + dl, name, strlen(name) + 1);
        } else {
            s->l->dropped++;
        }
        return true;
    }
    int k = kind_of(name);
    if (k >= 0) add(s->l, s->group, s->app, s->pre, name, size, mtime, k);
    return true;
}

/* The files of app `app` (or the own folder's group) under dir, and the folders below it down to `depth` */
static void scan_dir(fl_t *l, const fl_fs_t *fs, const char *group, const char *app, const char *dir, int depth, int sub_folders)
{
    scan_t s;
    memset(&s, 0, sizeof(s));
    s.l = l, s.group = group, s.app = app, s.dir = dir, s.pre = sub_folders ? dir : "";
    if (fs->list(fs->u, app, dir, on_file, &s) < 0) {
        l->dropped++; /* one unreadable folder does not hide the others */
        return;
    }
    if (!sub_folders || depth >= FL_DEPTH) return;
    for (int i = 0; i < s.nsub; i++) scan_dir(l, fs, group, app, s.sub[i], depth + 1, 1);
}

typedef struct {
    char g[24][FL_APP];
    int n, dropped;
} groups_t;

static bool on_root(const char *name, bool is_dir, unsigned long size, int64_t mtime, void *ctx)
{
    (void)is_dir, (void)size, (void)mtime;
    groups_t *g = ctx;
    if (g->n >= 24 || strlen(name) >= FL_APP) {
        g->dropped++;
        return true;
    }
    memcpy(g->g[g->n++], name, strlen(name) + 1);
    return true;
}

static bool on_own_root(const char *name, bool is_dir, unsigned long size, int64_t mtime, void *ctx)
{
    return is_dir ? on_root(name, 1, size, mtime, ctx) : true;
}

int fl_scan(fl_t *l, const fl_fs_t *fs)
{
    memset(l, 0, sizeof(*l));
    groups_t g;
    memset(&g, 0, sizeof(g));
    int rc = fs->list_apps ? fs->list_apps(fs->u, on_root, &g) : fs->list(fs->u, "", "", on_own_root, &g);
    if (rc < 0) return rc;
    l->dropped += g.dropped;
    for (int i = 0; i < g.n; i++) {
        if (fs->list_apps) {
            const char *shown = !memcmp(g.g[i], "trmnl-", 6) && g.g[i][6] ? g.g[i] + 6 : g.g[i];
            char grp[FL_GROUP];
            size_t n = strlen(shown) >= FL_GROUP ? FL_GROUP - 1 : strlen(shown);
            memcpy(grp, shown, n), grp[n] = 0;
            scan_dir(l, fs, grp, g.g[i], "", 0, 1);
        } else {
            scan_dir(l, fs, g.g[i], "", g.g[i], 0, 0);
        }
    }
    fl_sort(l);
    return 0;
}

static int cmp(const void *pa, const void *pb)
{
    const fl_entry_t *a = pa, *b = pb;
    int c = fl_natcmp(a->group, b->group);
    if (c) return c;
    if (a->mtime != b->mtime) return a->mtime < b->mtime ? 1 : -1; /* newest first; unknown (0) last */
    return fl_natcmp(b->name, a->name);
}

/* Insertion sort: the loader has no qsort, and the list is short */
void fl_sort(fl_t *l)
{
    for (int i = 1; i < l->n; i++) {
        fl_entry_t t = l->e[i];
        int j = i - 1;
        while (j >= 0 && cmp(&l->e[j], &t) > 0) l->e[j + 1] = l->e[j], j--;
        l->e[j + 1] = t;
    }
}
