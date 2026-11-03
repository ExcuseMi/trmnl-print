/* Text layout: wrap, headings, blank lines, page breaks, long words, UTF-8, CRLF, drawing. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "check.h"
#include <stdlib.h>
#include <string.h>
#include "pcl.h"
#include "textlay.h"

#define MAXP 64
static tl_line_t pages[MAXP][TL_PAGE_LINES];
static int counts[MAXP], ends[MAXP];

static int layout(tl_t *t, const char *s)
{
    tl_init(t, s, strlen(s), PCL_PAGE_W, PCL_PAGE_H);
    int np = 0, k;
    while (np < MAXP && (k = tl_page(t, pages[np], TL_PAGE_LINES)) > 0) counts[np] = k, ends[np++] = t->y_end;
    return np;
}

static int line_w(const tl_t *t, const tl_line_t *l)
{
    char tmp[TL_LINE_MAX + 1];
    tl_line_text(t, l, tmp, sizeof(tmp));
    return (int)(font_width64(t->face[l->kind], tmp, 0) / 64);
}

static void wrap(void)
{
    char s[6000] = "";
    for (int i = 0; i < 400; i++) strcat(s, "word"), strcat(s, i % 9 == 0 ? "s " : " ");
    tl_t t;
    int np = layout(&t, s);
    CHECK(np == 1 && counts[0] > 5);
    int words = 0;
    for (int i = 0; i < counts[0]; i++) {
        CHECK(line_w(&t, &pages[0][i]) <= t.width);
        CHECK(pages[0][i].len > 0);
        char tmp[TL_LINE_MAX + 1];
        tl_line_text(&t, &pages[0][i], tmp, sizeof(tmp));
        CHECK(tmp[0] != ' ' && tmp[strlen(tmp) - 1] != ' '); /* no space at the ends */
        for (char *p = tmp; *p; p++) words += *p == ' ';
        words++;
    }
    CHECK(words == 400);                                  /* no word lost or split */
    for (int i = 1; i < counts[0]; i++) CHECK(pages[0][i].y > pages[0][i - 1].y);
}

static void headings_and_gaps(void)
{
    tl_t t;
    int np = layout(&t, "# Title\n## Sub\n### Third\nbody one\n\nbody two\r\nbody three\n");
    CHECK(np == 1 && counts[0] == 6);
    CHECK(pages[0][0].kind == 1 && pages[0][1].kind == 2 && pages[0][2].kind == 2 && pages[0][3].kind == 0);
    char tmp[64];
    tl_line_text(&t, &pages[0][0], tmp, sizeof(tmp));
    CHECK(!strcmp(tmp, "Title"));
    tl_line_text(&t, &pages[0][4], tmp, sizeof(tmp));
    CHECK(!strcmp(tmp, "body two"));                      /* the CR of CRLF is not part of it */
    int normal = pages[0][5].y - pages[0][4].y, gap = pages[0][4].y - pages[0][3].y;
    CHECK(gap > normal);                                   /* the blank line left space */
    CHECK(t.face[1]->size > t.face[0]->size);
    tl_line_text(&t, &pages[0][3], tmp, sizeof(tmp));
    CHECK(!strcmp(tmp, "body one"));
    int n2 = layout(&t, "#nospace\n#### four\n");
    CHECK(n2 == 1 && counts[0] == 2 && pages[0][0].kind == 0 && pages[0][1].kind == 2);
    tl_line_text(&t, &pages[0][0], tmp, sizeof(tmp));
    CHECK(!strcmp(tmp, "#nospace"));                       /* a # without a space is text */
}

static void page_breaks(void)
{
    char *s = malloc(20000);
    s[0] = 0;
    for (int i = 0; i < 300; i++) {
        char l[24];
        strcpy(l, "line ");
        l[5] = (char)('0' + i / 100), l[6] = (char)('0' + i / 10 % 10), l[7] = (char)('0' + i % 10), l[8] = '\n', l[9] = 0;
        strcat(s, l);
    }
    tl_t t;
    int np = layout(&t, s);
    CHECK(np >= 4 && np < 12);
    int total = 0;
    for (int p = 0; p < np; p++) {
        CHECK(ends[p] <= t.page_h);
        CHECK(pages[p][counts[p] - 1].y <= t.page_h);
        for (int i = 0; i < counts[p]; i++) {
            char tmp[32];
            tl_line_text(&t, &pages[p][i], tmp, sizeof(tmp));
            int num = (tmp[5] - '0') * 100 + (tmp[6] - '0') * 10 + (tmp[7] - '0');
            CHECK(num == total);                           /* in order, none lost, none twice across the breaks */
            total++;
        }
        if (p) CHECK(pages[p][0].y == t.face[0]->ascent); /* a new page starts at the top */
    }
    CHECK(total == 300);
    /* a form feed starts a page */
    int n2 = layout(&t, "one\n\f\ntwo\n");
    CHECK(n2 == 2 && counts[0] == 1 && counts[1] == 1);
    n2 = layout(&t, "\f\none\n");
    CHECK(n2 == 1);                                        /* a form feed on an empty page makes no empty page */
    free(s);
}

