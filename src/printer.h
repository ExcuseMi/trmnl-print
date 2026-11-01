/* The default printer: what the Print app sets up and any app reads. One small text file in the Print app's data
   folder, /appdata/trmnl-print/printer.cfg, readable by every app through API 2.8 (file_open_app):
       name=Brother MFC-L2710DW series
       host=192.168.1.50
       port=631
       path=ipp/print
       format=pwg
   One key=value per line; unknown keys are ignored; host is required, the rest default (port 631, path ipp/print, pwg).
   SPDX-License-Identifier: MIT */
#ifndef PRINTER_H
#define PRINTER_H
#include <stddef.h>
#include <app_api.h>

#define PJ_CFG_APP "trmnl-print"
#define PJ_CFG_FILE "printer.cfg"
#define PJ_CFG_MAX 512
#define PJ_DEFAULT_PORT 631
#define PJ_DEFAULT_PATH "ipp/print"
#define PJ_DEV_KEY "printer" /* API 2.11: device_setting_get/set key; the value is the same text, up to PJ_CFG_MAX + 1 bytes */
#define PJ_NO_PRINTER (-20) /* pj_default_printer: no printer set up (no file, or one that holds no usable host) */

typedef struct {
    char name[64];  /* the printer's own name, "" if unknown */
    char host[64];  /* IP address or name */
    int port;
    char path[48];  /* IPP resource path without a leading slash; "" is read as ipp/print */
    int format;     /* PJ_PWG (0) or PJ_PCL (1), see printjob.h */
} pj_printer_t;

/* "host[:port][/path]" (what the phone page takes). 0, or < 0 for an empty or bad address */
int pj_printer_parse(const char *s, pj_printer_t *p);
/* The same text from a printer, "host[:port][/path]" with the port and path left out when they are the defaults */
void pj_printer_addr(const pj_printer_t *p, char *out, size_t cap);
/* The file's text. Length, or < 0 if it does not fit in cap */
int pj_cfg_format(const pj_printer_t *p, char *out, size_t cap);
/* 0, or PJ_NO_PRINTER when the text holds no usable host */
int pj_cfg_parse(const char *text, size_t n, pj_printer_t *p);
/* API 2.11 (api->size >= APP_API_SLOT(58)): the "printer" device setting into buf (cap >= PJ_CFG_MAX + 1). Its length,
   PJ_NO_PRINTER (none set), APP_EIO, or APP_ENOTSUP (older firmware or bad arguments) */
int pj_device_text(const app_api_t *api, char *buf, size_t cap);
/* For any app: reads the default printer, from the X's device settings on API 2.11+, else from the Print app's file. 0 and *out, PJ_NO_PRINTER (tell the person "No
   printer yet: set one up in Print" and offer app_launch("trmnl-print", "setup:<this app>")), APP_ENOTSUP (the
   firmware is older than API 2.8) or APP_EIO */
int pj_default_printer(const app_api_t *api, pj_printer_t *out);
#endif
