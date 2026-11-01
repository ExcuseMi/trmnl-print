/* SPDX-License-Identifier: MIT */
#include "textlay.h"
#include <string.h>
#include "gfx.h"

static int pitch(const font_face_t *f) { return f->ascent + f->descent + f->size / 8; }

void tl_init(tl_t *t, const char *text, size_t n, int page_w, int page_h)
{
    memset(t, 0, sizeof(*t));
    t->text = text;
    t->n = n;
    t->face[0] = font_get(FONT_MEDIUM, 46);
    t->face[1] = font_get(FONT_BOLD, 64);
    t->face[2] = font_get(FONT_BOLD, 46);
    t->width = page_w - 2 * TL_MARGIN_X;
    t->page_h = page_h - TL_MARGIN_TOP - TL_MARGIN_BOTTOM;
    t->at_start = 1;
}

/* Copies text[off, off + len) for measuring or drawing: control characters become spaces */
static void copy_clean(char *dst, const char *src, size_t len)
{
    for (size_t i = 0; i < len; i++) dst[i] = (unsigned char)src[i] < ' ' ? ' ' : src[i];
    dst[len] = 0;
}

static int fits(const tl_t *t, const font_face_t *f, size_t a, size_t b)
{
    char tmp[TL_LINE_MAX + 1];
    if (b - a > TL_LINE_MAX) return 0;
    copy_clean(tmp, t->text + a, b - a);
    return font_width64(f, tmp, 0) <= (int32_t)t->width * 64;
}

/* The end of the UTF-8 character that starts at p */
static size_t next_char(const tl_t *t, size_t p)
{
    p++;
    while (p < t->n && ((unsigned char)t->text[p] & 0xc0) == 0x80) p++;
    return p;
}

/* Next wrapped line: 1 a text line in *ln (y unset), 2 a blank line, 3 a form feed, 0 the end */
static int next_line(tl_t *t, tl_line_t *ln)
{
    if (t->pos >= t->n) return 0;
    size_t eol = t->pos;
    while (eol < t->n && t->text[eol] != '\n') eol++;
    size_t end = eol;
    if (t->at_start) {
        size_t p = t->pos;
        if (t->text[p] == '\f') {
            t->pos = p + 1;
            return 3;
        }
        while (end > p && (t->text[end - 1] == '\r' || t->text[end - 1] == ' ' || t->text[end - 1] == '\t')) end--;
        if (end == p) {
            t->pos = eol < t->n ? eol + 1 : eol;
            return 2;
        }
        size_t k = 0;
        while (p + k < end && t->text[p + k] == '#' && k < 6) k++;
        t->cur_kind = 0;
        if (k && p + k < end && t->text[p + k] == ' ') {
            t->cur_kind = k == 1 ? 1 : 2;
            t->pos = p + k + 1;
        }
        t->at_start = 0;
    } else {
        while (end > t->pos && (t->text[end - 1] == '\r' || t->text[end - 1] == ' ' || t->text[end - 1] == '\t')) end--;
    }
    const font_face_t *f = t->face[t->cur_kind];
    size_t a = t->pos, brk = 0, p = a;
    while (p < end) { /* words */
        size_t e = p;
        while (e < end && t->text[e] != ' ' && t->text[e] != '\t') e++;
        if (fits(t, f, a, e)) {
            brk = e;
            p = e;
            while (p < end && (t->text[p] == ' ' || t->text[p] == '\t')) p++;
            if (p >= end) break;
            continue;
        }
        if (!brk) { /* one word wider than the line: break inside it */
            size_t c = a, last = a;
            while (c < e && fits(t, f, a, next_char(t, c))) c = next_char(t, c);
            last = c > a ? c : next_char(t, a);
            brk = last;
        }
        break;
    }
    if (!brk) brk = end;
    ln->off = a;
    ln->len = brk - a;
    ln->kind = t->cur_kind;
    size_t np = brk;
    while (np < end && (t->text[np] == ' ' || t->text[np] == '\t')) np++;
    if (np >= end) { /* paragraph done */
        t->pos = eol < t->n ? eol + 1 : eol;
        t->at_start = 1;
    } else {
        t->pos = np;
    }
    return 1;
}

int tl_page(tl_t *t, tl_line_t *out, int max)
{
    int n = 0, y = 0, gap = 0;
    t->y_end = 0;
    for (;;) {
        tl_line_t ln;
        int k;
        if (t->have_carry) {
            ln = t->carry;
            t->have_carry = 0;
            k = 1;
        } else {
            k = next_line(t, &ln);
        }
        if (k == 0) break;
        if (k == 2) {
            if (n) gap += pitch(t->face[0]) / 2;
            continue;
        }
        if (k == 3) {
            if (n) break; /* the next page starts after the form feed */
            continue;
        }
        const font_face_t *f = t->face[ln.kind];
        int space = n ? gap + (ln.kind ? pitch(f) / 3 : 0) : 0;
        if (n && (y + space + pitch(f) > t->page_h || n >= max)) {
            t->carry = ln;
            t->have_carry = 1;
            break;
        }
        y += space;
        ln.y = y + f->ascent;
        out[n++] = ln;
        y += pitch(f);
        gap = 0;
    }
    t->y_end = y;
    return n;
}

void tl_line_text(const tl_t *t, const tl_line_t *ln, char *out, size_t cap)
{
    size_t len = ln->len > TL_LINE_MAX ? TL_LINE_MAX : ln->len;
    if (len >= cap) len = cap - 1;
    copy_clean(out, t->text + ln->off, len);
}

void tl_draw(const tl_t *t, const tl_line_t *ln, int n, int y0, int rows, int w, uint8_t *buf)
{
    memset(buf, 0xff, (size_t)(w / 8) * (size_t)rows);
    gfx_t g;
    gfx_init(&g, buf, w, rows, w / 8, 0);
    for (int i = 0; i < n; i++) {
        const font_face_t *f = t->face[ln[i].kind];
        if (ln[i].y - f->ascent >= y0 + rows || ln[i].y + f->descent < y0) continue;
        char tmp[TL_LINE_MAX + 1];
        size_t len = ln[i].len > TL_LINE_MAX ? TL_LINE_MAX : ln[i].len;
        copy_clean(tmp, t->text + ln[i].off, len);
        gfx_text(&g, f, 0, ln[i].y - y0, tmp, 0, GFX_BLACK);
    }
}
