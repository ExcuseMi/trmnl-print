/* Printer setup in the controller: finding, choosing the default, the phone fallback, the test page, "setup:<app>".
   SPDX-License-Identifier: MIT */
#include "check.h"
#include "rig.h"

static void no_default(rig_t *r)
{
    r->cfg_n = 0;
}

static void found(rig_t *r, const char *name, const char *host, int port, const char *path)
{
    r->br[r->nbr].name = name, r->br[r->nbr].host = host, r->br[r->nbr].port = port, r->br[r->nbr].path = path;
    r->nbr++;
}

/* from the list: Printer row, find (automatic without a default) */
static void open_finding(pr_t *p, rig_t *r)
{
    p->sel = 0;
    tap(p, APP_TOUCH_MIDDLE);
    if (p->find_req) pr_run_find(p);
    (void)r;
}

static void none_found(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    pr_t p;
    pr_init(&p, &r.env);
    open_finding(&p, &r);
    CHECK(p.mode == PR_PICK && r.browses == 1 && p.nfound == 0);
    CHECK(strstr(p.note, "No printer found") && strstr(p.note, "address"));
    CHECK(prs_items(&p) == 3 && prs_kind(&p, 0) == PK_FIND && prs_kind(&p, 1) == PK_PHONE && prs_kind(&p, 2) == PK_BACK);
    CHECK(!p.have_def && r.pr.posts == 0);                 /* nothing set, nothing printed */
    tap(&p, APP_TOUCH_RIGHT), tap(&p, APP_TOUCH_RIGHT);
    tap(&p, APP_TOUCH_MIDDLE);                              /* Back */
    CHECK(p.mode == PR_LIST && !p.have_def && r.cfg_writes == 0);
    pr_free(&p);
    rig_free(&r);
}

static void one_found_is_saved_and_used(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    found(&r, "Brother MFC-L2710DW series", "192.168.1.50", 631, "ipp/print");
    pr_t p;
    pr_init(&p, &r.env);
    open_finding(&p, &r);
    CHECK(p.mode == PR_PICK && p.nfound == 1 && prs_kind(&p, 0) == PK_FOUND);
    tap(&p, APP_TOUCH_MIDDLE);                              /* choose it */
    CHECK(p.mode == PR_LIST && p.have_def && r.cfg_writes == 1);
    pj_printer_t q;
    CHECK(pj_cfg_parse(r.cfg, (size_t)r.cfg_n, &q) == 0 && !strcmp(q.host, "192.168.1.50") && !strcmp(q.path, "ipp/print") &&
          !strcmp(q.name, "Brother MFC-L2710DW series"));
    CHECK(r.pr.posts == 0);                                 /* choosing never prints */
    pr_free(&p);
    pr_init(&p, &r.env);                                    /* a new start reads the saved choice, no search */
    CHECK(p.have_def && !strcmp(p.def.host, "192.168.1.50") && r.browses == 1);
    p.sel = find_sel(&p, "sk0012.png");
    tap(&p, APP_TOUCH_MIDDLE);
    tap(&p, APP_TOUCH_MIDDLE);                              /* Print */
    CHECK(p.mode == PR_PRINTING && p.print_req == 1);
    pr_run_print(&p);
    CHECK(r.pr.posts == 1 && !strcmp(r.pr.url, "http://192.168.1.50:631/ipp/print") && r.browses == 1);
    pr_free(&p);
    rig_free(&r);
}

static void several_found(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    found(&r, "A", "10.0.0.2", 631, "ipp/print");
    found(&r, "B", "10.0.0.3", 631, "printers/B");
    found(&r, "A again", "10.0.0.2", 631, "ipp/print");     /* the same printer twice */
    pr_t p;
    pr_init(&p, &r.env);
    open_finding(&p, &r);
    CHECK(p.nfound == 2 && prs_items(&p) == 5);
    tap(&p, APP_TOUCH_RIGHT);
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.have_def && !strcmp(p.def.host, "10.0.0.3") && !strcmp(p.def.path, "printers/B") && p.mode == PR_LIST);
    pr_free(&p);
    rig_free(&r);
}

