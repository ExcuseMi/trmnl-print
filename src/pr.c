/* SPDX-License-Identifier: MIT */
#include "pr.h"
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "pngio.h"

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
    int n = p->env->cfg_get(p->env->u, "printer", out, (int)cap - 1);
    if (n <= 0 || (size_t)n >= cap) {
        size_t d = strlen(PR_DEFAULT_PRINTER);
        memcpy(out, PR_DEFAULT_PRINTER, d + 1);
        return;
    }
    out[n] = 0;
}

static void open_preview(pr_t *p)
{
    drop_data(p);
    const fl_entry_t *e = &p->list.e[p->sel - 1];
    uint8_t *buf = 0;
    size_t n = 0;
    if (p->env->read(p->env->u, e->group, e->name, &buf, &n) < 0 || !buf) {
        free(buf);
        set_note(p, "That file could not be read");
        p->mode = PR_LIST;
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
            p->mode = PR_LIST;
            return;
        }
        p->px = malloc((size_t)((w + 7) / 8) * (size_t)h);
        if (!p->px || png_decode(buf, n, p->px, w, h, (w + 7) / 8) < 0) {
            drop_data(p);
            set_note(p, "Not a 1-bit PNG (only Sketch pictures are read)");
            p->mode = PR_LIST;
            return;
        }
        p->pw = w;
        p->ph = h;
    }
    p->note[0] = 0;
    p->mode = PR_PREVIEW;
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
            if (p->sel == 0) p->mode = PR_SETTINGS, p->settings_req = 1;
            else open_preview(p);
        }
        break;
    case PR_PREVIEW:
        if (gesture != APP_GESTURE_TAP) break;
        if (zone == APP_TOUCH_LEFT) drop_data(p), p->mode = PR_LIST;
        else if (zone == APP_TOUCH_MIDDLE) p->mode = PR_PRINTING, p->print_req = 1;
        else if (zone == APP_TOUCH_RIGHT && p->sel < p->list.n) p->sel++, open_preview(p);
        break;
    case PR_RESULT:
        if (gesture == APP_GESTURE_TAP) p->mode = p->data ? PR_PREVIEW : PR_LIST;
        break;
    case PR_SETTINGS:
        if (gesture == APP_GESTURE_TAP) p->mode = PR_LIST, p->settings_req = -1;
        break;
    default: break;
    }
}

void pr_run_print(pr_t *p)
{
    p->print_req = 0;
    char printer[80];
    pr_printer(p, printer, sizeof(printer));
    const fl_entry_t *e = &p->list.e[p->sel - 1];
    int rc = e->kind == FL_PNG ? pj_print_png(&p->env->net, printer, p->data, p->data_n, &p->res)
                               : pj_print_text(&p->env->net, printer, (const char *)p->data, p->data_n, &p->res);
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

void pr_set_share(pr_t *p, int rc, const app_webshare_status_t *st)
{
    memset(p->share, 0, sizeof(p->share));
    size_t cap = sizeof(p->share[0]);
    if (rc != 0) {
        put_s(p->share[0], cap, rc == APP_ENOMEM ? "Not enough memory for the Wi-Fi page." : rc == APP_ENONET ? "No Wi-Fi network could be started." : "The Wi-Fi page could not start.");
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
