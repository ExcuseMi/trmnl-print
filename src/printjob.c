/* SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "printjob.h"
#include <stdlib.h>
#include <string.h>
#include "imgfit.h"
#include "ipp.h"
#include "pcl.h"
#include "pngio.h"
#include "pwg.h"
#include "strbuf.h"
#include "textlay.h"

#define PJ_FULL (-2)

size_t pj_job_bytes = PJ_JOB_BYTES; /* a variable so the tests can force a job to fill up */

typedef struct {
    uint8_t *p;
    size_t cap, n;
} buf_t;

static int buf_write(void *u, const void *b, size_t n)
{
    buf_t *B = u;
    if (B->n + n > B->cap) return PJ_FULL;
    memcpy(B->p + B->n, b, n);
    B->n += n;
    return 0;
}

static void say(pj_result_t *r, const char *a, const char *b, int num)
{
    size_t n = 0;
    const char *s[2] = { a, b };
    for (int k = 0; k < 2; k++)
        while (s[k] && *s[k] && n < sizeof(r->msg) - 12) r->msg[n++] = *s[k]++;
    if (num >= 0) {
        char d[12];
        int m = 0;
        do d[m++] = (char)('0' + num % 10), num /= 10; while (num);
        while (m) r->msg[n++] = d[--m];
    }
    r->msg[n] = 0;
}

typedef struct {
    const pj_net_t *net;
    char host[64];
    int port;
    char path[48];
    char url[160];
    buf_t b;
    size_t header;
    size_t mark; /* start of the page being written */
    pj_result_t *r;
    uint8_t resp[160];
} job_t;

static int job_open(job_t *j, const pj_net_t *net, const pj_printer_t *printer, pj_result_t *r)
{
    memset(j, 0, sizeof(*j));
    j->net = net;
    j->r = r;
    pj_printer_t chk;
    char addr[128];
    pj_printer_addr(printer, addr, sizeof(addr));
    if (!printer->host[0] || pj_printer_parse(addr, &chk) < 0) return say(r, "Printer address is not valid: ", printer->host, -1), -1;
    memcpy(j->host, printer->host, sizeof(j->host));
    j->port = printer->port;
    sb_set(j->path, sizeof(j->path), printer->path[0] ? printer->path : PJ_DEFAULT_PATH);
    size_t cap = pj_job_bytes;
    j->b.p = malloc(cap);
    if (!j->b.p && cap > PJ_JOB_BYTES_SMALL) j->b.p = malloc(cap = PJ_JOB_BYTES_SMALL);
    if (!j->b.p) return say(r, "Not enough memory to print", "", -1), -1;
    j->b.cap = cap;
    int cut = sb_set(j->url, sizeof(j->url), "http://") | sb_cat(j->url, sizeof(j->url), j->host) | sb_cat(j->url, sizeof(j->url), ":");
    cut |= sb_int(j->url, sizeof(j->url), j->port, 0) | sb_cat(j->url, sizeof(j->url), "/") | sb_cat(j->url, sizeof(j->url), j->path);
    if (cut) return free(j->b.p), j->b.p = 0, say(r, "Printer address is too long", "", -1), -1;
    return 0;
}

/* Starts a job in the buffer: IPP header, PCL reset */
static void job_start(job_t *j)
{
    pcl_out_t o = { buf_write, &j->b, 0, 0 };
    j->b.n = ipp_print_job_header(j->b.p, j->b.cap, j->host, j->port, j->path, "tiny-paper", j->net->format == PJ_PCL ? IPP_PCL : IPP_PWG);
    if (j->net->format == PJ_PCL) pcl_job_begin(&o);
    else pwg_job_begin(&o);
    j->header = j->b.n;
    j->mark = j->b.n;
}

static int job_send(job_t *j)
{
    pcl_out_t o = { buf_write, &j->b, 0, 0 };
    if (j->net->format == PJ_PCL) pcl_job_end(&o);
    size_t rn = 0;
    int st = j->net->post(j->net->u, j->url, j->b.p, j->b.n, j->resp, sizeof(j->resp), &rn);
    if (st == PJ_NET_TIMEOUT) return say(j->r, "Printer did not answer: timeout", "", -1), -1;
    if (st == PJ_NET_NO_WIFI) return say(j->r, "No Wi-Fi network: cannot reach the printer", "", -1), -1;
    if (st < 0) return say(j->r, "Printer not reachable", "", -1), -1;
    if (st != 200) return say(j->r, "Printer refused the job, HTTP ", "", st), -1;
    int ipp = ipp_status(j->resp, rn);
    if (ipp < 0) return say(j->r, "Printer answered, but not as a printer should", "", -1), -1;
    if (ipp >= 0x100) return say(j->r, "Printer refused the job, IPP code ", "", ipp), -1;
    j->r->jobs++;
    return 0;
}

