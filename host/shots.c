/* make shots: the app's screens to PNG (shots/), and two PCL jobs (shots/sketch.pcl, shots/text.pcl) for a look with
   a PCL renderer (make render). No printer is involved. SPDX-License-Identifier: MIT */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pr.h"
#include "tbfw.h"
#include "util.h"

int png_write(const char *path, const gfx_t *g);

static int bad;
static const char *fmt_now = "pwg";
static const char *dir;

static void wave(uint8_t *px, int stride, int w, int h)
{
    gfx_t g;
    gfx_init(&g, px, w, h, stride, 0);
    for (int s = 0; s < 6; s++) {
        float prev_x = 100, prev_y = 200 + (float)s * 190;
        for (int i = 1; i <= 120; i++) {
            float t = (float)i / 120.0f, x = 100 + t * 1650, y = 200 + (float)s * 190 + sinf(t * 12.0f + (float)s) * 70;
            gfx_line(&g, prev_x, prev_y, x, y, 2 + (int)(5.0f * sinf(t * 3.14159f)), GFX_BLACK);
            prev_x = x, prev_y = y;
        }
    }
    gfx_ellipse(&g, 700, 1050, 1100, 1300, 6, GFX_NONE, GFX_BLACK);
    gfx_line(&g, 100, 1360, 1770, 1360, 1, GFX_BLACK);
}

static const char *TEXT =
    "# Notes from the week\n\n"
    "A first paragraph that is long enough to wrap over several lines so the layout can be seen: the quick brown fox "
    "jumps over the lazy dog, and then it does so again, because a printed page should show how words are broken "
    "between lines, with accents like \xc3\xa9\xc3\xa8\xc3\xa0\xc3\xbc and a degree sign 21\xc2\xb0.\n\n"
    "## A smaller heading\n"
    "Second paragraph. Short lines stay short.\nThe next line follows at once.\n\n"
    "### Third level\n"
    "- a list item\n- another list item that is a bit longer than the first one is\n\n"
    "Last paragraph of the first page.\n";

typedef struct {
    uint8_t *b;
    size_t n;
} cap_t;

static int cap_post(void *u, const char *url, const void *body, size_t n, uint8_t *resp, size_t cap, size_t *rn)
{
    (void)url, (void)cap;
    cap_t *c = u;
    c->b = realloc(c->b, c->n + n);
    memcpy(c->b + c->n, body, n);
    c->n += n;
    memset(resp, 0, 8);
    resp[0] = 1, resp[1] = 1;
    *rn = 8;
    return 200;
}

static const char *pwg_name(const char *base)
{
    static char n[32];
    snprintf(n, sizeof(n), "%s.%s", base, fmt_now);
    return n;
}

static void save_pcl(const char *name, cap_t *c)
{
    size_t hl = 0; /* the IPP header ends at the end-of-attributes tag; the document starts at its first bytes */
    int pwg = !strcmp(fmt_now, "pwg");
    while (hl + 3 < c->n && !(pwg ? !memcmp(c->b + hl, "RaS2", 4) : (c->b[hl] == 0x1b && c->b[hl + 1] == 'E'))) hl++;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(c->b + hl, 1, c->n - hl, f) != c->n - hl) bad = 1;
    if (f) fclose(f);
    printf("%s (%zu bytes)\n", path, c->n - hl);
}

static void shot(const pr_t *p, int orient, const char *name)
{
    static uint8_t fb[1872 / 8 * 1404];
    memset(fb, 0xff, sizeof(fb));
    app_touchbar_t bar;
    tbfw_layout(orient, &bar);
    gfx_t g;
    gfx_init(&g, fb, 1872, 1404, 1872 / 8, orient);
    pr_draw(p, &g, &bar);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    if (png_write(path, &g)) bad = 1;
    printf("%s\n", path);
}

typedef struct {
    ffs_t fs;
    pr_env_t env;
    cap_t cap;
} rig_t;
static int rread(void *u, const char *g, const char *n, uint8_t **b, size_t *s) { return ffs_read(&((rig_t *)u)->fs, g, n, b, s); }
static int rcfg(void *u, const char *k, char *b, int cap)
{
    (void)u;
    if (strcmp(k, "format") || cap < 3) return -1;
    memcpy(b, fmt_now, 3);
    return 3;
}

static int run(void);

int main(int argc, char **argv)
{
    dir = argc > 1 ? argv[1] : "shots";
    run();
    fmt_now = "pcl";
    return run() | bad;
}

static int run(void)
{
    static rig_t r;
    memset(&r, 0, sizeof(r));
    size_t n;
    uint8_t *png = make_png(1872, 1404, wave, &n);
    for (int i = 1; i <= 5; i++) {
        char p[64];
        snprintf(p, sizeof(p), "sketch/sk%04d.png", i);
        ffs_put(&r.fs, p, png, n);
    }
    ffs_put(&r.fs, "writer/notes.md", TEXT, strlen(TEXT));
    ffs_put(&r.fs, "writer/shopping.txt", "milk\nbread\n", 11);
    r.env = (pr_env_t){ { &r.fs, ffs_list }, rread, rcfg, { PJ_PWG, cap_post, &r.cap }, &r };
    pr_t p;
    pr_init(&p, &r.env);
    p.sel = 2;
    shot(&p, 0, "list");
    shot(&p, 1, "list-portrait");
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_TAP);
    shot(&p, 0, "preview-sketch");
    shot(&p, 1, "preview-sketch-portrait");
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_TAP);
    pr_run_print(&p);
    save_pcl(pwg_name("sketch"), &r.cap);
    shot(&p, 0, "result");
    free(r.cap.b);
    memset(&r.cap, 0, sizeof(r.cap));
    p.mode = PR_LIST;
    p.sel = 7;
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_TAP);
    shot(&p, 0, "preview-text");
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_TAP);
    pr_run_print(&p);
    save_pcl(pwg_name("text"), &r.cap);
    strcpy(p.share[0], "1. With your phone on the same Wi-Fi open:");
    strcpy(p.share[1], "2. Then type the code:");
    strcpy(p.share[2], "http://print.local");
    strcpy(p.share[3], "Code: 123456");
    p.mode = PR_SETTINGS;
    shot(&p, 0, "settings");
    pr_free(&p);
    free(r.cap.b);
    free(png);
    ffs_free(&r.fs);
    return bad;
}
