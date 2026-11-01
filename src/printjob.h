/* Printing: a PNG or a text becomes PCL pages, packed into IPP Print-Job requests that the app posts to the printer.
   The network is a callback (the X's http_request on the device, a fake in the tests), so every failure path can be
   tested. A job holds up to PJ_JOB_BYTES; more pages go as further jobs, in order. SPDX-License-Identifier: MIT */
#ifndef PRINTJOB_H
#define PRINTJOB_H
#include <stddef.h>
#include <stdint.h>
#include "printer.h"

#define PJ_JOB_BYTES (1024 * 1024)
#define PJ_JOB_BYTES_SMALL (256 * 1024)
#define PJ_MAX_PAGES 200

extern size_t pj_job_bytes; /* the job buffer size, PJ_JOB_BYTES; changed by tests only */

/* Failures a post can report (the adaptor maps the firmware's errors to these) */
#define PJ_NET_UNREACHABLE (-1)
#define PJ_NET_TIMEOUT (-2)
#define PJ_NET_NO_WIFI (-3)

/* The connection: POSTs body (Content-Type application/ipp) to url; returns the HTTP status (> 0), or a PJ_NET_* error. The answer's
   body goes to resp (cap bytes), its size to *rn. */
/* What a job carries */
enum { PJ_PWG, PJ_PCL };

typedef struct {
    int format; /* PJ_PWG (default, 0) or PJ_PCL */
    int (*post)(void *u, const char *url, const void *body, size_t n, uint8_t *resp, size_t cap, size_t *rn);
    void *u;
} pj_net_t;

typedef struct {
    char msg[96]; /* plain words for the screen, also on success */
    int pages, jobs;
} pj_result_t;

/* Each returns 0 when all pages were accepted, else < 0 with r->msg set. */
int pj_print_png(const pj_net_t *net, const pj_printer_t *printer, const uint8_t *file, size_t n, pj_result_t *r);
int pj_print_text(const pj_net_t *net, const pj_printer_t *printer, const char *text, size_t n, pj_result_t *r);
/* A 1-bit picture (1 = white, `stride` bytes a row), fitted to the page like a PNG; for pages the app draws itself */
int pj_print_bitmap(const pj_net_t *net, const pj_printer_t *printer, const uint8_t *px, int w, int h, int stride, pj_result_t *r);
#endif
