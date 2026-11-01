/* libc calls the X's loader does not export (its table: elf_loader's esp_elf_symbol.c): the compiler emits memmove for
   overlapping copies, and the X refused the app at start ("needs a function the firmware lacks", device 2026-10-31).
   apps/build.sh's import check is the guard; -fvisibility=hidden keeps these inside the app.
   SPDX-License-Identifier: MIT */
#include <stddef.h>

/* no loop-to-call rewrite here, or the body would call itself */
__attribute__((optimize("no-tree-loop-distribute-patterns"))) void *memmove(void *dst, const void *src, size_t n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s)
        while (n--) *d++ = *s++;
    else
        while (n--) d[n] = s[n];
    return dst;
}

/* memcmp is not in the loader's table either (found by the same import check, 2026-11-01) */
__attribute__((optimize("no-tree-loop-distribute-patterns"))) int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y) return *x < *y ? -1 : 1;
    return 0;
}
