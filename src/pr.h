/* The Print app's controller: the file list, the preview, the print run and the messages. All decisions are here, on
   the host-tested side; the glue (apps/trmnl-print/main.c) only feeds events and pushes the screen. SPDX-License-Identifier: MIT */
#ifndef PR_H
#define PR_H
#include <app_api.h>
#include "flist.h"
#include "gfx.h"
#include "printer.h"
#include "printjob.h"
#include "textlay.h"

#define PR_ROWS 8            /* list rows per page */
#define PR_FOUND_MAX 8
#define PR_READ_MAX (1024 * 1024) /* the largest file read: a text beyond is cut at this size and said so */

/* PR_FINDING: browsing the network (blocks); PR_PICK: the printer menu; PR_TESTASK: confirm the test page */
enum { PR_LIST, PR_PREVIEW, PR_PRINTING, PR_RESULT, PR_SETTINGS, PR_FINDING, PR_PICK, PR_TESTASK };
/* The items of PR_PICK after the found printers, in order (the test page only with a default printer) */
enum { PK_FOUND, PK_FIND, PK_PHONE, PK_TEST, PK_BACK };

typedef struct {
    fl_fs_t files;
    /* the whole file into a malloc'd buffer (the caller frees); < 0 on failure */
    int (*read)(void *u, const fl_entry_t *e, uint8_t **buf, size_t *n);
    int (*cfg_get)(void *u, const char *key, char *buf, int cap); /* length, or < 0 */
    pj_net_t net;
    /* the default printer's file (printer.cfg text): bytes read, or < 0 when there is none; write it whole, 0 or < 0 */
    int (*cfg_read)(void *u, char *buf, size_t cap);
    int (*cfg_write)(void *u, const char *text, size_t n);
    /* api->net_browse("_ipp._tcp", ...): the count or < 0 (APP_E*) */
    int (*browse)(void *u, app_browse_cb_t cb, void *ctx);
    int64_t (*now)(void *u); /* Unix seconds, 0 if the clock is not set */
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
    fl_entry_t cur;    /* the file previewed and printed: from the list, or from the start argument */
    char caller[FL_APP]; /* the app that started us with a file (API 2.9): after printing or Back it gets the screen back */
    int back_req;      /* 1: the glue calls app_launch(caller, "") and returns from app_main */
    pj_printer_t def;  /* the default printer (printer.cfg) */
    int have_def;
    pj_printer_t found[PR_FOUND_MAX];
    int nfound, psel;  /* PR_PICK: the printers found, the selected item */
    int find_req;      /* the glue shows "Finding printers", then calls pr_run_find */
    int pick_back;     /* the mode PR_PICK goes back to */
    int setup;         /* started with "setup:<app>": choosing a printer (or Back) returns to that app */
    int test;          /* the print run or result in progress is the test page */
    char snap[2][80];  /* the phone page's printer and format values when it opened: only a change counts */
} pr_t;

void pr_init(pr_t *p, const pr_env_t *env);
void pr_free(pr_t *p);
void pr_rescan(pr_t *p);
/* The start argument "app:path" (api->app_arg): the file `path` of the app `app` opens straight in the preview. A bad
   argument or an unreadable file shows a message (PR_RESULT) and OK goes back to the caller, or to the list when the
   app name itself is bad. An empty argument does nothing. */
void pr_start_arg(pr_t *p, const char *arg);
/* The default printer as shown ("name  address" or the address), "" when there is none */
void pr_printer(const pr_t *p, char *out, size_t cap);
/* After find_req: blocks while the network is searched; PR_PICK with the printers found */
void pr_run_find(pr_t *p);
/* printer setup (prsetup.c) */
void prs_load(pr_t *p);
void prs_open(pr_t *p, int back_to);       /* the printer menu; searches at once when there is no default */
void prs_touch(pr_t *p, int zone, int gesture); /* PR_PICK and PR_TESTASK */
void prs_settings_closed(pr_t *p);          /* the phone page was left: a changed value is applied */
int prs_items(const pr_t *p);
int prs_kind(const pr_t *p, int i);         /* PK_*; for PK_FOUND the index is i */
void prs_search(pr_t *p);                  /* fills found[] and the note */
int prs_print_test(pr_t *p);                /* the test page; 0 or < 0 with res.msg set */
void pr_touch(pr_t *p, int zone, int gesture);
/* After print_req: blocks while the job goes out; sets the result screen */
void pr_run_print(pr_t *p);
/* The phone page's lines from webshare_start_settings' result and status (what the person types where) */
void pr_set_share(pr_t *p, int start_rc, const app_webshare_status_t *st);
void pr_draw(const pr_t *p, gfx_t *g, const app_touchbar_t *bar);
#endif
