/* The Print app's screens. SPDX-License-Identifier: MIT */
#include <string.h>
#include "imgfit.h"
#include "pcl.h"
#include "pr.h"
#include "strbuf.h"

enum { BLK = GFX_BLACK, WHT = GFX_WHITE };

static const font_face_t *face(int bold, int size) { return font_get(bold ? FONT_BOLD : FONT_MEDIUM, size); }
static int text_w(const font_face_t *f, const char *s) { return (int)((font_width64(f, s, 0) + 32) >> 6); }
static void text(gfx_t *g, const font_face_t *f, int x, int y, const char *s, int c) { gfx_text(g, f, (int32_t)x * 64, y, s, 0, c); }

static void label(gfx_t *g, const app_touchbar_t *bar, int z, const char *s)
{
    const font_face_t *f = face(1, 34);
    const app_rect_t *r = &bar->zone[z];
    gfx_rrect(g, r->x + 6, r->y + 6, r->x + r->w - 7, r->y + r->h - 7, 8, 3, WHT, BLK);
    text(g, f, r->x + (r->w - text_w(f, s)) / 2, r->y + r->h / 2 + 12, s, BLK);
}

static void size_str(unsigned long n, char *out)
{
    char d[12];
    int m = 0, k = 0;
    unsigned long v = n < 1024 ? n : (n + 1023) / 1024;
    do d[m++] = (char)('0' + v % 10), v /= 10; while (v);
    while (m) out[k++] = d[--m];
    if (n >= 1024) out[k++] = ' ', out[k++] = 'K', out[k++] = 'B';
    else out[k++] = ' ', out[k++] = 'B';
    out[k] = 0;
}

static void cat(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(dst);
    while (*s && n + 1 < cap) dst[n++] = *s++;
    dst[n] = 0;
}

/* The screen area for content: the bar's strip stays free (the zone labels are drawn in it) */
static void content(gfx_t *g)
{
    gfx_content(g, APP_TOUCHBAR_DEPTH, 0);
    if (g->orient == APP_ORIENT_BAR_BOTTOM || g->orient == APP_ORIENT_BAR_TOP) g->H = g->SH - APP_TOUCHBAR_DEPTH;
}

static void draw_list(const pr_t *p, gfx_t *g)
{
    const font_face_t *big = face(1, 64), *f = face(0, 46), *small = face(0, 34);
    text(g, big, 50, 100, "Print", BLK);
    int first = p->sel / PR_ROWS * PR_ROWS, total = p->list.n + 1, y = 190;
    for (int i = first; i < first + PR_ROWS && i < total; i++, y += 100) {
        int on = i == p->sel;
        if (on) gfx_fill_rect(g, 30, (float)(y - 56), (float)(g->W - 30), (float)(y + 30), BLK);
        int c = on ? WHT : BLK;
        if (i == 0) {
            char pr[160], line[200] = "Printer: ";
            pr_printer(p, pr, sizeof(pr));
            cat(line, sizeof(line), pr[0] ? pr : "none yet (touch the middle to set one up)");
            text(g, f, 50, y, line, c);
            continue;
        }
        const fl_entry_t *e = &p->list.e[i - 1];
        char sz[16];
        size_str(e->size, sz);
        text(g, f, 50, y, e->name, c);
        text(g, small, 50 + 520, y, e->group, c);
        text(g, small, g->W - 50 - text_w(small, sz), y, sz, c);
    }
    if (p->list.n == 0) text(g, f, 50, 300, "No files yet. Draw a sketch or write something first.", BLK);
    if (p->note[0]) text(g, small, 50, g->H - 30, p->note, BLK);
}

