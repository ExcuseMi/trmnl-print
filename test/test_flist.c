/* The file list: natural order, grouping, newest first, filters, limits, a failing file system. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "util.h"

static void order(void)
{
    CHECK(fl_natcmp("sk0009.png", "sk0012.png") < 0);
    CHECK(fl_natcmp("sk0012.png", "sk0012.png") == 0);
    CHECK(fl_natcmp("a2", "a10") < 0);
    CHECK(fl_natcmp("a10", "a2") > 0);
    CHECK(fl_natcmp("Notes", "notes") == 0);
    CHECK(fl_natcmp("a", "ab") < 0 && fl_natcmp("ab", "a") > 0);
    CHECK(fl_natcmp("x007", "x7") == 0);
    CHECK(fl_natcmp("x99999999999999999999", "x100000000000000000000") < 0); /* longer than any int */
    CHECK(fl_natcmp("", "") == 0 && fl_natcmp("", "a") < 0);
}

static void grouping(void)
{
    ffs_t fs;
    memset(&fs, 0, sizeof(fs));
    ffs_put(&fs, "sketch/sk0002.png", "x", 1);
    ffs_put(&fs, "writer/diary.md", "hello", 5);
    ffs_put(&fs, "sketch/sk0010.png", "x", 1);
    ffs_put(&fs, "sketch/sk0001.png", "x", 1);
    ffs_put(&fs, "sketch/sk0010-cleared.png", "x", 1);   /* the app's own backups are not listed */
    ffs_put(&fs, "sketch/sk0003-bad.png", "x", 1);
    ffs_put(&fs, "writer/notes.txt", "x", 1);
    ffs_put(&fs, "writer/readme.pdf", "x", 1);            /* not printable */
    ffs_put(&fs, "gameboy/shot2.png", "x", 1);
    ffs_put(&fs, "loose.txt", "x", 1);                    /* not in a folder: not an app's file */
    fl_t l;
    fl_fs_t f = { &fs, ffs_list, 0 };
    CHECK(fl_scan(&l, &f) == 0);
    CHECK(l.n == 6 && l.dropped == 0);
    const char *want[] = { "gameboy/shot2.png", "sketch/sk0010.png", "sketch/sk0002.png", "sketch/sk0001.png", "writer/notes.txt", "writer/diary.md" };
    for (int i = 0; i < 6; i++) {
        char got[64];
        snprintf(got, sizeof(got), "%s/%s", l.e[i].group, l.e[i].name);
        CHECK(!strcmp(got, want[i]));
    }
    CHECK(l.e[0].kind == FL_PNG && l.e[4].kind == FL_TEXT);
    CHECK(l.e[5].size == 5);
    ffs_free(&fs);
}

static void limits(void)
{
    ffs_t fs;
    memset(&fs, 0, sizeof(fs));
    ffs_put(&fs, "sketch/a.png", "x", 1);
    ffs_put(&fs, "sketch/this-name-is-far-too-long-to-be-kept-in-the-list-of-the-print-app.png", "x", 1);
    fl_t l;
    fl_fs_t f = { &fs, ffs_list, 0 };
    CHECK(fl_scan(&l, &f) == 0 && l.n == 1 && l.dropped == 1);
    ffs_free(&fs);
    /* more files than the list holds: the first FL_MAX are kept, the rest counted */
    static ffs_t big;
    memset(&big, 0, sizeof(big));
    static fl_t bl;
    for (int i = 0; i < FL_MAX + 8; i++) {
        char p[64];
        snprintf(p, sizeof(p), "g/f%03d.txt", i);
        ffs_put(&big, p, "x", 1);
    }
    fl_fs_t bf = { &big, ffs_list, 0 };
    CHECK(fl_scan(&bl, &bf) == 0 && bl.n == FL_MAX && bl.dropped == 8);
    CHECK(bl.e[0].name[0] == 'f');
    ffs_free(&big);
    /* the file system cannot list: an error, and an empty list */
    ffs_t bad;
    memset(&bad, 0, sizeof(bad));
    bad.fail_list_root = 1;
    CHECK(fl_scan(&l, &(fl_fs_t){ &bad, ffs_list, 0 }) < 0 && l.n == 0);
    /* nothing there at all */
    ffs_t none;
    memset(&none, 0, sizeof(none));
    CHECK(fl_scan(&l, &(fl_fs_t){ &none, ffs_list, 0 }) == 0 && l.n == 0);
}

int main(void)
{
    order();
    grouping();
    limits();
    return CHECK_DONE("test_flist");
}
