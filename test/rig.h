/* The controller's test rig: files, a printer that records, the printer.cfg file, a network search with set results. SPDX-License-Identifier: MIT */
#ifndef RIG_H
#define RIG_H
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pr.h"
#include "tbfw.h"
#include "util.h"

typedef struct {
    ffs_t fs;
    printer_t pr;
    char cfg[PJ_CFG_MAX]; /* the printer.cfg file */
    int cfg_n, cfg_fail_write, cfg_writes;
    struct { const char *name, *host, *path; int port; } br[10]; /* what the network search finds */
    int nbr, br_rc, browses;
    int64_t now;
    char printer[64], format[8]; /* the phone page's settings */
    pr_env_t env;
} rig_t;

static int rig_cfg(void *u, const char *k, char *b, int cap)
{
    rig_t *r = u;
    const char *v = !strcmp(k, "printer") ? r->printer : !strcmp(k, "format") ? r->format : "";
    if (!v[0]) return -1;
    int n = (int)strlen(v);
    if (n > cap) n = cap;
    memcpy(b, v, (size_t)n);
    return n;
}
static int rig_cfg_read(void *u, char *b, size_t cap)
{
    rig_t *r = u;
    if (cap < PJ_CFG_BUF) return -1; /* as prcfg_read on API 2.11 (pj_device_text) */
    if (r->cfg_n <= 0) return -1;
    size_t n = (size_t)r->cfg_n < cap ? (size_t)r->cfg_n : cap;
    memcpy(b, r->cfg, n);
    return (int)n;
}
static int rig_cfg_write(void *u, const char *t, size_t n)
{
    rig_t *r = u;
    r->cfg_writes++;
    if (r->cfg_fail_write || n >= sizeof(r->cfg)) return -1;
    memcpy(r->cfg, t, n);
    r->cfg_n = (int)n;
    return 0;
}
static int rig_browse(void *u, app_browse_cb_t cb, void *ctx)
{
    rig_t *r = u;
    r->browses++;
    if (r->br_rc < 0) return r->br_rc;
    for (int i = 0; i < r->nbr; i++) cb(r->br[i].name, r->br[i].host, (uint16_t)r->br[i].port, r->br[i].path, ctx);
    return r->nbr;
}
static int64_t rig_now(void *u) { return ((rig_t *)u)->now; }
/* The default printer as another app would have written it by choosing 10.0.0.7 */
static void rig_default(rig_t *r, const char *text)
{
    r->cfg_n = (int)strlen(text);
    memcpy(r->cfg, text, (size_t)r->cfg_n);
}
static int rig_read(void *u, const fl_entry_t *e, uint8_t **b, size_t *sz) { return ffs_read(&((rig_t *)u)->fs, e, b, sz); }

static void paint(uint8_t *px, int stride, int w, int h)
{
    for (int i = 0; i < w && i < h; i++) set_ink(px, stride, i, i);
}

static void rig_new(rig_t *r)
{
    memset(r, 0, sizeof(*r));
    printer_init(&r->pr);
    size_t n;
    uint8_t *png = make_png(400, 300, paint, &n);
    for (int i = 1; i <= 12; i++) {
        char p[64];
        snprintf(p, sizeof(p), "sketch/sk%04d.png", i);
        ffs_put(&r->fs, p, png, n);
    }
    free(png);
    ffs_put(&r->fs, "writer/diary.md", "# Diary\n\nToday I wrote a few lines.\n", 36);
    ffs_put(&r->fs, "writer/broken.txt", "x", 1);
    r->fs.f[r->fs.count - 1].unreadable = 1;
    ffs_put(&r->fs, "sketch/sk0099.png", "this is not a png at all, just text", 35);
    r->env.files = (fl_fs_t){ &r->fs, ffs_list, 0 };
    r->env.read = rig_read;
    r->env.cfg_get = rig_cfg;
    r->env.net = (pj_net_t){ PJ_PWG, fake_post, &r->pr };
    r->env.cfg_read = rig_cfg_read;
    r->env.cfg_write = rig_cfg_write;
    r->env.browse = rig_browse;
    r->env.now = rig_now;
    r->now = 1793534400; /* 2026-11-01 12:00 UTC */
    rig_default(r, "name=Test printer\nhost=10.0.0.7\n");
    r->env.u = r;
}
static void rig_free(rig_t *r)
{
    ffs_free(&r->fs);
    printer_free(&r->pr);
}

static int find_sel(pr_t *p, const char *name)
{
    for (int i = 0; i < p->list.n; i++)
        if (!strcmp(p->list.e[i].name, name)) return i + 1;
    return -1;
}

static void tap(pr_t *p, int zone) { pr_touch(p, zone, APP_GESTURE_TAP); }


#endif