static int pj_done(job_t *j, int rc)
{
    free(j->b.p);
    j->b.p = 0;
    if (rc == 0) say(j->r, j->r->pages == 1 ? "Sent 1 page to the printer" : "Sent pages to the printer: ", "", j->r->pages == 1 ? -1 : j->r->pages);
    return rc;
}

/* A page that could not be written whole: the words for it (one page in the job: it does not fit at all) */
static int page_failed(job_t *j, int rc)
{
    if (rc != PJ_FULL) return say(j->r, "The page could not be made", "", -1), -1;
    return say(j->r, "The page is too big for the memory of the X", "", -1), -1;
}

/* Writes one page into the current job; when it does not fit, sends what is there first and starts a new job */
static int job_page(job_t *j, const pcl_region_t *reg, int scale)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        pcl_out_t o = { buf_write, &j->b, 0, 0 };
        j->mark = j->b.n;
        int rc = j->net->format == PJ_PCL ? pcl_page(&o, reg) : pwg_page(&o, reg, scale);
        if (rc == 0) {
            j->r->pages++;
            return 0;
        }
        if (rc != PJ_FULL) return page_failed(j, rc);
        j->b.n = j->mark; /* drop the half page */
        if (j->mark == j->header) return page_failed(j, rc);
        if (job_send(j) < 0) return -1;
        job_start(j);
    }
    return -1;
}

typedef struct {
    img_t im;
    fit_t f;
    fit_cache_t c;
} pic_t;

static int pic_row(void *u, int y, uint8_t *row)
{
    pic_t *p = u;
    fit_row_cached(&p->im, &p->f, &p->c, y, row);
    return 0;
}

/* A picture's job, made a few rows at a time (pj_job_step) */
struct pj_job {
    pj_net_t net;
    job_t j;
    pic_t pic;
    uint8_t *own; /* the decoded picture, when the job made it */
    pcl_out_t o;
    int done;
    union {
        pwg_run_t pwg;
        pcl_run_t pcl;
    } run;
};

void pj_job_free(pj_job_t *J)
{
    if (!J) return;
    free(J->j.b.p);
    free(J->own);
    free(J);
}

static pj_job_t *img_start(const pj_net_t *net, const pj_printer_t *printer, uint8_t *px, uint8_t *own, int w, int h, int stride, pj_result_t *r)
{
    pj_job_t *J = calloc(1, sizeof(*J));
    if (!J) return free(own), say(r, "Not enough memory to print", "", -1), (pj_job_t *)0;
    J->net = *net;
    J->own = own;
    pic_t *p = &J->pic;
    p->im.w = w, p->im.h = h, p->im.stride = stride, p->im.px = px;
    int pwg = net->format != PJ_PCL;
    fit_compute(w, h, pwg ? PWG_PAGE_W : PCL_PAGE_W, pwg ? PWG_PAGE_H : PCL_PAGE_H, &p->f);
    if (job_open(&J->j, &J->net, printer, r) < 0) return pj_job_free(J), (pj_job_t *)0;
    job_start(&J->j);
    pcl_region_t reg = { p->f.x, p->f.y, p->f.w, p->f.h, pic_row, p };
    J->o = (pcl_out_t){ buf_write, &J->j.b, 0, 0 };
    int rc = pwg ? pwg_page_begin(&J->run.pwg, &J->o, &reg, 1) : pcl_page_begin(&J->run.pcl, &J->o, &reg);
    if (rc < 0) return page_failed(&J->j, rc), pj_job_free(J), (pj_job_t *)0;
    return J;
}

int pj_job_step(pj_job_t *J, int rows)
{
    if (J->done) return J->done < 0 ? -1 : 0;
    int rc = J->net.format != PJ_PCL ? pwg_page_step(&J->run.pwg, rows) : pcl_page_step(&J->run.pcl, rows);
    if (rc > 0) return 1;
    if (rc == 0) J->j.r->pages++, rc = job_send(&J->j);
    else rc = page_failed(&J->j, rc);
    rc = pj_done(&J->j, rc);
    J->done = rc < 0 ? -1 : 1;
    return rc;
}