static void search_errors(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    int codes[] = { APP_ENONET, APP_EBUSY, APP_ENOTSUP, APP_EIO };
    for (unsigned i = 0; i < 4; i++) {
        r.br_rc = codes[i];
        p.mode = PR_FINDING, p.find_req = 1;
        pr_run_find(&p);
        CHECK(p.mode == PR_PICK && p.nfound == 0 && p.note[0]);
        CHECK(prs_kind(&p, prs_items(&p) - 1) == PK_BACK);  /* the fallback and Back are always there */
    }
    CHECK(r.pr.posts == 0 && !strcmp(p.def.host, "10.0.0.7"));  /* the old default is untouched */
    pr_free(&p);
    rig_free(&r);
}

static void phone_fallback(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    pr_t p;
    pr_init(&p, &r.env);
    open_finding(&p, &r);                                   /* nothing found */
    tap(&p, APP_TOUCH_RIGHT);
    tap(&p, APP_TOUCH_MIDDLE);                              /* Enter address on the phone */
    CHECK(p.mode == PR_SETTINGS && p.settings_req == 1);
    tap(&p, APP_TOUCH_LEFT);                                /* leaves without typing: nothing changes */
    CHECK(p.mode == PR_PICK && !p.have_def && r.cfg_writes == 0);
    tap(&p, APP_TOUCH_MIDDLE);
    strcpy(r.printer, "10.0.0.20:9631");
    tap(&p, APP_TOUCH_MIDDLE);                              /* typed on the phone; leaving applies it */
    CHECK(p.have_def && !strcmp(p.def.host, "10.0.0.20") && p.def.port == 9631 && p.mode == PR_LIST);
    /* a stale phone value does not override a printer chosen from the list later */
    r.nbr = 0;
    found(&r, "Found", "10.0.0.2", 631, "ipp/print");
    tap(&p, APP_TOUCH_MIDDLE);                              /* the printer row, with a default: the menu */
    CHECK(p.mode == PR_PICK);
    p.mode = PR_FINDING, p.find_req = 1;
    pr_run_find(&p);
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(!strcmp(p.def.host, "10.0.0.2"));
    p.mode = PR_PICK, p.psel = prs_items(&p) - 3;
    CHECK(prs_kind(&p, p.psel) == PK_PHONE);
    tap(&p, APP_TOUCH_MIDDLE);
    tap(&p, APP_TOUCH_LEFT);                                /* the phone page still holds 10.0.0.20:9631 */
    CHECK(!strcmp(p.def.host, "10.0.0.2"));
    /* a bad address: said, not saved */
    p.mode = PR_PICK, p.psel = 0;
    p.nfound = 0;
    p.psel = 1;
    tap(&p, APP_TOUCH_MIDDLE);
    strcpy(r.printer, "not an address");
    int writes = r.cfg_writes;
    tap(&p, APP_TOUCH_LEFT);
    CHECK(p.mode == PR_PICK && strstr(p.note, "not valid") && r.cfg_writes == writes && !strcmp(p.def.host, "10.0.0.2"));
    /* the page format is part of the default printer */
    p.psel = 1;
    tap(&p, APP_TOUCH_MIDDLE);
    strcpy(r.format, "pcl");
    tap(&p, APP_TOUCH_LEFT);
    CHECK(p.def.format == PJ_PCL && strstr(r.cfg, "format=pcl"));
    pr_free(&p);
    rig_free(&r);
}

static void save_fails(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    found(&r, "A", "10.0.0.2", 631, "ipp/print");
    r.cfg_fail_write = 1;
    pr_t p;
    pr_init(&p, &r.env);
    open_finding(&p, &r);
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PICK && !p.have_def && strstr(p.note, "could not be saved"));
    pr_free(&p);
    rig_free(&r);
}

static void print_without_printer_finds_first(void)
{
    static rig_t r;
    rig_new(&r);
    no_default(&r);
    found(&r, "A", "10.0.0.2", 631, "ipp/print");
    pr_t p;
    pr_init(&p, &r.env);
    p.sel = find_sel(&p, "sk0012.png");
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PREVIEW);
    tap(&p, APP_TOUCH_MIDDLE);                              /* Print, but no printer yet */
    CHECK(p.mode == PR_FINDING && p.find_req && !p.print_req);
    pr_run_find(&p);
    tap(&p, APP_TOUCH_MIDDLE);                              /* choose */
    CHECK(p.mode == PR_PREVIEW && p.data && p.have_def && r.pr.posts == 0);  /* back on the preview, still unprinted */
    tap(&p, APP_TOUCH_MIDDLE);
    pr_run_print(&p);
    CHECK(r.pr.posts == 1 && p.mode == PR_RESULT);
    pr_free(&p);
    rig_free(&r);
}

