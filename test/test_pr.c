#define _GNU_SOURCE
/* The controller: list paging, preview, print flow, the printer setting, errors on the screen. SPDX-License-Identifier: MIT */
#include "check.h"
#include "rig.h"

static void navigation(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    CHECK(p.list.n == 15 && p.sel == 0 && p.mode == PR_LIST);
    tap(&p, APP_TOUCH_LEFT);
    CHECK(p.sel == 0);                                    /* not before the first row */
    tap(&p, APP_TOUCH_RIGHT);
    tap(&p, APP_TOUCH_RIGHT);
    CHECK(p.sel == 2);
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_SWIPE_LEFT);
    CHECK(p.sel == 2 + PR_ROWS);
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_SWIPE_LEFT);
    CHECK(p.sel == p.list.n);                              /* stops at the last row */
    for (int i = 0; i < 20; i++) tap(&p, APP_TOUCH_RIGHT);
    CHECK(p.sel == p.list.n);
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_SWIPE_RIGHT);
    CHECK(p.sel == p.list.n - PR_ROWS);
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_SWIPE_RIGHT);
    pr_touch(&p, APP_TOUCH_MIDDLE, APP_GESTURE_SWIPE_RIGHT);
    CHECK(p.sel == 0);
    pr_free(&p);
    rig_free(&r);
}

static void printer_row(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    char a[160];
    pr_printer(&p, a, sizeof(a));
    CHECK(!strcmp(a, "Test printer  10.0.0.7"));
    tap(&p, APP_TOUCH_MIDDLE);                              /* on the printer row, with a default: the menu */
    CHECK(p.mode == PR_PICK && !p.find_req && r.browses == 0);
    pr_free(&p);
    rig_default(&r, "");                                    /* no default printer: nothing is assumed */
    r.cfg_n = 0;
    pr_init(&p, &r.env);
    pr_printer(&p, a, sizeof(a));
    CHECK(!a[0] && !p.have_def);
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_FINDING && p.find_req == 1);         /* looks at once */
    pr_free(&p);
    rig_free(&r);
}

static void preview_and_print(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    p.sel = find_sel(&p, "sk0012.png");
    CHECK(p.sel == 2);                                      /* the newest sketch first (sk0099 is the not-a-PNG one) */
    p.sel = find_sel(&p, "sk0012.png");
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PREVIEW && p.px && p.pw == 400);
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PRINTING && p.print_req == 1);
    pr_run_print(&p);
    CHECK(p.mode == PR_RESULT && p.print_req == 0 && r.pr.posts == 1);
    CHECK(!strcmp(p.note, "Sent 1 page to the printer"));
    CHECK(!strcmp(r.pr.url, "http://10.0.0.7:631/ipp/print"));
    CHECK(memmem(r.pr.body[0], r.pr.len[0], "image/pwg-raster", 16));        /* the default format */
    rig_default(&r, "host=10.0.0.7\nformat=pcl\n");
    prs_load(&p);
    p.mode = PR_PREVIEW, p.print_req = 1;
    pr_run_print(&p);
    CHECK(memmem(r.pr.body[1], r.pr.len[1], "application/octet-stream", 24)); /* the file says format=pcl */
    rig_default(&r, "host=10.0.0.7\nformat=junk\n");
    prs_load(&p);
    p.mode = PR_PREVIEW, p.print_req = 1;
    pr_run_print(&p);
    CHECK(memmem(r.pr.body[2], r.pr.len[2], "image/pwg-raster", 16));        /* anything else is the default */
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PREVIEW);                            /* back to the preview */
    tap(&p, APP_TOUCH_RIGHT);                               /* next file */
    CHECK(p.mode == PR_PREVIEW && !strcmp(p.list.e[p.sel - 1].name, "sk0011.png"));
    tap(&p, APP_TOUCH_LEFT);
    CHECK(p.mode == PR_LIST && !p.data && !p.px);
    /* a text file */
    p.sel = find_sel(&p, "diary.md");
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PREVIEW && p.data && !p.px);
    tap(&p, APP_TOUCH_MIDDLE);
    pr_run_print(&p);
    CHECK(p.mode == PR_RESULT && r.pr.posts == 4);
    pr_free(&p);
    rig_free(&r);
}

static void errors_on_screen(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    /* a file that cannot be read, and one that is not a PNG: a message on the list, no preview, no crash */
    p.sel = find_sel(&p, "broken.txt");
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_LIST && strstr(p.note, "could not be read") && !p.data);
    p.sel = find_sel(&p, "sk0099.png");
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_LIST && strstr(p.note, "cannot be printed") && !p.data && !p.px);
    /* the printer is off: the words are on the result screen, the app goes on */
    p.sel = find_sel(&p, "sk0001.png");
    tap(&p, APP_TOUCH_MIDDLE);
    r.pr.status = PJ_NET_UNREACHABLE;
    tap(&p, APP_TOUCH_MIDDLE);
    pr_run_print(&p);
    CHECK(p.mode == PR_RESULT && !strcmp(p.note, "Printer not reachable"));
    tap(&p, APP_TOUCH_MIDDLE);
    r.pr.status = 200;
    tap(&p, APP_TOUCH_MIDDLE);
    pr_run_print(&p);
    CHECK(!strcmp(p.note, "Sent 1 page to the printer"));
    /* nothing to list at all */
    ffs_free(&r.fs);
    r.fs.count = 0;
    pr_rescan(&p);
    CHECK(p.list.n == 0 && p.sel == 0);
    tap(&p, APP_TOUCH_RIGHT);
    CHECK(p.sel == 0);
    r.fs.fail_list_root = 1;                                /* the listing itself fails */
    pr_rescan(&p);
    CHECK(p.list.n == 0 && strstr(p.note, "could not be listed"));
    pr_free(&p);
    rig_free(&r);
}

