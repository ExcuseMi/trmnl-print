/* The firmware's file calls behind the list and the reader: other apps' folders from API 2.8 (api->size >= SLOT(53)),
   else only the print app's own folder, as before. SPDX-License-Identifier: MIT */
#ifndef APPFS_H
#define APPFS_H
#include <app_api.h>
#include "flist.h"

#define APPFS_READ_MAX (1024 * 1024)

typedef struct {
    const app_api_t *api;
    fl_fs_t fs; /* for pr_env_t.files */
} appfs_t;

void appfs_init(appfs_t *a, const app_api_t *api);
/* Reads the entry's file whole (up to APPFS_READ_MAX) into a malloc'd buffer; < 0 on any failure, nothing allocated */
int appfs_read(void *u, const fl_entry_t *e, uint8_t **buf, size_t *n);
#endif
