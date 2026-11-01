/* SPDX-License-Identifier: MIT */
#include "font.h"

const font_face_t *font_get(int weight, int size)
{
    const font_face_t *best = 0, *small = 0;
    for (int i = 0; i < font_face_count; i++) {
        const font_face_t *f = &font_faces[i];
        if (f->weight != weight) continue;
        if (f->size <= size && (!best || f->size > best->size)) best = f;
        if (!small || f->size < small->size) small = f;
    }
    return best ? best : small;
}

const font_glyph_t *font_glyph(const font_face_t *f, uint32_t cp)
{
    int lo = 0, hi = (int)f->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (f->g[mid].cp == cp) return &f->g[mid];
        if (f->g[mid].cp < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return 0;
}

uint32_t font_next_cp(const char **s)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p;
    if (!c) return 0;
    int n = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
    if (n) c &= 0x3f >> n;
    p++;
    for (int i = 0; i < n && (*p & 0xc0) == 0x80; i++) c = (c << 6) | (*p++ & 0x3f);
    *s = (const char *)p;
    return c;
}

static int32_t digit_adv(const font_face_t *f)
{
    int32_t m = 0;
    for (char d = '0'; d <= '9'; d++) {
        const font_glyph_t *g = font_glyph(f, (uint32_t)d);
        if (g && (int32_t)g->adv64 > m) m = (int32_t)g->adv64;
    }
    return m;
}

int32_t font_width64(const font_face_t *f, const char *s, int tab)
{
    int32_t w = 0, dw = tab ? digit_adv(f) : 0;
    uint32_t c;
    while ((c = font_next_cp(&s))) {
        if (tab && c >= '0' && c <= '9') { w += dw; continue; }
        const font_glyph_t *g = font_glyph(f, c);
        if (g) w += (int32_t)g->adv64;
    }
    return w;
}

int font_has_all(const font_face_t *f, const char *s)
{
    uint32_t c;
    while ((c = font_next_cp(&s)))
        if (!font_glyph(f, c)) return 0;
    return 1;
}

const font_face_t *font_fit(int weight, const char *s, int maxw, int maxh, int tab)
{
    const font_face_t *best = 0;
    for (int i = 0; i < font_face_count; i++) {
        const font_face_t *f = &font_faces[i];
        if (f->weight != weight || (best && f->size <= best->size)) continue;
        if (f->digit_h > maxh || font_width64(f, s, tab) > (int32_t)maxw * 64 || !font_has_all(f, s)) continue;
        best = f;
    }
    return best ? best : font_get(weight, 10);
}
