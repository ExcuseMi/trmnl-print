/* SPDX-License-Identifier: MIT */
#include "pr.h"
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "pngio.h"
#include "strbuf.h"

static void set_note(pr_t *p, const char *s)
{
    size_t n = strlen(s);
    if (n >= sizeof(p->note)) n = sizeof(p->note) - 1;
    memcpy(p->note, s, n);
    p->note[n] = 0;
}

static void drop_data(pr_t *p)
{
    free(p->data);
    free(p->px);
    p->data = 0;
    p->px = 0;
    p->data_n = 0;
    p->pw = p->ph = 0;
    p->data_cut = 0;
}

void pr_init(pr_t *p, const pr_env_t *env)
{
    memset(p, 0, sizeof(*p));
    p->env = env;
    prs_load(p);
    pr_rescan(p);
}

void pr_free(pr_t *p) { drop_data(p); }

void pr_rescan(pr_t *p)
{
    p->note[0] = 0;
    if (fl_scan(&p->list, &p->env->files) < 0) {
        memset(&p->list, 0, sizeof(p->list));
        set_note(p, "The files could not be listed");
    }
    if (p->sel > p->list.n) p->sel = p->list.n;
}

void pr_printer(const pr_t *p, char *out, size_t cap)
{
    out[0] = 0;
    if (!p->have_def) return;
    char a[100];
    pj_printer_addr(&p->def, a, sizeof(a));
    if (p->def.name[0]) sb_set(out, cap, p->def.name), sb_cat(out, cap, "  "), sb_cat(out, cap, a);
    else sb_set(out, cap, a);
}

/* Back to where the person came from: the caller when there is one (the glue launches it), else the list */
static void go_back(pr_t *p)
{
    drop_data(p);
    if (p->caller[0]) p->back_req = 1;
    else p->mode = PR_LIST;
}

static void open_entry(pr_t *p, const fl_entry_t *e)
{
    drop_data(p);
    p->cur = *e;
    uint8_t *buf = 0;
    size_t n = 0;
    if (p->env->read(p->env->u, e, &buf, &n) < 0 || !buf) {
        free(buf);
        set_note(p, "That file could not be read");
        p->mode = p->caller[0] ? PR_RESULT : PR_LIST;
        return;
    }
    p->data = buf;
    p->data_n = n;
    p->data_cut = e->kind == FL_TEXT && e->size > n;
    if (e->kind == FL_PNG) {
        int w, h;
        if (png_dims(buf, n, &w, &h) < 0 || (size_t)((w + 7) / 8) * (size_t)h > IMG_MAX_BYTES) {
            drop_data(p);
            set_note(p, "That picture cannot be printed (not a PNG, or too big)");
            p->mode = p->caller[0] ? PR_RESULT : PR_LIST;
            return;
        }
        p->px = malloc((size_t)((w + 7) / 8) * (size_t)h);
        if (!p->px || png_decode(buf, n, p->px, w, h, (w + 7) / 8) < 0) {
            drop_data(p);
            set_note(p, "Not a 1-bit PNG (only Sketch pictures are read)");
            p->mode = p->caller[0] ? PR_RESULT : PR_LIST;
            return;
        }
        p->pw = w;
        p->ph = h;
    }
    p->note[0] = 0;
    p->mode = PR_PREVIEW;
}

static void open_preview(pr_t *p) { open_entry(p, &p->list.e[p->sel - 1]); }

static int name_ok(const char *a, size_t n)
{
    if (n == 0 || n >= FL_APP || n > 24) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = a[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return 0;
    }
    return 1;
}

static int path_ok(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= FL_NAME || s[0] == '/' || strchr(s, '\\')) return 0;
    for (const char *q = s; *q;) {
        const char *e = strchr(q, '/');
        size_t k = e ? (size_t)(e - q) : strlen(q);
        if (k == 0 || (k == 1 && q[0] == '.') || (k == 2 && q[0] == '.' && q[1] == '.')) return 0;
        q += k + (e ? 1 : 0);
        if (e && !*q) return 0;
    }
    return 1;
}

void pr_start_arg(pr_t *p, const char *arg)
{
    if (!arg || !arg[0]) return;
    if (!strcmp(arg, "test")) {
        p->test_exit = p->test = 1;
        if (p->have_def) p->mode = PR_TESTASK;
        else set_note(p, "No printer yet: set one up in Print"), p->mode = PR_RESULT;
        return;
    }
    if (strlen(arg) > 8 && !memcmp(arg, "printer:", 8)) { /* a default from the cable (TAPP RUN): the phone form may be out of reach */
        prs_set_default_text(p, arg + 8);
        return;
    }
    if (strlen(arg) >= 6 && !memcmp(arg, "setup:", 6)) { /* an app sends the person to set a printer up, and gets the screen back */
        size_t k = strlen(arg + 6);
        if (!name_ok(arg + 6, k)) {
            set_note(p, "Print was started with an argument it does not understand");
            return;
        }
        memcpy(p->caller, arg + 6, k + 1);
        p->setup = 1;
        prs_open(p, PR_LIST);
        return;
    }
    const char *colon = strchr(arg, ':');
    if (!colon || !name_ok(arg, (size_t)(colon - arg))) {
        set_note(p, "Print was started with an argument it does not understand");
        return;
    }
    memcpy(p->caller, arg, (size_t)(colon - arg));
    p->caller[colon - arg] = 0;
    const char *path = colon + 1;
    size_t n = strlen(path);
    int png = n > 4 && !strcmp(path + n - 4, ".png");
    int txt = n > 3 && (!strcmp(path + n - 3, ".md") || !strcmp(path + n - 4 > path ? path + n - 4 : path, ".txt"));
    if (!path_ok(path) || !(png || txt)) {
        set_note(p, "That file cannot be printed");
        p->mode = PR_RESULT;
        return;
    }
    fl_entry_t e;
    memset(&e, 0, sizeof(e));
    memcpy(e.app, p->caller, strlen(p->caller) + 1);
    memcpy(e.group, p->caller, strlen(p->caller) < FL_GROUP ? strlen(p->caller) + 1 : 0);
    memcpy(e.name, path, n + 1);
    e.kind = png ? FL_PNG : FL_TEXT;
    open_entry(p, &e);
    if (p->mode == PR_LIST) p->mode = PR_RESULT; /* open_entry fell back to the list: tell why, then go back */
}