static void draw_pick(const pr_t *p, gfx_t *g)
{
    const font_face_t *f = face(0, 46), *small = face(0, 34);
    text(g, face(1, 64), 50, 100, "Printers", BLK);
    char d[160] = "Default: ";
    if (p->have_def) {
        pr_printer(p, d + 9, sizeof(d) - 9);
    } else cat(d, sizeof(d), "none yet");
    text(g, small, 50, 150, d, BLK);
    int total = prs_items(p), first = p->psel / PR_ROWS * PR_ROWS, y = 230;
    for (int i = first; i < first + PR_ROWS && i < total; i++, y += 90) {
        int on = i == p->psel, k = prs_kind(p, i);
        if (on) gfx_fill_rect(g, 30, (float)(y - 56), (float)(g->W - 30), (float)(y + 30), BLK);
        int c = on ? WHT : BLK;
        if (k == PK_FOUND) {
            const pj_printer_t *f1 = &p->found[i];
            char a[100];
            pj_printer_addr(f1, a, sizeof(a));
            text(g, f, 50, y, f1->name[0] ? f1->name : a, c);
            text(g, small, g->W - 50 - text_w(small, a), y, a, c);
        } else {
            text(g, f, 50, y, k == PK_FIND ? "Find printers" : k == PK_PHONE ? "Enter address on the phone" : k == PK_TEST ? "Test page" : "Back", c);
        }
    }
    if (p->note[0]) text(g, small, 50, g->H - 30, p->note, BLK);
}

static void draw_preview(const pr_t *p, gfx_t *g)
{
    const fl_entry_t *e = &p->cur;
    const font_face_t *small = face(0, 34);
    char to[170] = "Prints on: ";
    if (p->have_def) pr_printer(p, to + 11, sizeof(to) - 11);
    else cat(to, sizeof(to), "no printer yet (Print will look for one)");
    text(g, small, g->W - 50 - text_w(small, to), 70, to, BLK);
    text(g, face(1, 46), 50, 70, e->name, BLK);
    text(g, small, 50 + text_w(face(1, 46), e->name) + 30, 70, e->group, BLK);
    int top = 100, bottom = g->H - 20;
    if (p->px) {
        img_t im = { p->px, p->pw, p->ph, (p->pw + 7) / 8 };
        int aw = g->W - 100, ah = bottom - top - 20;
        long sc = (long)aw * 1000 / PCL_PAGE_W < (long)ah * 1000 / PCL_PAGE_H ? (long)aw * 1000 / PCL_PAGE_W : (long)ah * 1000 / PCL_PAGE_H;
        int sw = (int)(PCL_PAGE_W * sc / 1000), sh = (int)(PCL_PAGE_H * sc / 1000);
        int sx = (g->W - sw) / 2, sy = top + 10;
        fit_t page, ft;
        fit_compute(p->pw, p->ph, PCL_PAGE_W, PCL_PAGE_H, &page); /* where the printer puts it, scaled to the sheet */
        ft.rotate = page.rotate;
        ft.w = (int)(page.w * sc / 1000) & ~7;
        ft.h = (int)(page.h * sc / 1000);
        if (ft.w < 8 || ft.h < 1) return;
        gfx_rrect(g, (float)sx, (float)sy, (float)(sx + sw), (float)(sy + sh), 0, 2, WHT, BLK);
        int x0 = sx + (int)(page.x * sc / 1000), y0 = sy + (int)(page.y * sc / 1000), aw8 = ft.w, ah8 = ft.h;
        uint8_t row[2048];
        if (aw8 / 8 > (int)sizeof(row)) return;
        for (int y = 0; y < ah8; y++) {
            fit_row(&im, &ft, y, row);
            for (int x = 0; x < aw8; x++)
                if (!(row[x >> 3] & (0x80 >> (x & 7)))) gfx_px(g, x0 + x, y0 + y, BLK);
        }
        return;
    }
    tl_t t;
    tl_line_t ln[40];
    tl_init(&t, (const char *)p->data, p->data_n, g->W - 100 + 2 * TL_MARGIN_X, 100000);
    int k = tl_page(&t, ln, 40);
    for (int i = 0; i < k; i++) {
        int base = top + 40 + ln[i].y;
        if (base > bottom) break;
        char tmp[TL_LINE_MAX + 1];
        tl_line_text(&t, &ln[i], tmp, sizeof(tmp));
        text(g, t.face[ln[i].kind], 50, base, tmp, BLK);
    }
    if (p->data_cut) text(g, small, 50, g->H - 30, "Only the first 1 MB of this text is printed.", BLK);
}

