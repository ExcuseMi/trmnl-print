/* Print for the TRMNL X: list files, preview, print on a network printer (IPP with PCL pages over http_request: the
   app API has no raw socket). Everything it decides is in src/pr.c, tested on the host. SPDX-License-Identifier: MIT */
#include <app_api.h>
#include <stdlib.h>
#include <string.h>
#include "app_orient.h"
#include "appfs.h"
#include "pr.h"

APP_INFO("Print", 48 * 1024);

static const app_api_t *api;
static pr_t pr;
static pr_env_t env;

static appfs_t afs;

static int cfg_get(void *u, const char *k, char *b, int cap) { return (void)u, api->setting_get(k, b, (size_t)cap); }

/* ---- the network: one HTTP POST per job ---- */

static int f_post(void *u, const char *url, const void *body, size_t n, uint8_t *resp, size_t cap, size_t *rn)
{
    (void)u;
    static const char *const hdr[] = { "Content-Type: application/ipp", 0 };
    app_http_request_t rq = { "POST", url, hdr, body, n, 30000 };
    app_http_response_t rs;
    memset(&rs, 0, sizeof(rs));
    *rn = 0;
    if (!api->net_up()) return PJ_NET_NO_WIFI;
    int rc = api->http_request(&rq, resp, cap, &rs);
    if (rc == APP_ENONET || rs.status == APP_ENONET) return PJ_NET_NO_WIFI;
    if (rc == APP_ETIMEDOUT || rs.status == APP_ETIMEDOUT) return PJ_NET_TIMEOUT;
    if (rs.status <= 0) return PJ_NET_UNREACHABLE;
    *rn = rs.body_len < cap ? rs.body_len : cap;
    return rs.status;
}

static const app_setting_decl_t decl[] = {
    { "printer", "Printer address (IP, or IP:port; the port is 631 if left out)", 0, 0, 64, APP_SETTING_TEXT },
    { "format", "Page format (pwg: image/pwg-raster, pcl: PCL as octet-stream)", "pwg|pcl", 0, 0, APP_SETTING_CHOICE },
};

static void push(int full)
{
    api->screen_refresh(0, full ? APP_REFRESH_FULL : APP_REFRESH_PARTIAL);
}

int app_main(const app_api_t *a)
{
    api = a;
    uint8_t *fb = api->screen_set_mode(APP_SCREEN_1BIT);
    if (!fb) return APP_ENOMEM;
    int fw = api->screen_width(), fh = api->screen_height(), stride = api->screen_stride();
    int orient = ao_start(api);
    app_touchbar_t bar;
    ao_layout(api, orient, fw, fh, &bar);
    gfx_t g;
    gfx_init(&g, fb, fw, fh, stride, orient);
    int has_settings = api->size >= APP_API_SLOT(47);
    if (has_settings) api->settings_declare(decl, 2);
    appfs_init(&afs, api);
    env.files = afs.fs;
    env.read = appfs_read;
    env.u = &afs;
    env.cfg_get = cfg_get;
    env.net = (pj_net_t){ PJ_PWG, f_post, 0 };
    pr_init(&pr, &env);
    int sharing = 0, share_rc = 0;
    uint32_t sec = 0;
    pr_draw(&pr, &g, &bar);
    push(1);
    for (;;) {
        app_any_event_t u;
        int exit_now = 0, dirty = 0, full = 0;
        uint32_t wait = pr.mode == PR_SETTINGS ? 500 : 5000;
        for (int k = 0; k < 16 && !exit_now && api->input_poll(&u.ev, k ? 0 : wait); k++) {
            switch (u.ev.type) {
            case APP_EVENT_EXIT: exit_now = 1; break;
            case APP_EVENT_TOUCH: {
                int before = pr.mode, sel = pr.sel;
                pr_touch(&pr, u.ev.zone, u.ev.gesture);
                dirty = 1;
                full = pr.mode != before || (pr.sel / PR_ROWS) != (sel / PR_ROWS);
                break;
            }
            case APP_EVENT_ORIENTATION:
                if (u.ev.key <= APP_ORIENT_BAR_RIGHT && (int)u.ev.key != orient) {
                    orient = (int)u.ev.key;
                    ao_layout(api, orient, fw, fh, &bar);
                    gfx_set_orient(&g, orient);
                    dirty = full = 1;
                }
                break;
            default: break;
            }
        }
        if (exit_now) break;
        if (pr.settings_req > 0) {
            share_rc = has_settings ? api->webshare_start_settings(APP_WEBSHARE_AUTO) : APP_ENOTSUP;
            sharing = share_rc == 0;
            pr.settings_req = 0;
            sec = 0;
        } else if (pr.settings_req < 0) {
            if (sharing) api->webshare_stop();
            sharing = 0;
            pr.settings_req = 0;
        }
        if (pr.mode == PR_SETTINGS && api->millis() / 1000 != sec) {
            sec = api->millis() / 1000;
            app_webshare_status_t ws;
            memset(&ws, 0, sizeof(ws));
            if (sharing) api->webshare_status(&ws);
            char keep[sizeof(pr.share)];
            memcpy(keep, pr.share, sizeof(keep));
            pr_set_share(&pr, share_rc, &ws);
            if (memcmp(keep, pr.share, sizeof(keep))) dirty = 1;
        }
        if (pr.print_req) { /* show "Printing", then block while the job goes out */
            pr_draw(&pr, &g, &bar);
            push(1);
            pr_run_print(&pr);
            dirty = full = 1;
        }
        if (dirty) {
            pr_draw(&pr, &g, &bar);
            push(full);
        }
    }
    if (sharing) api->webshare_stop();
    pr_free(&pr);
    return 0;
}
