/* The list of printable files, grouped by the app that made them, newest first. The firmware's file_list gives names
   and sizes but no dates, so "newest" is the highest name in natural order (sk0012.png before sk0009.png, a title
   with a later number first). SPDX-License-Identifier: MIT */
#ifndef FLIST_H
#define FLIST_H
#include <stdbool.h>
#include <stddef.h>

#define FL_MAX 192
#define FL_NAME 40
#define FL_GROUP 20

enum { FL_PNG, FL_TEXT };

typedef struct {
    char group[FL_GROUP]; /* the folder, e.g. "sketch" */
    char name[FL_NAME];
    unsigned char kind;
    unsigned long size;
} fl_entry_t;

typedef struct {
    fl_entry_t e[FL_MAX];
    int n;
    int dropped; /* files over FL_MAX, or names too long, left out */
} fl_t;

/* The file system the list reads: dir "" is the root of the print folder. cb returns false to stop. */
typedef bool (*fl_cb_t)(const char *name, bool is_dir, unsigned long size, void *ctx);
typedef struct {
    void *u;
    int (*list)(void *u, const char *dir, fl_cb_t cb, void *ctx);
} fl_fs_t;

/* Natural order compare: digit runs by value, case-insensitive. < 0, 0, > 0 */
int fl_natcmp(const char *a, const char *b);
/* Scans root/<group>/ for .png .md .txt (never the sketch app's "-cleared"/"-bad" copies) */
int fl_scan(fl_t *l, const fl_fs_t *fs);
/* Groups alphabetical, names newest first */
void fl_sort(fl_t *l);
#endif
