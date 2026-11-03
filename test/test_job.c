/* Print jobs: the IPP request around the PCL, every way the printer can fail, job splitting. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pwg.h"
#include "util.h"

static void ink_block(uint8_t *px, int stride, int w, int h)
{
    for (int y = h / 4; y < h / 2; y++)
        for (int x = w / 4; x < w / 2; x++) set_ink(px, stride, x, y);
}

static const uint8_t *find(const uint8_t *h, size_t n, const char *needle, size_t m)
{
    for (size_t i = 0; i + m <= n; i++)
        if (!memcmp(h + i, needle, m)) return h + i;
    return 0;
}

static void ipp_bytes(void)
{
    uint8_t b[256];
    size_t n = ipp_print_job_header(b, sizeof(b), "192.168.1.50", 631, NULL, "doc", IPP_PWG);
    CHECK(n > 0);
    CHECK(b[0] == 1 && b[1] == 1 && b[2] == 0 && b[3] == 2);     /* IPP 1.1, Print-Job */
    CHECK(b[4] == 0 && b[5] == 0 && b[6] == 0 && b[7] == 1 && b[8] == 1);
    CHECK(b[n - 1] == 3);                                          /* end of attributes */
    CHECK(find(b, n, "ipp://192.168.1.50:631/ipp/print", 32));
    CHECK(find(b, n, "image/pwg-raster", 16));
    /* the first attribute is the charset, as the standard needs it first */
    CHECK(b[8] == 1 && b[9] == 0x47 && b[10] == 0 && b[11] == 18 && !memcmp(b + 12, "attributes-charset", 18));
    for (size_t cap = 0; cap < n; cap += 13) CHECK(ipp_print_job_header(b, cap, "192.168.1.50", 631, NULL, "doc", IPP_PWG) == 0); /* too small: refused */
    CHECK(ipp_print_job_header(b, sizeof(b), "", 631, NULL, "doc", IPP_PWG) == 0);
    CHECK(ipp_print_job_header(b, sizeof(b), "h", 0, NULL, "doc", IPP_PWG) == 0);
    CHECK(ipp_print_job_header(b, sizeof(b), "h", 70000, NULL, "doc", IPP_PWG) == 0);
    /* the resource path comes from discovery: in the URI of the request, and in the URL of the post */
    n = ipp_print_job_header(b, sizeof(b), "192.168.1.50", 631, "printers/Office", "doc", IPP_PWG);
    CHECK(n > 0 && find(b, n, "ipp://192.168.1.50:631/printers/Office", 38));
    CHECK(ipp_print_job_header(b, sizeof(b), "h", 631, "", "doc", IPP_PWG) > 0);
    uint8_t resp[8] = { 1, 1, 0, 0, 0, 0, 0, 1 };
    CHECK(ipp_status(resp, 8) == 0);
    resp[2] = 0x04, resp[3] = 0x05;
    CHECK(ipp_status(resp, 8) == 0x405);
    CHECK(ipp_status(resp, 7) == -1);
    resp[0] = 'x';
    CHECK(ipp_status(resp, 8) == -1);
}

static void path_in_url(void)
{
    size_t n;
    uint8_t *png = make_png(200, 100, ink_block, &n);
    printer_t pr;
    printer_init(&pr);
    pj_net_t net = { PJ_PWG, fake_post, &pr };
    pj_result_t r;
    CHECK(pj_print_png(&net, tp("10.0.0.2:9631/printers/Office"), png, n, &r) == 0);
    CHECK(!strcmp(pr.url, "http://10.0.0.2:9631/printers/Office"));
    CHECK(pj_print_png(&net, tp("10.0.0.2"), png, n, &r) == 0 && !strcmp(pr.url, "http://10.0.0.2:631/ipp/print"));
    pj_printer_t bad = *tp("10.0.0.2");
    strcpy(bad.path, "../etc");                 /* a path from outside the app is never trusted */
    int posts = pr.posts;
    CHECK(pj_print_png(&net, &bad, png, n, &r) < 0 && pr.posts == posts);
    free(png);
    printer_free(&pr);
}