static void odd_input(void)
{
    tl_t t;
    CHECK(layout(&t, "") == 0);
    CHECK(layout(&t, "\n\n   \n\t\n") == 0);                /* only blank lines: nothing to print */
    char big[1500];
    memset(big, 'W', sizeof(big) - 1), big[sizeof(big) - 1] = 0;
    int np = layout(&t, big);
    CHECK(np == 1 && counts[0] >= 2);
    size_t sum = 0;
    for (int i = 0; i < counts[0]; i++) {
        CHECK(line_w(&t, &pages[0][i]) <= t.width);
        sum += pages[0][i].len;
    }
    CHECK(sum == sizeof(big) - 1);                         /* a word wider than the line is broken, nothing lost */
    char u[1900] = "";
    for (int i = 0; i < 150; i++) strcat(u, "\xc3\xa9\xc3\xa8\xc3\xa0\xc3\xbc\xc3\xaf\xc3\xb6"); /* e a u i o with accents, no spaces */
    np = layout(&t, u);
    CHECK(np == 1 && counts[0] >= 2);
    for (int i = 0; i < counts[0]; i++) {
        CHECK(pages[0][i].len % 2 == 0);                   /* never inside a two-byte character */
        CHECK(line_w(&t, &pages[0][i]) <= t.width);
    }
    char tabs[] = "a\tb\tc";
    np = layout(&t, tabs);
    char tmp[16];
    tl_line_text(&t, &pages[0][0], tmp, sizeof(tmp));
    CHECK(np == 1 && !strcmp(tmp, "a b c"));
    char nonl[] = "no newline at the end";
    CHECK(layout(&t, nonl) == 1 && counts[0] == 1);
    char tiny[8];
    tl_line_text(&t, &pages[0][0], tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7);                              /* output is cut to the buffer, with its NUL */
}

static void drawing(void)
{
    tl_t t;
    layout(&t, "Hello\n\nWorld\n");
    int w = (PCL_PAGE_W - 2 * TL_MARGIN_X) & ~7, rows = 400, stride = w / 8;
    uint8_t *buf = malloc((size_t)stride * (size_t)rows);
    tl_draw(&t, pages[0], counts[0], 0, rows, w, buf);
    long ink[400] = { 0 };
    for (int y = 0; y < rows; y++)
        for (int i = 0; i < stride; i++) ink[y] += __builtin_popcount((uint8_t)~buf[y * stride + i]);
    int first = -1, last = -1;
    for (int y = 0; y < rows; y++)
        if (ink[y]) first = first < 0 ? y : first, last = y;
    CHECK(first >= 0 && first >= pages[0][0].y - t.face[0]->ascent - 2);
    CHECK(last <= pages[0][1].y + t.face[0]->descent + 2);
    /* the band below the text: nothing is drawn there, and a band that misses all lines is white */
    tl_draw(&t, pages[0], counts[0], 2000, rows, w, buf);
    int white = 1;
    for (int i = 0; i < stride * rows; i++) white &= buf[i] == 0xff;
    CHECK(white);
    /* a band cutting through a line shows the same pixels as the whole drawing, shifted */
    uint8_t *whole = malloc((size_t)stride * (size_t)rows), *part = malloc((size_t)stride * 32);
    tl_draw(&t, pages[0], counts[0], 0, rows, w, whole);
    int y0 = pages[0][0].y - 10;
    tl_draw(&t, pages[0], counts[0], y0, 32, w, part);
    CHECK(!memcmp(part, whole + (size_t)y0 * (size_t)stride, (size_t)stride * 32));
    free(buf), free(whole), free(part);
}

int main(void)
{
    wrap();
    headings_and_gaps();
    page_breaks();
    odd_input();
    drawing();
    return CHECK_DONE("test_text");
}
