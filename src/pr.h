/* The Print app's controller: the file list, the preview, the print run and the messages. All decisions are here, on
   the host-tested side; the glue (apps/trmnl-print/main.c) only feeds events and pushes the screen. SPDX-License-Identifier: MIT */
#ifndef PR_H
#define PR_H
#include <app_api.h>
#include "flist.h"
#include "gfx.h"
#include "printjob.h"
#include "textlay.h"

#define PR_ROWS 8            /* list rows per page */
#define PR_DEFAULT_PRINTER "192.0.2.40"
#define PR_READ_MAX (1024 * 1024) /* the largest file read: a text beyond is cut at this size and said so */

enum { PR_LIST, PR_PREVIEW, PR_PRINTING, PR_RESULT, PR_SETTINGS };

typedef struct {
    fl_fs_t files;
    /* the whole file into a malloc'd buffer (the caller frees); < 0 on failure */
    int (*read)(void *u, const fl_entry_t *e, uint8_t **buf, size_t *n);
    int (*cfg_get)(void *u, const char *key, char *buf, int cap); /* length, or < 0 */
    pj_net_t net;
    void *u;
} pr_env_t;

typedef struct {
    const pr_env_t *env;
    fl_t list;
    int sel;       /* 0 the printer row, 1.. a file */
    int mode;
    int mode_after; /* where a result goes back to */
    uint8_t *data; /* the previewed file */
    size_t data_n;
    uint8_t *px;   /* the previewed PNG decoded: 1 = white */
    int pw, ph;
    int data_cut;  /* the text was longer than PR_READ_MAX */
    char note[96]; /* a message for the screen: read errors, the print result */
    pj_result_t res;
    int settings_req; /* 1: the glue starts the phone settings page; -1: stops it */
    int print_req;    /* the glue shows "Printing", then calls pr_run_print */
    char share[4][72]; /* the phone page's lines while PR_SETTINGS */
} pr_t;

void pr_init(pr_t *p, const pr_env_t *env);
void pr_free(pr_t *p);
void pr_rescan(pr_t *p);
void pr_printer(const pr_t *p, char *out, size_t cap); /* the address in use */
void pr_touch(pr_t *p, int zone, int gesture);
/* After print_req: blocks while the job goes out; sets the result screen */
void pr_run_print(pr_t *p);
/* The phone page's lines from webshare_start_settings' result and status (what the person types where) */
void pr_set_share(pr_t *p, int start_rc, const app_webshare_status_t *st);
void pr_draw(const pr_t *p, gfx_t *g, const app_touchbar_t *bar);
#endif