static void test_page(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    tap(&p, APP_TOUCH_MIDDLE);                              /* the menu */
    int test_i = -1;
    for (int i = 0; i < prs_items(&p); i++)
        if (prs_kind(&p, i) == PK_TEST) test_i = i;
    CHECK(test_i >= 0);
    p.psel = test_i;
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_TESTASK && r.pr.posts == 0);         /* asks first */
    tap(&p, APP_TOUCH_LEFT);
    CHECK(p.mode == PR_PICK && r.pr.posts == 0 && !p.print_req);
    p.psel = test_i;
    tap(&p, APP_TOUCH_MIDDLE);
    tap(&p, APP_TOUCH_MIDDLE);                              /* Print */
    CHECK(p.mode == PR_PRINTING && p.print_req && p.test);
    pr_run_print(&p);
    CHECK(p.mode == PR_RESULT && r.pr.posts == 1 && !strcmp(p.note, "Sent 1 page to the printer"));
    CHECK(!strcmp(r.pr.url, "http://10.0.0.7:631/ipp/print"));
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.mode == PR_PICK && !p.test);                    /* OK goes back to the menu */
    /* the printer is off: said on the result screen */
    r.pr.status = PJ_NET_UNREACHABLE;
    p.psel = test_i;
    tap(&p, APP_TOUCH_MIDDLE);
    tap(&p, APP_TOUCH_MIDDLE);
    pr_run_print(&p);
    CHECK(!strcmp(p.note, "Printer not reachable") && r.pr.posts == 2);
    /* no default: the menu has no test page */
    pr_free(&p);
    no_default(&r);
    pr_init(&p, &r.env);
    open_finding(&p, &r);
    for (int i = 0; i < prs_items(&p); i++) CHECK(prs_kind(&p, i) != PK_TEST);
    p.test = 1, p.have_def = 0;
    CHECK(prs_print_test(&p) < 0 && strstr(p.res.msg, "No printer"));
    pr_free(&p);
    rig_free(&r);
}

static void setup_argument(void)
{
    static rig_t r;
    rig_new(&r);
    pr_t p;
    pr_init(&p, &r.env);
    pr_start_arg(&p, "setup:sketch");                        /* with a default: the menu, no search */
    CHECK(p.mode == PR_PICK && !strcmp(p.caller, "sketch") && p.setup && !p.find_req && !p.back_req);
    p.psel = prs_items(&p) - 1;                             /* Back */
    tap(&p, APP_TOUCH_MIDDLE);
    CHECK(p.back_req == 1 && r.pr.posts == 0);
    pr_free(&p);

    no_default(&r);
    found(&r, "A", "10.0.0.2", 631, "ipp/print");
    pr_init(&p, &r.env);
    pr_start_arg(&p, "setup:sketch");                        /* no default: finds at once */
    CHECK(p.mode == PR_FINDING && p.find_req);
    pr_run_find(&p);
    tap(&p, APP_TOUCH_MIDDLE);                              /* choosing a printer ends the setup: back to the caller */
    CHECK(p.have_def && p.back_req == 1 && !strcmp(p.caller, "sketch"));
    pr_free(&p);

    pr_init(&p, &r.env);
    pr_start_arg(&p, "setup:sketch");
    p.psel = 1;                                          /* the phone page */
    tap(&p, APP_TOUCH_MIDDLE);
    strcpy(r.printer, "10.0.0.30");
    tap(&p, APP_TOUCH_LEFT);
    CHECK(!strcmp(p.def.host, "10.0.0.30") && p.back_req == 1);
    pr_free(&p);

    const char *bad[] = { "setup:", "setup:../x", "setup:a b", "setup:a/b" };
    for (unsigned i = 0; i < 4; i++) {
        pr_init(&p, &r.env);
        pr_start_arg(&p, bad[i]);
        CHECK(p.mode == PR_LIST && !p.setup && !p.caller[0] && p.note[0]);
        pr_free(&p);
    }
    rig_free(&r);
}

int main(void)
{
    none_found();
    one_found_is_saved_and_used();
    several_found();
    search_errors();
    phone_fallback();
    save_fails();
    print_without_printer_finds_first();
    test_page();
    setup_argument();
    return CHECK_DONE("test_setup");
}