static int run_all(pj_job_t *J)
{
    if (!J) return -1;
    int rc;
    while ((rc = pj_job_step(J, 1 << 30)) > 0) {}
    pj_job_free(J);
    return rc;
}

pj_job_t *pj_png_start(const pj_net_t *net, const pj_printer_t *printer, const uint8_t *file, size_t n, pj_result_t *r)
{
    memset(r, 0, sizeof(*r));
    int w, h;
    if (png_dims(file, n, &w, &h) < 0) return say(r, "Not a PNG this app can read", "", -1), (pj_job_t *)0;
    int stride = (w + 7) / 8;
    if ((size_t)stride * (size_t)h > IMG_MAX_BYTES) return say(r, "The picture is too big", "", -1), (pj_job_t *)0;
    uint8_t *px = malloc((size_t)stride * (size_t)h);
    if (!px) return say(r, "Not enough memory to print", "", -1), (pj_job_t *)0;
    if (png_decode(file, n, px, w, h, stride) < 0) return free(px), say(r, "Not a 1-bit PNG (only Sketch pictures are read)", "", -1), (pj_job_t *)0;
    return img_start(net, printer, px, px, w, h, stride, r);
}

int pj_print_png(const pj_net_t *net, const pj_printer_t *printer, const uint8_t *file, size_t n, pj_result_t *r)
{
    return run_all(pj_png_start(net, printer, file, n, r));
}

int pj_print_bitmap(const pj_net_t *net, const pj_printer_t *printer, const uint8_t *px, int w, int h, int stride, pj_result_t *r)
{
    memset(r, 0, sizeof(*r));
    if (!px || w < 8 || h < 1 || stride < (w + 7) / 8 || (size_t)stride * (size_t)h > IMG_MAX_BYTES) return say(r, "The page could not be made", "", -1), -1;
    return run_all(img_start(net, printer, (uint8_t *)px, 0, w, h, stride, r));
}

typedef struct {
    const tl_t *t;
    const tl_line_t *ln;
    int nl, w, y0, rows;
    uint8_t *band;
} txt_t;

static int txt_row(void *u, int y, uint8_t *row)
{
    txt_t *x = u;
    if (y < x->y0 || y >= x->y0 + x->rows) {
        x->y0 = y;
        tl_draw(x->t, x->ln, x->nl, x->y0, x->rows, x->w, x->band);
    }
    memcpy(row, x->band + (size_t)(y - x->y0) * (size_t)(x->w / 8), (size_t)x->w / 8);
    return 0;
}

#define BAND_ROWS 64

int pj_print_text(const pj_net_t *net, const pj_printer_t *printer, const char *text, size_t n, pj_result_t *r)
{
    memset(r, 0, sizeof(*r));
    if (n == 0) return say(r, "The file is empty", "", -1), -1;
    static tl_line_t lines[TL_PAGE_LINES]; /* 140 x 24 bytes: a page of lines */
    uint8_t *band = malloc((size_t)(PCL_PAGE_W / 8) * BAND_ROWS);
    if (!band) return say(r, "Not enough memory to print", "", -1), -1;
    job_t j;
    if (job_open(&j, net, printer, r) < 0) return free(band), -1;
    job_start(&j);
    tl_t t;
    tl_init(&t, text, n, PCL_PAGE_W, PCL_PAGE_H);
    int rc = 0, pages = 0, k;
    while (rc == 0 && (k = tl_page(&t, lines, TL_PAGE_LINES)) > 0) {
        if (pages++ >= PJ_MAX_PAGES) {
            rc = (say(r, "The text is longer than 200 pages", "", -1), -1);
            break;
        }
        int w = (PCL_PAGE_W - 2 * TL_MARGIN_X) & ~7;
        txt_t x = { &t, lines, k, w, -BAND_ROWS, BAND_ROWS, band };
        pcl_region_t reg = { TL_MARGIN_X, TL_MARGIN_TOP, w, t.y_end > 0 ? t.y_end : 1, txt_row, &x };
        rc = job_page(&j, &reg, 2); /* a PWG page is 600 dpi: the 300 dpi text is doubled */
    }
    if (rc == 0) rc = job_send(&j);
    free(band);
    return pj_done(&j, rc);
}