static void draw_msg(gfx_t *g, const char *title, const char *a, const char *b)
{
    text(g, face(1, 64), 50, 120, title, BLK);
    text(g, face(0, 46), 50, 230, a, BLK);
    if (b[0]) text(g, face(0, 46), 50, 300, b, BLK);
}

void pr_draw(const pr_t *p, gfx_t *g, const app_touchbar_t *bar)
{
    gfx_screen(g);
    gfx_fill(g, WHT);
    content(g);
    switch (p->mode) {
    case PR_LIST: draw_list(p, g); break;
    case PR_PREVIEW: draw_preview(p, g); break;
    case PR_PRINTING:
    case PR_RESULT: { /* which printer it goes to (owner 2026-11-02) */
        char to[80] = "", who[64] = "";
        if (p->have_def) pj_printer_label(&p->def, who, sizeof(who)), sb_set(to, sizeof(to), "To: "), sb_cat(to, sizeof(to), who);
        if (p->mode == PR_PRINTING) draw_msg(g, "Printing...", p->have_def ? to : "Sending the page to the printer.", "This takes a few seconds.");
        else draw_msg(g, p->res.pages && p->note[0] && p->res.jobs ? "Done" : "Not printed", p->note, to);
        break;
    }
    case PR_SETTINGS: {
        text(g, face(1, 64), 50, 120, "Printer address", BLK);
        int y = 220;
        for (int i = 0; i < 4; i++)
            if (p->share[i][0]) text(g, face(i == 2 ? 1 : 0, i == 2 ? 64 : 46), 50, y, p->share[i], BLK), y += i == 2 ? 110 : 70;
        text(g, face(0, 34), 50, y + 40, "The printer's address is in its network settings,", BLK);
        text(g, face(0, 34), 50, y + 85, "or on the network report it prints (menu: Network).", BLK);
        break;
    }
    case PR_FINDING: draw_msg(g, "Finding printers...", "Looking on your Wi-Fi network.", "This takes up to 15 seconds."); break;
    case PR_PICK: draw_pick(p, g); break;
    case PR_TESTASK: {
        char a[160];
        pr_printer(p, a, sizeof(a));
        draw_msg(g, "Test page", "Print a test page on:", a);
        break;
    }
    default: break;
    }
    gfx_screen(g);
    if (p->mode == PR_LIST) label(g, bar, APP_TOUCH_LEFT, "Up"), label(g, bar, APP_TOUCH_MIDDLE, p->sel ? "Open" : "Printer"), label(g, bar, APP_TOUCH_RIGHT, "Down");
    else if (p->mode == PR_PICK) label(g, bar, APP_TOUCH_LEFT, "Up"), label(g, bar, APP_TOUCH_MIDDLE, "Choose"), label(g, bar, APP_TOUCH_RIGHT, "Down");
    else if (p->mode == PR_TESTASK) label(g, bar, APP_TOUCH_LEFT, "Cancel"), label(g, bar, APP_TOUCH_MIDDLE, "Print");
    else if (p->mode == PR_PREVIEW) label(g, bar, APP_TOUCH_LEFT, "Back"), label(g, bar, APP_TOUCH_MIDDLE, "Print"), label(g, bar, APP_TOUCH_RIGHT, "Next file");
    else if (p->mode != PR_PRINTING && p->mode != PR_FINDING) label(g, bar, APP_TOUCH_MIDDLE, "OK");
}