static void png_job(void)
{
    size_t n;
    uint8_t *png = make_png(1872, 1404, ink_block, &n);
    printer_t pr;
    printer_init(&pr);
    pj_net_t net = { PJ_PCL, fake_post, &pr };
    pj_result_t r;
    CHECK(pj_print_png(&net, tp("192.168.1.50"), png, n, &r) == 0);
    CHECK(pr.posts == 1 && r.pages == 1 && r.jobs == 1);
    CHECK(!strcmp(pr.url, "http://192.168.1.50:631/ipp/print"));
    CHECK(!strcmp(r.msg, "Sent 1 page to the printer"));
    const uint8_t *b = pr.body[0];
    uint8_t hb[300];
    size_t hl = ipp_print_job_header(hb, sizeof(hb), "192.168.1.50", 631, NULL, "tiny-paper", IPP_PCL);
    CHECK(hl > 0);
    const char start[] = "\x1b" "E" "\x1b&u300D" "\x1b&l26A" "\x1b&l0O";
    CHECK(!memcmp(b + hl, start, sizeof(start) - 1));
    CHECK(find(b, pr.len[0], "\x1b*p29X\x1b*p184Y", 13)); /* x = 100 - 71, y = 234 - 50 for the 2280 x 3040 picture */
    CHECK(!memcmp(b + pr.len[0] - 3, "\f\x1b" "E", 3));
    CHECK(pr.len[0] > hl + 100 && pr.len[0] < 320000);     /* mostly white rows pack small */
    printer_free(&pr);
    /* every failure: reported in words, nothing crashes, nothing counted as sent */
    struct { int status, ipp; const char *msg; } cases[] = {
        { PJ_NET_UNREACHABLE, 0, "Printer not reachable" },
        { PJ_NET_TIMEOUT, 0, "Printer did not answer: timeout" },
        { PJ_NET_NO_WIFI, 0, "No Wi-Fi network: cannot reach the printer" },
        { 500, 0, "Printer refused the job, HTTP 500" },
        { 404, 0, "Printer refused the job, HTTP 404" },
        { 200, 0x0400, "Printer refused the job, IPP code 1024" },
        { 200, -1, "Printer answered, but not as a printer should" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        printer_init(&pr);
        pr.status = cases[i].status, pr.ipp = cases[i].ipp;
        CHECK(pj_print_png(&net, tp("192.168.1.50"), png, n, &r) < 0);
        CHECK(!strcmp(r.msg, cases[i].msg));
        CHECK(r.jobs == 0);
        printer_free(&pr);
    }
    printer_init(&pr);
    pr.ipp = 1;                                              /* successful-ok-ignored-or-substituted-attributes */
    CHECK(pj_print_png(&net, tp("192.168.1.50"), png, n, &r) == 0);
    printer_free(&pr);
    /* bad input: nothing is sent */
    printer_init(&pr);
    CHECK(pj_print_png(&net, tp("192.168.1.50"), png, 40, &r) < 0 && pr.posts == 0);
    CHECK(pj_print_png(&net, tp(""), png, n, &r) < 0 && pr.posts == 0 && strstr(r.msg, "address"));
    png[n / 2] ^= 0xff;                                       /* a damaged file */
    CHECK(pj_print_png(&net, tp("p"), png, n, &r) < 0 && pr.posts == 0);
    printer_free(&pr);
    free(png);
}

static void text_job(void)
{
    printer_t pr;
    printer_init(&pr);
    pj_net_t net = { PJ_PCL, fake_post, &pr };
    pj_result_t r;
    CHECK(pj_print_text(&net, tp("p:9000"), "# Title\n\nSome text here.\n", 25, &r) == 0);
    CHECK(pr.posts == 1 && r.pages == 1 && !strcmp(pr.url, "http://p:9000/ipp/print"));
    CHECK(find(pr.body[0], pr.len[0], "ipp://p:9000/ipp/print", 22));
    CHECK(find(pr.body[0], pr.len[0], "\x1b*p79X\x1b*p100Y", 13)); /* 150 - 71, 150 - 50 */
    printer_free(&pr);
    printer_init(&pr);
    CHECK(pj_print_text(&net, tp("p"), "", 0, &r) < 0 && pr.posts == 0);
    CHECK(pj_print_text(&net, tp("p"), "\n\n", 2, &r) == 0 || r.pages == 0); /* only blank lines: sends an empty job or says so, never crashes */
    printer_free(&pr);
    /* a text of 3 pages (about 150 KB each as PCL): one job, split into several when the buffer fills */
    char *s = malloc(60000);
    s[0] = 0;
    for (int i = 0; i < 150; i++) strcat(s, "A line of the long text that is printed.\n");
    printer_init(&pr);
    int rc0 = pj_print_text(&net, tp("p"), s, strlen(s), &r);
    CHECK(rc0 == 0 && r.pages == 3 && pr.posts == 1);
    int pages_one_job = r.pages;
    printer_free(&pr);
    size_t keep = pj_job_bytes;
    pj_job_bytes = 320000;                                      /* a small buffer: the job fills up and is split */
    printer_init(&pr);
    CHECK(pj_print_text(&net, tp("p"), s, strlen(s), &r) == 0);
    CHECK(r.pages == pages_one_job && pr.posts == r.jobs && r.jobs == 2);
    int pages_seen = 0;
    for (int i = 0; i < pr.posts && i < 8; i++) {
        CHECK(pr.len[i] <= 320000);
        const uint8_t *b = pr.body[i];
        CHECK(!memcmp(b + pr.len[i] - 2, "\x1b" "E", 2));       /* each job ends with its reset */
        int starts = 0, ends = 0;                                /* every page in it is whole: as many ends as starts */
        for (size_t k = 0; k + 7 <= pr.len[i]; k++) {
            starts += !memcmp(b + k, "\x1b*t300R", 7);
            ends += !memcmp(b + k, "\x1b*rB\f", 5);
        }
        CHECK(starts == ends && starts >= 1);
        pages_seen += ends;
    }
    CHECK(pages_seen == r.pages);
    printer_free(&pr);
    /* the printer fails on the second job: the run stops there with the words, and says how far it got */
    printer_init(&pr);
    pr.status = PJ_NET_TIMEOUT;
    CHECK(pj_print_text(&net, tp("p"), s, strlen(s), &r) < 0 && pr.posts == 1 && strstr(r.msg, "timeout"));
    printer_free(&pr);
    /* a page that cannot fit in an empty job */
    pj_job_bytes = 300;
    printer_init(&pr);
    CHECK(pj_print_text(&net, tp("p"), s, strlen(s), &r) < 0 && strstr(r.msg, "too big") && pr.posts == 0);
    printer_free(&pr);
    pj_job_bytes = keep;
    free(s);
}

static void pwg_jobs(void)
{
    size_t n;
    uint8_t *png = make_png(1872, 1404, ink_block, &n);
    printer_t pr;
    printer_init(&pr);
    pj_net_t net = { PJ_PWG, fake_post, &pr };
    pj_result_t r;
    CHECK(pj_print_png(&net, tp("192.168.1.50"), png, n, &r) == 0 && pr.posts == 1 && r.pages == 1);
    const uint8_t *b = pr.body[0];
    CHECK(find(b, pr.len[0], "image/pwg-raster", 16) && !find(b, pr.len[0], "application/octet-stream", 24));
    uint8_t hb[300];
    size_t hl = ipp_print_job_header(hb, sizeof(hb), "192.168.1.50", 631, NULL, "tiny-paper", IPP_PWG);
    CHECK(!memcmp(b + hl, "RaS2PwgRaster", 13));
    CHECK(pr.len[0] > hl + 1800 && pr.len[0] < 400000);      /* a mostly white page stays small */
    /* the picture is centred on the 600 dpi page: a 2280 x 3040 (300 dpi) fit is 4560 x 6080 here */
    const uint8_t *ph = b + hl + 4;
    CHECK(ph[372 + 2] == 0x13 && ph[372 + 3] == 0x60);      /* 4960 */
    printer_free(&pr);
    printer_init(&pr);
    /* a text page: 600 dpi rows are 1 byte a pixel, so it is bigger than PCL; one page must fit the job buffer */
    char *s = malloc(60000);
    s[0] = 0;
    for (int i = 0; i < 52; i++) strcat(s, "A line of the long text that is printed, with some more words.\n");
    CHECK(pj_print_text(&net, tp("p"), s, strlen(s), &r) == 0 && r.pages == 1);
    fprintf(stderr, "pwg full text page job %zu bytes\n", pr.len[0]);
    CHECK(pr.len[0] < PJ_JOB_BYTES);
    /* 600 dpi: the 300 dpi text is doubled, so its top margin (150 px) is 300 white rows: a 256 group, then 44 */
    const uint8_t *ras = find(pr.body[0], pr.len[0], "RaS2", 4);
    CHECK(ras && ras[4 + PWG_HEADER] == 255);
    printer_free(&pr);
    /* more pages than one job holds: split in order, each job whole (starts with RaS2) */
    for (int i = 0; i < 150; i++) strcat(s, "Another line of the long text that is printed here.\n");
    size_t keep = pj_job_bytes;
    pj_job_bytes = 1500000;
    printer_init(&pr);
    CHECK(pj_print_text(&net, tp("p"), s, strlen(s), &r) == 0 && r.pages >= 3 && pr.posts == r.jobs);
    for (int i = 0; i < pr.posts && i < 8; i++) CHECK(find(pr.body[i], pr.len[i], "RaS2PwgRaster", 13) && pr.len[i] <= 1500000);
    printer_free(&pr);
    pj_job_bytes = keep;
    free(s);
    free(png);
}

int main(void)
{
    pwg_jobs();
    ipp_bytes();
    path_in_url();
    png_job();
    text_job();
    return CHECK_DONE("test_job");
}
