/* The default printer file: parse, write, read back through the firmware calls another app uses. SPDX-License-Identifier: MIT */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "printer.h"
#include "printjob.h"

static void address(void)
{
    pj_printer_t p;
    CHECK(pj_printer_parse("192.168.1.50", &p) == 0 && !strcmp(p.host, "192.168.1.50") && p.port == 631 && !p.path[0]);
    CHECK(pj_printer_parse("brother.local:9631", &p) == 0 && !strcmp(p.host, "brother.local") && p.port == 9631);
    CHECK(pj_printer_parse("10.0.0.2:631/printers/Office", &p) == 0 && !strcmp(p.path, "printers/Office"));
    CHECK(pj_printer_parse("10.0.0.2/ipp/print", &p) == 0 && !strcmp(p.host, "10.0.0.2") && !strcmp(p.path, "ipp/print"));
    const char *bad[] = { "", ":631", "host:", "host:0", "host:70000", "host:12x", "a b", "host/../x", "host/a b", "ho/st:1/%00", "a\"b", "h:1:2" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) CHECK(pj_printer_parse(bad[i], &p) < 0 && !p.host[0]);
    CHECK(pj_printer_parse(NULL, &p) < 0);
    char long_host[100];
    memset(long_host, 'a', 99), long_host[99] = 0;
    CHECK(pj_printer_parse(long_host, &p) < 0);
    pj_printer_parse("10.0.0.2:9631/printers/Office", &p);
    char a[100];
    pj_printer_addr(&p, a, sizeof(a));
    CHECK(!strcmp(a, "10.0.0.2:9631/printers/Office"));
    pj_printer_parse("10.0.0.2/ipp/print", &p);
    pj_printer_addr(&p, a, sizeof(a));
    CHECK(!strcmp(a, "10.0.0.2"));                        /* the defaults are left out */
}

static void cfg_round_trip(void)
{
    pj_printer_t p, q;
    pj_printer_parse("192.168.1.50:631/ipp/print", &p);
    snprintf(p.name, sizeof(p.name), "Brother MFC-L2710DW series");
    p.format = PJ_PCL;
    char t[PJ_CFG_MAX];
    int n = pj_cfg_format(&p, t, sizeof(t));
    CHECK(n > 0 && strstr(t, "name=Brother MFC-L2710DW series\n") && strstr(t, "host=192.168.1.50\n") && strstr(t, "format=pcl\n"));
    CHECK(pj_cfg_parse(t, (size_t)n, &q) == 0);
    CHECK(!strcmp(q.name, p.name) && !strcmp(q.host, p.host) && q.port == 631 && !strcmp(q.path, "ipp/print") && q.format == PJ_PCL);
    CHECK(pj_cfg_format(&p, t, 20) < 0);                  /* does not fit: refused, not cut */
}

static void cfg_bad_files(void)
{
    pj_printer_t q;
    const char *none[] = { "", "\n\n", "name=x\n", "host=\n", "host=a b\n", "host=a\"b\n", "port=631\n", "garbage without equals", "host=ok\nhost=bad host\n" };
    for (unsigned i = 0; i < sizeof(none) / sizeof(none[0]); i++) CHECK(pj_cfg_parse(none[i], strlen(none[i]), &q) == PJ_NO_PRINTER && !q.host[0]);
    const char *t = "host=h1\r\nport=99999\r\npath=../x\r\nformat=weird\r\nname=A\x01" "B\r\nfuture=1\r\n";
    CHECK(pj_cfg_parse(t, strlen(t), &q) == 0);
    CHECK(q.port == 631 && !q.path[0] && q.format == PJ_PWG && !strcmp(q.name, "AB"));  /* bad values fall back, host stays */
    const char *nonl = "host=h2";                          /* no final newline */
    CHECK(pj_cfg_parse(nonl, strlen(nonl), &q) == 0 && !strcmp(q.host, "h2"));
}

/* ---- another app reading the file through the firmware calls ---- */
static char file_text[PJ_CFG_MAX + 100];
static size_t file_len, file_pos;
static int file_exists, file_read_err, opened;
static char opened_app[40], opened_path[40];
static int f_dummy;
static app_file_t *f_open_app(const char *app, const char *path)
{
    snprintf(opened_app, sizeof(opened_app), "%s", app);
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    opened++;
    file_pos = 0;
    return file_exists ? (app_file_t *)&f_dummy : 0;
}
static int f_read(app_file_t *f, void *b, size_t n)
{
    (void)f;
    if (file_read_err) return -1;
    size_t k = file_len - file_pos < n ? file_len - file_pos : n;
    memcpy(b, file_text + file_pos, k);
    file_pos += k;
    return (int)k;
}
static int f_close(app_file_t *f) { return (void)f, 0; }

static void put(const char *s) { file_len = strlen(s), memcpy(file_text, s, file_len), file_exists = 1, file_read_err = 0; }

static void default_printer(void)
{
    app_api_t api;
    memset(&api, 0, sizeof(api));
    api.size = sizeof(api);
    api.file_open_app = f_open_app;
    api.file_read = f_read;
    api.file_close = f_close;
    pj_printer_t p;
    file_exists = 0;
    CHECK(pj_default_printer(&api, &p) == PJ_NO_PRINTER);                       /* no file: nothing set up */
    CHECK(!strcmp(opened_app, "trmnl-print") && !strcmp(opened_path, "printer.cfg"));
    put("host=10.0.0.9\nname=Office\n");
    CHECK(pj_default_printer(&api, &p) == 0 && !strcmp(p.host, "10.0.0.9") && !strcmp(p.name, "Office"));
    put("nonsense");
    CHECK(pj_default_printer(&api, &p) == PJ_NO_PRINTER);                       /* a file without a host */
    put("host=10.0.0.9\n");
    file_read_err = 1;
    CHECK(pj_default_printer(&api, &p) == APP_EIO);
    file_read_err = 0;
    memset(file_text, 'x', sizeof(file_text));                                  /* a huge file is read only to the limit */
    file_len = sizeof(file_text), file_exists = 1;
    CHECK(pj_default_printer(&api, &p) == PJ_NO_PRINTER);
    api.size = APP_API_SLOT(53) - 1;                                            /* firmware before API 2.8 */
    int before = opened;
    CHECK(pj_default_printer(&api, &p) == APP_ENOTSUP && opened == before);
    CHECK(pj_default_printer(NULL, &p) == APP_EINVAL);
}

int main(void)
{
    address();
    cfg_round_trip();
    cfg_bad_files();
    default_printer();
    return CHECK_DONE("test_printer");
}
