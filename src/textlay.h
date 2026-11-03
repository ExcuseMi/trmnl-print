/* Plain text and Markdown laid out on A4 pages: word wrap to the page width, headings (#, ##) bigger and bold, blank
   lines as gaps, page breaks. Lines are found one by one from the text in memory; a page is a short list of placed
   lines, drawn into row bands so a page bitmap never exists. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef TEXTLAY_H
#define TEXTLAY_H
#include <stddef.h>
#include <stdint.h>
#include "font.h"

#define TL_MARGIN_X 150   /* paper px (12.7 mm) */
#define TL_MARGIN_TOP 150
#define TL_MARGIN_BOTTOM 150
#define TL_LINE_MAX 400   /* bytes of one wrapped line */
#define TL_PAGE_LINES 140
#define TL_MAX_PAGES 200

typedef struct {
    size_t off, len;   /* the line's bytes in the text (a heading without its #s) */
    int kind;          /* 0 body, 1 heading 1, 2 heading 2 and below */
    int y;             /* baseline, in the page region (0 = region top) */
} tl_line_t;

typedef struct {
    const char *text;
    size_t n, pos;
    const font_face_t *face[3];
    int width;         /* text width in px */
    int page_h;        /* region height available: page height minus top and bottom margins */
    /* a line carried to the next page */
    int have_carry;
    tl_line_t carry;
    int y_end;         /* after tl_page: the bottom of the last line */
    int at_start;      /* pos is at the start of a source line */
    int cur_kind;      /* kind of the paragraph being wrapped */
    int pending_gap;   /* a blank line is waiting to become space */
} tl_t;

void tl_init(tl_t *t, const char *text, size_t n, int page_w, int page_h);
/* The next page's lines (up to max); returns their count, 0 at the end of the text. t->y_end is the region height used. */
int tl_page(tl_t *t, tl_line_t *out, int max);
/* The line's text, control characters as spaces, in out (cap bytes) */
void tl_line_text(const tl_t *t, const tl_line_t *ln, char *out, size_t cap);
/* Draws lines into rows [y0, y0 + rows) of the region: buf is rows x (w / 8) bytes, 1 = white */
void tl_draw(const tl_t *t, const tl_line_t *ln, int n, int y0, int rows, int w, uint8_t *buf);
#endif
