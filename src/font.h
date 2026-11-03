/* Baked bitmap fonts (gen/fonts_gen.c from tools/bake_fonts.py) and text measuring. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef FONT_H
#define FONT_H
#include <stdint.h>

enum { FONT_MEDIUM, FONT_SEMIBOLD, FONT_BOLD };

typedef struct {
    uint16_t cp; /* code point */
    int16_t xoff, yoff; /* top-left of the bitmap from the pen on the baseline */
    uint16_t w, h;
    uint32_t adv64; /* advance in 1/64 px */
    uint32_t off; /* row records in the face's data */
} font_glyph_t;

typedef struct {
    uint8_t weight;
    uint16_t size; /* px, as given to FreeType */
    int16_t ascent, descent; /* font metrics (descent positive) */
    int16_t digit_h; /* height of '0' above the baseline */
    uint16_t n;
    const font_glyph_t *g; /* sorted by code point */
    const uint8_t *data;
} font_face_t;

extern const font_face_t font_faces[];
extern const int font_face_count;

/* The largest baked face of `weight` not above `size` (the smallest one if none is) */
const font_face_t *font_get(int weight, int size);
const font_glyph_t *font_glyph(const font_face_t *f, uint32_t cp);
/* Decodes one UTF-8 character, advancing *s; 0 at the end */
uint32_t font_next_cp(const char **s);
/* Width in 1/64 px; tab: digits take the widest digit's advance (tabular figures, as the mockups) */
int32_t font_width64(const font_face_t *f, const char *s, int tab);
/* 1 when every character of s is baked in f */
int font_has_all(const font_face_t *f, const char *s);
/* The largest face of `weight` holding s with width <= maxw and digit height <= maxh (lib.py fit) */
const font_face_t *font_fit(int weight, const char *s, int maxw, int maxh, int tab);
#endif
