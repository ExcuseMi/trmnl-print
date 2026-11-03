/* Where the Print app keeps the default printer: the X's device settings on API 2.11+, else its own printer.cfg.
   SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#include "prcfg.h"
#include <string.h>

int prcfg_read(const app_api_t *api, char *buf, size_t cap)
{
    if (api->size >= APP_API_SLOT(58)) {
        int n = pj_device_text(api, buf, cap);
        return n == PJ_NO_PRINTER ? -1 : n;
    }
    app_file_t *f = api->file_open(PJ_CFG_FILE, APP_FILE_READ);
    if (!f) return -1;
    size_t len = 0;
    int k = 0;
    while (len < cap && (k = api->file_read(f, buf + len, cap - len)) > 0) len += (size_t)k;
    api->file_close(f);
    return k < 0 ? -1 : (int)len;
}

int prcfg_write(const app_api_t *api, const char *text, size_t n)
{
    if (api->size >= APP_API_SLOT(58)) return api->device_setting_set(PJ_DEV_KEY, text, n) == 0 ? 0 : -1;
    app_file_t *f = api->file_open(PJ_CFG_FILE, APP_FILE_WRITE);
    if (!f) return -1;
    int k = api->file_write(f, text, n);
    int c = api->file_close(f);
    return k == (int)n && c >= 0 ? 0 : -1;
}