void pr_touch(pr_t *p, int zone, int gesture)
{
    int total = p->list.n + 1;
    switch (p->mode) {
    case PR_LIST:
        if (gesture == APP_GESTURE_SWIPE_LEFT) p->sel = p->sel + PR_ROWS < total ? p->sel + PR_ROWS : total - 1;
        else if (gesture == APP_GESTURE_SWIPE_RIGHT) p->sel = p->sel >= PR_ROWS ? p->sel - PR_ROWS : 0;
        else if (gesture != APP_GESTURE_TAP) break;
        else if (zone == APP_TOUCH_LEFT && p->sel > 0) p->sel--;
        else if (zone == APP_TOUCH_RIGHT && p->sel + 1 < total) p->sel++;
        else if (zone == APP_TOUCH_MIDDLE) {
            if (p->sel == 0) prs_open(p, PR_LIST);
            else open_preview(p);
        }
        break;
    case PR_PREVIEW:
        if (gesture != APP_GESTURE_TAP) break;
        if (zone == APP_TOUCH_LEFT) {
            if (p->caller[0]) go_back(p);
            else drop_data(p), p->mode = PR_LIST;
        } else if (zone == APP_TOUCH_MIDDLE) {
            if (p->have_def) p->test = 0, p->mode = PR_PRINTING, p->print_req = 1;
            else prs_open(p, PR_PREVIEW); /* no printer yet: find one first, then back here to print */
        }
        else if (zone == APP_TOUCH_RIGHT && !p->caller[0] && p->sel < p->list.n) p->sel++, open_preview(p);
        break;
    case PR_RESULT:
        if (gesture != APP_GESTURE_TAP) break;
        if (p->test_exit) p->exit_req = 1;
        else if (p->test) p->test = 0, p->mode = PR_PICK;
        else if (p->caller[0]) go_back(p); /* printed, or not: back in the app the person came from */
        else p->mode = p->data ? PR_PREVIEW : PR_LIST;
        break;
    case PR_SETTINGS:
        if (gesture == APP_GESTURE_TAP) p->settings_req = -1, prs_settings_closed(p);
        break;
    case PR_PICK:
    case PR_TESTASK: prs_touch(p, zone, gesture); break;
    default: break;
    }
}

void pr_run_print(pr_t *p)
{
    p->print_req = 0;
    if (p->test) {
        prs_print_test(p);
        set_note(p, p->res.msg);
        p->mode = PR_RESULT;
        return;
    }
    const fl_entry_t *e = &p->cur;
    pj_net_t net = p->env->net;
    net.format = p->def.format;
    int rc = !p->have_def ? (sb_set(p->res.msg, sizeof(p->res.msg), "No printer yet: set one up first"), -1)
             : e->kind == FL_PNG ? pj_print_png(&net, &p->def, p->data, p->data_n, &p->res)
                                 : pj_print_text(&net, &p->def, (const char *)p->data, p->data_n, &p->res);
    (void)rc;
    set_note(p, p->res.msg);
    p->mode = PR_RESULT;
}

static void put_s(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(dst);
    while (*s && n + 1 < cap) dst[n++] = *s++;
    dst[n] = 0;
}

void pr_run_find(pr_t *p)
{
    p->find_req = 0;
    p->nfound = 0;
    p->psel = 0;
    p->note[0] = 0;
    prs_search(p);
    p->mode = PR_PICK;
}

void pr_set_share(pr_t *p, int rc, const app_webshare_status_t *st)
{
    memset(p->share, 0, sizeof(p->share));
    size_t cap = sizeof(p->share[0]);
    if (rc != 0) {
        put_s(p->share[0], cap, rc == APP_ENOMEM   ? "Not enough memory for the Wi-Fi page."
                                : rc == APP_ENONET ? "No Wi-Fi network could be started."
                                : rc == APP_ENOENT ? "The X refused this app's settings. Update the X or the app."
                                : rc == APP_EBUSY  ? "The Wi-Fi page is still closing. Try again in a moment."
                                                   : "The Wi-Fi page could not start.");
        return;
    }
    if (!st->running) {
        put_s(p->share[0], cap, "The page switched itself off. Touch the bar to leave.");
        return;
    }
    if (st->mode == APP_WEBSHARE_AP) {
        put_s(p->share[0], cap, "1. Join the Wi-Fi network \"");
        put_s(p->share[0], cap, st->network);
        put_s(p->share[0], cap, "\" with your phone.");
        put_s(p->share[1], cap, "2. Open this address, then type the code:");
    } else {
        put_s(p->share[0], cap, "1. With your phone on the same Wi-Fi open:");
        put_s(p->share[1], cap, "2. Then type the code:");
    }
    put_s(p->share[2], cap, st->address);
    put_s(p->share[3], cap, st->locked == 0 ? "Code: " : "Too many wrong codes: wait.");
    if (st->locked == 0) put_s(p->share[3], cap, st->code);
}
