/* The Print app's own read and write of the default printer text (pr_env_t cfg_read / cfg_write). SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0 */
#ifndef PRCFG_H
#define PRCFG_H
#include "printer.h"
/* The printer.cfg text (cap >= PJ_CFG_BUF on API 2.11): bytes, or < 0 when there is none */
int prcfg_read(const app_api_t *api, char *buf, size_t cap);
/* 0, or < 0 (the firmware's checks refuse it, or the file cannot be written) */
int prcfg_write(const app_api_t *api, const char *text, size_t n);
#endif
