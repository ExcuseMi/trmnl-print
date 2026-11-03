/* Bounded string building without snprintf (the X's app loader has none, nor strcpy or strncmp: apps/build.sh's import
   check says so). Each returns 0, or -1 when the text was cut to fit; dst is always terminated. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef STRBUF_H
#define STRBUF_H
#include <stddef.h>
#include <string.h>

static inline int sb_cat(char *dst, size_t cap, const char *s)
{
    size_t n = strlen(dst), k = strlen(s);
    int cut = n + k >= cap;
    if (cut) k = cap - 1 - n;
    memcpy(dst + n, s, k);
    dst[n + k] = 0;
    return cut ? -1 : 0;
}
static inline int sb_set(char *dst, size_t cap, const char *s)
{
    dst[0] = 0;
    return sb_cat(dst, cap, s);
}
/* a non-negative number, at least `width` digits (zero padded) */
static inline int sb_int(char *dst, size_t cap, long v, int width)
{
    char d[24];
    int m = 0;
    if (v < 0) v = 0;
    do d[m++] = (char)('0' + v % 10), v /= 10; while (v);
    while (m < width && m < 20) d[m++] = '0';
    char t[24];
    int k = 0;
    while (m) t[k++] = d[--m];
    t[k] = 0;
    return sb_cat(dst, cap, t);
}
#endif
