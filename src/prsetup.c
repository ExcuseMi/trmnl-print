/* The Print app's printer setup: finding printers on the network, the default printer file, the phone fallback and the
   test page. SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <string.h>
#include "pr.h"
#include "strbuf.h"

static void note(pr_t *p, const char *s) { sb_set(p->note, sizeof(p->note), s); }

void prs_load(pr_t *p)
{
    char buf[PJ_CFG_BUF]; /* prcfg_read refuses a smaller one: the default never loaded (2026-11-02) */
    p->have_def = 0;
    memset(&p->def, 0, sizeof(p->def));
    if (!p->env->cfg_read) return;
    int n = p->env->cfg_read(p->env->u, buf, sizeof(buf));
    if (n > 0 && pj_cfg_parse(buf, (size_t)n, &p->def) == 0) p->have_def = 1;
    else memset(&p->def, 0, sizeof(p->def));
}

static int save(pr_t *p, const pj_printer_t *pr)
{
    char buf[PJ_CFG_MAX];
    int n = pj_cfg_format(pr, buf, sizeof(buf));
    if (n < 0 || !p->env->cfg_write || p->env->cfg_write(p->env->u, buf, (size_t)n) < 0) return -1;
    p->def = *pr;
    p->have_def = 1;
    return 0;
}

int prs_items(const pr_t *p) { return p->nfound + 2 + (p->have_def ? 1 : 0) + 1; }

int prs_kind(const pr_t *p, int i)
{
    if (i < p->nfound) return PK_FOUND;
    int k = i - p->nfound;
    if (k == 0) return PK_FIND;
    if (k == 1) return PK_PHONE;
    return p->have_def && k == 2 ? PK_TEST : PK_BACK;
}

typedef struct {
    pr_t *p;
} found_ctx_t;

static void on_found(const char *name, const char *host, uint16_t port, const char *path, void *ctx)
{
    pr_t *p = ((found_ctx_t *)ctx)->p;
    if (p->nfound >= PR_FOUND_MAX || !host || !port) return;
    for (int i = 0; i < p->nfound; i++)
        if (p->found[i].port == port && !strcmp(p->found[i].host, host)) return;
    pj_printer_t *f = &p->found[p->nfound];
    memset(f, 0, sizeof(*f));
    if (strlen(host) >= sizeof(f->host)) return;
    sb_set(f->name, sizeof(f->name), name ? name : "");
    memcpy(f->host, host, strlen(host) + 1);
    f->port = port;
    sb_set(f->path, sizeof(f->path), path ? path : "");
    f->format = p->have_def ? p->def.format : PJ_PWG;
    p->nfound++;
}

void prs_search(pr_t *p)
{
    found_ctx_t c = { p };
    int rc = p->env->browse ? p->env->browse(p->env->u, on_found, &c) : -5;
    if (rc == APP_ENONET) note(p, "No Wi-Fi network. Join one on the Wi-Fi page first.");
    else if (rc == APP_EBUSY) note(p, "The radio is busy. Try again in a moment.");
    else if (rc < 0) note(p, "Could not search the network. Enter the address on the phone.");
    else if (p->nfound == 0) note(p, "No printer found. Is it on, and on this Wi-Fi? Or enter its address on the phone.");
}

void prs_open(pr_t *p, int back_to)
{
    p->pick_back = back_to;
    p->psel = 0;
    p->nfound = 0;
    p->test = 0;
    p->note[0] = 0;
    if (p->have_def) p->mode = PR_PICK;
    else p->mode = PR_FINDING, p->find_req = 1; /* nothing is set up: look at once */
}

/* The setup is done (a printer chosen, or Back): the caller of "setup:" gets the screen back, else where we came from */
static void finish(pr_t *p)
{
    if (p->setup) p->back_req = 1;
    else p->mode = p->pick_back == PR_PREVIEW && !p->data ? PR_LIST : p->pick_back;
}

static void choose(pr_t *p, int i)
{
    pj_printer_t c = p->found[i];
    if (save(p, &c) < 0) {
        note(p, "The printer could not be saved");
        return;
    }
    p->note[0] = 0;
    finish(p);
}

