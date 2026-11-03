/* Print's own read and write of the default printer text, on API 2.11 (device settings) and before (printer.cfg).
   SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "check.h"
#include <stdio.h>
#include <string.h>
#include "prcfg.h"

static char file[PJ_CFG_MAX + 1], dev[PJ_CFG_MAX + 1];
static int file_n = -1, dev_n = -1, file_writes, dev_writes, dev_set_rc;
static char dev_key[16];
static int fd;

static app_file_t *f_open(const char *path, int mode)
{
    CHECK(!strcmp(path, "printer.cfg"));
    if (mode == APP_FILE_READ && file_n < 0) return 0;
    return (app_file_t *)&fd;
}
static int f_read(app_file_t *f, void *b, size_t n)
{
    (void)f;
    static size_t pos;
    if (pos >= (size_t)file_n) return pos = 0, 0;
    size_t k = (size_t)file_n - pos < n ? (size_t)file_n - pos : n;
    memcpy(b, file + pos, k), pos += k;
    return (int)k;
}
static int f_write(app_file_t *f, const void *b, size_t n)
{
    (void)f;
    file_writes++;
    memcpy(file, b, n), file_n = (int)n;
    return (int)n;
}
static int f_close(app_file_t *f) { return (void)f, 0; }
static int d_get(const char *k, char *b, size_t cap)
{
    if (strcmp(k, "printer")) return APP_ENOENT;
    if (dev_n < 0) return APP_ENOENT;
    if ((size_t)dev_n >= cap) return APP_EINVAL;
    memcpy(b, dev, (size_t)dev_n), b[dev_n] = 0;
    return dev_n;
}
static int d_set(const char *k, const char *v, size_t n)
{
    snprintf(dev_key, sizeof(dev_key), "%s", k);
    dev_writes++;
    if (dev_set_rc) return dev_set_rc;
    memcpy(dev, v, n), dev_n = (int)n;
    return 0;
}

static void api_init(app_api_t *a, int n211)
{
    memset(a, 0, sizeof(*a));
    a->size = n211 ? sizeof(*a) : APP_API_SLOT(58) - 1;
    a->file_open = f_open, a->file_read = f_read, a->file_write = f_write, a->file_close = f_close;
    a->device_setting_get = d_get, a->device_setting_set = d_set;
}

int main(void)
{
    app_api_t a;
    char b[PJ_CFG_MAX + 1];
    const char *t = "name=X\nhost=10.0.0.4\n";
    api_init(&a, 1);                                   /* API 2.11: the device setting, never the file */
    CHECK(prcfg_read(&a, b, sizeof(b)) < 0);           /* none set */
    CHECK(prcfg_write(&a, t, strlen(t)) == 0 && dev_writes == 1 && file_writes == 0 && !strcmp(dev_key, "printer"));
    CHECK(prcfg_read(&a, b, sizeof(b)) == (int)strlen(t) && !memcmp(b, t, strlen(t)));
    dev_set_rc = APP_EINVAL;
    CHECK(prcfg_write(&a, t, strlen(t)) < 0);          /* the firmware refuses: an error, not a silent success */
    dev_set_rc = 0;
    api_init(&a, 0);                                   /* API 2.10: printer.cfg, never the device setting */
    int dw = dev_writes;
    CHECK(prcfg_read(&a, b, sizeof(b)) < 0);
    CHECK(prcfg_write(&a, t, strlen(t)) == 0 && file_writes == 1 && dev_writes == dw);
    CHECK(prcfg_read(&a, b, sizeof(b)) == (int)strlen(t) && !memcmp(b, t, strlen(t)));
    return CHECK_DONE("test_prcfg");
}