static void drawing(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    size_t sz = (size_t)(1872 / 8) * 1404;
    uint8_t *fb = malloc(sz);
    for (int orient = 0; orient < 4; orient++) {
        app_touchbar_t bar;
        tbfw_layout(orient, &bar);
        gfx_t g;
        gfx_init(&g, fb, 1872, 1404, 1872 / 8, orient);
        for (int mode = 0; mode < 8; mode++) {
            pr_free(&p);
            p.mode = mode;
            p.sel = mode == PR_PREVIEW ? 2 : mode == PR_LIST ? 3 : 0;
            if (mode == PR_PREVIEW) tap(&p, APP_TOUCH_MIDDLE), p.mode = PR_LIST, p.sel = 2, tap(&p, APP_TOUCH_MIDDLE);
            strcpy(p.share[0], "1. With your phone");
            strcpy(p.share[2], "http://writer.local");
            p.mode = mode;
            if (mode == PR_PREVIEW && !p.px) {
                p.sel = find_sel(&p, "sk0012.png");
                p.mode = PR_LIST;
                tap(&p, APP_TOUCH_MIDDLE);
            }
            memset(fb, 0xff, sz);
            pr_draw(&p, &g, &bar);
            int ink = 0;
            for (size_t i = 0; i < sz; i++) ink |= fb[i] != 0xff;
            CHECK(ink);
        }
    }
    free(fb);
    pr_free(&p);
    rig_free(&r);
}

static void start_argument(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    pr_start_arg(&p, "sketch:sk0003.png"); /* straight on the preview, Next file is off */
    CHECK(p.mode == PR_PREVIEW && p.px && !strcmp(p.caller, "sketch") && !strcmp(p.cur.name, "sk0003.png"));
    tap(&p, APP_TOUCH_RIGHT);
    CHECK(p.mode == PR_PREVIEW && !strcmp(p.cur.name, "sk0003.png"));
    tap(&p, APP_TOUCH_MIDDLE); /* print */
    CHECK(p.print_req);
    pr_run_print(&p);
    CHECK(r.pr.posts >= 1 && p.mode == PR_RESULT && !p.back_req);
    tap(&p, APP_TOUCH_MIDDLE); /* OK: back to the caller */
    CHECK(p.back_req == 1);
    pr_free(&p);

    pr_init(&p, &r.env);
    pr_start_arg(&p, "sketch:sk0003.png");
    tap(&p, APP_TOUCH_LEFT); /* Back */
    CHECK(p.back_req == 1 && !p.data);
    pr_free(&p);

    const char *bad[] = { "sketch:sk0500.png", "sketch:../x.png", "sketch:/etc/a.png", "sketch:a.exe", "sketch:", "sketch:sk0099.png", "sketch:writer/broken.txt" };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        pr_init(&p, &r.env);
        pr_start_arg(&p, bad[i]); /* a plain message, no crash, nothing printed, back to the caller */
        CHECK(p.mode == PR_RESULT && p.note[0] && !p.data);
        tap(&p, APP_TOUCH_MIDDLE);
        CHECK(p.back_req == 1);
        pr_free(&p);
    }
    pr_init(&p, &r.env);
    pr_start_arg(&p, "no colon here"); /* not our format: the plain list, no caller */
    CHECK(p.mode == PR_LIST && !p.caller[0] && p.note[0]);
    pr_start_arg(&p, "");
    pr_free(&p);
    rig_free(&r);
}

/* Every refusal of the phone page has its own line (2026-11-02: a refused settings form said only "could not start") */
static void share_refusals(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    app_webshare_status_t st;
    memset(&st, 0, sizeof(st));
    const int rcs[] = { APP_ENOMEM, APP_ENONET, APP_ENOENT, APP_EBUSY, APP_EINVAL };
    char seen[5][sizeof(p.share[0])];
    for (unsigned i = 0; i < 5; i++) {
        pr_set_share(&p, rcs[i], &st);
        CHECK(p.share[0][0]);
        memcpy(seen[i], p.share[0], sizeof(seen[i]));
        for (unsigned j = 0; j < i; j++) CHECK(strcmp(seen[i], seen[j]) != 0);
    }
    pr_free(&p);
    rig_free(&r);
}

int main(void)
{
    share_refusals();
    start_argument();
    navigation();
    printer_row();
    preview_and_print();
    errors_on_screen();
    drawing();
    return CHECK_DONE("test_pr");
}
