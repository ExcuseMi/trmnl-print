/* SPDX-License-Identifier: MIT */
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
    const char *group;
} scan_t;

static bool on_file(const char *name, bool is_dir, unsigned long size, void *ctx)
{
    scan_t *s = ctx;
    if (is_dir) return true;
    int k = kind_of(name);
    if (k < 0) return true;
    if (s->l->n >= FL_MAX || strlen(name) >= FL_NAME) {
        s->l->dropped++;
        return true;
    }
    fl_entry_t *e = &s->l->e[s->l->n++];
    memcpy(e->group, s->group, strlen(s->group) + 1);
    memcpy(e->name, name, strlen(name) + 1);
    e->kind = (unsigned char)k;
    e->size = size;
    return true;
}

typedef struct {
    char g[16][FL_GROUP];
    int n, dropped;
} groups_t;

static bool on_root(const char *name, bool is_dir, unsigned long size, void *ctx)
{
    (void)size;
    groups_t *g = ctx;
    if (!is_dir) return true;
    if (g->n >= 16 || strlen(name) >= FL_GROUP) {
        g->dropped++;
        return true;
    }
    memcpy(g->g[g->n++], name, strlen(name) + 1);
    return true;
}

int fl_scan(fl_t *l, const fl_fs_t *fs)
{
    memset(l, 0, sizeof(*l));
    groups_t g;
    memset(&g, 0, sizeof(g));
    int rc = fs->list(fs->u, "", on_root, &g);
    if (rc < 0) return rc;
    l->dropped += g.dropped;
    for (int i = 0; i < g.n; i++) {
        scan_t s = { l, g.g[i] };
        if (fs->list(fs->u, g.g[i], on_file, &s) < 0) l->dropped++; /* one unreadable folder does not hide the others */
    }
    fl_sort(l);
    return 0;
}

static int cmp(const void *pa, const void *pb)
{
    const fl_entry_t *a = pa, *b = pb;
    int c = fl_natcmp(a->group, b->group);
    return c ? c : fl_natcmp(b->name, a->name);
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