void prs_touch(pr_t *p, int zone, int gesture)
{
    if (gesture != APP_GESTURE_TAP) return;
    if (p->mode == PR_TESTASK) {
        if (zone == APP_TOUCH_LEFT) p->test_exit ? (void)(p->exit_req = 1) : (void)(p->mode = PR_PICK);
        else if (zone == APP_TOUCH_MIDDLE) p->test = 1, p->mode = PR_PRINTING, p->print_req = 1;
        return;
    }
    int total = prs_items(p);
    if (zone == APP_TOUCH_LEFT && p->psel > 0) p->psel--;
    else if (zone == APP_TOUCH_RIGHT && p->psel + 1 < total) p->psel++;
    else if (zone == APP_TOUCH_MIDDLE) {
        switch (prs_kind(p, p->psel)) {
        case PK_FOUND: choose(p, p->psel); break;
        case PK_FIND: p->mode = PR_FINDING, p->find_req = 1; break;
        case PK_PHONE:
            p->snap[0][0] = p->snap[1][0] = 0;
            p->env->cfg_get(p->env->u, "printer", p->snap[0], (int)sizeof(p->snap[0]) - 1);
            p->env->cfg_get(p->env->u, "format", p->snap[1], (int)sizeof(p->snap[1]) - 1);
            p->mode = PR_SETTINGS, p->settings_req = 1;
            break;
        case PK_TEST: p->mode = PR_TESTASK; break;
        default: finish(p); break;
        }
    }
}

static void read_setting(const pr_t *p, const char *k, char *out, size_t cap)
{
    out[0] = 0;
    int n = p->env->cfg_get(p->env->u, k, out, (int)cap - 1);
    if (n < 0) n = 0;
    if ((size_t)n >= cap) n = (int)cap - 1;
    out[n] = 0;
}

void prs_settings_closed(pr_t *p)
{
    char a[80], f[80];
    read_setting(p, "printer", a, sizeof(a));
    read_setting(p, "format", f, sizeof(f));
    int addr_changed = a[0] && strcmp(a, p->snap[0]) != 0;
    int fmt_changed = strcmp(f, p->snap[1]) != 0;
    p->mode = PR_PICK;
    if (!addr_changed && !fmt_changed) return;
    pj_printer_t n;
    if (addr_changed) {
        if (pj_printer_parse(a, &n) < 0) {
            note(p, "That address is not valid. Use 192.168.1.20 or 192.168.1.20:631");
            return;
        }
        n.format = p->have_def ? p->def.format : PJ_PWG;
    } else {
        if (!p->have_def) return; /* a format without a printer: nothing to change yet */
        n = p->def;
    }
    if (fmt_changed) n.format = !strcmp(f, "pcl") ? PJ_PCL : PJ_PWG;
    if (save(p, &n) < 0) {
        note(p, "The printer could not be saved");
        return;
    }
    p->note[0] = 0;
    finish(p);
}

/* ---- the test page: a title, the date and a box on a small sheet ---- */

#define TP_W 400
#define TP_H 240

static void date_str(int64_t t, char *out, size_t cap)
{
    if (t <= 0) {
        sb_set(out, cap, "date unknown");
        return;
    }
    int64_t days = t / 86400, rem = t % 86400;
    int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
    out[0] = 0;
    sb_int(out, cap, y + (m <= 2), 4), sb_cat(out, cap, "-"), sb_int(out, cap, m, 2), sb_cat(out, cap, "-"), sb_int(out, cap, d, 2);
    sb_cat(out, cap, " "), sb_int(out, cap, rem / 3600, 2), sb_cat(out, cap, ":"), sb_int(out, cap, rem % 3600 / 60, 2), sb_cat(out, cap, " UTC");
}

int prs_print_test(pr_t *p)
{
    memset(&p->res, 0, sizeof(p->res));
    if (!p->have_def) return sb_set(p->res.msg, sizeof(p->res.msg), "No printer yet: set one up first"), -1;
    uint8_t *px = malloc((size_t)(TP_W / 8) * TP_H);
    if (!px) return sb_set(p->res.msg, sizeof(p->res.msg), "Not enough memory to print"), -1;
    gfx_t g;
    gfx_init(&g, px, TP_W, TP_H, TP_W / 8, APP_ORIENT_BAR_BOTTOM);
    gfx_fill(&g, GFX_WHITE);
    gfx_rrect(&g, 8, 8, TP_W - 9, TP_H - 9, 0, 4, GFX_WHITE, GFX_BLACK);
    gfx_text(&g, font_get(FONT_BOLD, 46), 24 * 64, 80, "tiny-paper", 0, GFX_BLACK);
    gfx_text(&g, font_get(FONT_MEDIUM, 34), 24 * 64, 130, "Test page", 0, GFX_BLACK);
    char d[40];
    date_str(p->env->now ? p->env->now(p->env->u) : 0, d, sizeof(d));
    gfx_text(&g, font_get(FONT_MEDIUM, 34), 24 * 64, 180, d, 0, GFX_BLACK);
    pj_net_t net = p->env->net;
    net.format = p->def.format;
    int rc = pj_print_bitmap(&net, &p->def, px, TP_W, TP_H, TP_W / 8, &p->res);
    free(px);
    return rc;
}
