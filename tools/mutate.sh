#!/bin/bash
# Red checks: each line breaks one thing in the source and expects its test to fail (run after changing a test; prints GREEN(!) for a guard that does not catch it).
cd "$(dirname "$0")/.."
run() {
  f=$1; e=$2; t=$3
  cp $f /tmp/mut-backup.c
  sed -i "$e" $f
  if cmp -s $f /tmp/mut-backup.c; then echo "NOCHANGE $f $e"; return; fi
  if ! make build/$t >/dev/null 2>&1; then echo "BUILDFAIL(!) $t <- $f: $e"; cp /tmp/mut-backup.c $f; return; fi
  out=$(./build/$t 2>&1)
  if echo "$out" | grep -q "FAILED\|ERROR: AddressSanitizer\|runtime error"; then echo "RED   $t  <- $f: $e"; else echo "GREEN(!) $t <- $f: $e"; fi
  cp /tmp/mut-backup.c $f
}
run src/pcl.c 's/row\[i\] = (uint8_t)~row\[i\];/row[i] = row[i];/' test_pcl
run src/pcl.c 's/while (i + run < n \&\& run < 128/while (i + run < n \&\& run < 129/' test_pcl
run src/pcl.c 's/x = r->x - PCL_OFF_X/x = r->x/' test_pcl
run src/imgfit.c 's/f->x = (pw - dw) \/ 2;/f->x = (pw - dw) \/ 2 + 10;/' test_fit
run src/imgfit.c 's/f->rotate = sw > sh;/f->rotate = sw < sh;/' test_fit
run src/imgfit.c 's/if (black \* 2 >= all)/if (black * 2 > all)/' test_fit
run src/textlay.c 's/<= (int32_t)t->width \* 64/<= (int32_t)t->width * 128/' test_text
run src/textlay.c 's/y + space + pitch(f) > t->page_h/y + space + pitch(f) > t->page_h * 2/' test_text
run src/textlay.c 's/if (n) gap += pitch(t->face\[0\]) \/ 2;/if (n) gap += 0;/' test_text
run src/flist.c 's/return c ? c : fl_natcmp(b->name, a->name);/return c ? c : fl_natcmp(a->name, b->name);/' test_flist
run src/flist.c 's/if (l > 12 \&\& (!strcmp(n + l - 12, "-cleared.png"))) return -1;//' test_flist
run src/printjob.c 's/if (ipp >= 0x100)/if (ipp >= 0x10000)/' test_job
run src/printjob.c 's/if (st == PJ_NET_TIMEOUT)/if (st == 12345)/' test_job
run src/printjob.c 's/j->b.n = j->mark; \/\* drop the half page \*\//;/' test_job
run src/printjob.c 's/if (j->mark == j->header)/if (0)/' test_job
run src/pr.c 's/p->mode = PR_PRINTING, p->print_req = 1;/p->mode = PR_PRINTING;/' test_pr
run src/pr.c 's/set_note(p, "That file could not be read");/set_note(p, "");/' test_pr
run src/pwg.c 's/d\[o++\] = (uint8_t)(run - 1);/d[o++] = (uint8_t)run;/' test_pwg
run src/pwg.c 's/put32(h, 400, 18);/put32(h, 400, 19);/' test_pwg
run src/pwg.c 's/l->repeat < 256 \&\&/l->repeat < 257 \&\&/' test_pwg
run src/pwg.c 's/d\[o++\] = (uint8_t)(257 - (i - st));/d[o++] = (uint8_t)(256 - (i - st));/' test_pwg
run src/pwg.c 's/put32(h, 276, PWG_DPI), put32(h, 280, PWG_DPI);/put32(h, 276, 300), put32(h, 280, 300);/' test_pwg
run src/pwg.c 's/if (c == 128) return -1;//' test_pwg
run src/printjob.c 's/j->net->format == PJ_PCL ? IPP_PCL : IPP_PWG/j->net->format == PJ_PCL ? IPP_PWG : IPP_PCL/' test_job
run src/printjob.c 's/pwg_page(&o, reg, scale)/pwg_page(\&o, reg, 1 + 0 * scale)/' test_job
run src/pr.c 's/net.format = fn == 3 \&\& !memcmp(fmt, "pcl", 3) ? PJ_PCL : PJ_PWG;/net.format = fn > 100 ? PJ_PCL : PJ_PWG;/' test_pr
run src/imgfit.c 's/int m = IMG_MARGIN \* pw \/ 2480;/int m = IMG_MARGIN;/' test_fit
