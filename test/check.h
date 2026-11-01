/* A minimal test harness: CHECK counts failures, each test file's main returns their number. SPDX-License-Identifier: MIT */
#ifndef CHECK_H
#define CHECK_H
#include <stdio.h>
static int check_fail;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c);                                      \
            check_fail++;                                                                                              \
        }                                                                                                              \
    } while (0)
#define CHECK_DONE(name) (fprintf(stderr, "%s: %s\n", name, check_fail ? "FAILED" : "ok"), check_fail)
#endif
