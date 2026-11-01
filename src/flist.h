/* The list of printable files, grouped by the app that made them, newest first. The firmware's file_list gives names
   and sizes but no dates, so "newest" is the highest name in natural order (sk0012.png before sk0009.png, a title
   with a later number first). SPDX-License-Identifier: MIT */
#ifndef FLIST_H
#define FLIST_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define FL_MAX 192
#define FL_NAME 56
#define FL_APP 26
#define FL_DEPTH 2 /* folders below an app's root that are searched (documents/<folder>/<title>.md) */
#define FL_GROUP 20

enum { FL_PNG, FL_TEXT };

typedef struct {
    char group[FL_GROUP]; /* what the list shows: the app ("sketch"), or the folder in the print app's own folder */
    char app[FL_APP];     /* the app whose folder it is (API 2.8); "" in the print app's own folder */
    char name[FL_NAME];   /* the file, with its folders below the app's root when it is in some */
    int64_t mtime;        /* Unix seconds, 0 when unknown */
    unsigned char kind;
    unsigned long size;
} fl_entry_t;

typedef struct {
    fl_entry_t e[FL_MAX];
    int n;
    int dropped; /* files over FL_MAX, or names too long, left out */
} fl_t;

/* The file system the list reads. cb returns false to stop. */
typedef bool (*fl_cb_t)(const char *name, bool is_dir, unsigned long size, int64_t mtime, void *ctx);
typedef struct {
    void *u;
    /* the folder `dir` ("" the root) of app `app`; app "" is the print app's own folder */
    int (*list)(void *u, const char *app, const char *dir, fl_cb_t cb, void *ctx);
    /* NULL: the firmware cannot list other apps (before API 2.8): the own folder is scanned, one sub folder per group.
       Else the apps with data; each app's files are searched FL_DEPTH folders deep */
    int (*list_apps)(void *u, fl_cb_t cb, void *ctx);
} fl_fs_t;

/* Natural order compare: digit runs by value, case-insensitive. < 0, 0, > 0 */
int fl_natcmp(const char *a, const char *b);
/* Lists .png .md .txt (never the sketch app's "-cleared"/"-bad" copies), sorted */
int fl_scan(fl_t *l, const fl_fs_t *fs);
/* Groups alphabetical; in a group newest first by time, files without one (or equal) by name, highest first */
void fl_sort(fl_t *l);
#endif
