# trmnl-print

Print for the TRMNL X: lists files, shows a sheet preview, sends the page to a network printer (target: Brother MFC-L2710DW). Pages are PWG raster (`image/pwg-raster`, sGray 8 bit, 600 dpi, A4 4960 x 7016, line-repeat RLE; what the printer lists as its document formats) or, as a fallback, PCL5 raster sent as `application/octet-stream`, packed into IPP Print-Job requests and posted with the app API's `http_request`.

| Part | Where |
|---|---|
| PWG raster (header, row coding, line repeat) | `src/pwg.c` |
| PCL stream, PackBits (fallback) | `src/pcl.c` |
| IPP request header and answer status | `src/ipp.c` |
| picture to A4 (turned if wide, aspect kept, centred) | `src/imgfit.c` |
| text and Markdown headings to pages (word wrap, breaks, bands) | `src/textlay.c` |
| file list (grouped, newest first) | `src/flist.c` |
| jobs, error words, job splitting | `src/printjob.c` |
| controller and screens | `src/pr.c`, `src/prui.c` |
| app glue | `apps/trmnl-print/main.c` |

| printer setup (find, default, phone fallback, test page, `setup:<app>`) | `src/prsetup.c` |
| default printer file, address parsing, `pj_default_printer` | `src/printer.c` |

## The default printer
No address is built in. The app finds printers with API 2.10 `net_browse("_ipp._tcp")` (Printer row, or the first print); the choice is saved as `/appdata/trmnl-print/printer.cfg`: `name=`, `host=`, `port=` (631), `path=` (ipp/print), `format=` (pwg or pcl), one per line (`src/printer.h`). Any app reads it with `pj_default_printer(api, &p)` (API 2.8 `file_open_app`) and prints with `pj_print_png(&net, &p, ...)`: `PJ_NO_PRINTER` means "No printer yet: set one up in Print", offer `app_launch("trmnl-print", "setup:<this app>")`; Print returns to that app when a printer is chosen or Back is pressed. Fallback: "Enter address on the phone" (setting `printer` = `host[:port][/path]`, setting `format`). "Test page" prints a small page after a confirm.

## Files (API 2.8, `src/appfs.c`)
With `api->size >= APP_API_SLOT(53)` the list shows every app's files (`file_list_apps`, `file_list_app`, `file_open_app`): `.png .md .txt` up to 2 folders below each app's root, grouped by app, newest first by modification time (name order when the time is 0). Older firmware: only the print app's own folder, one sub folder per group, by name. Built against `trmnl-fw-files` until 2.8 is merged (`FW` in the Makefile). Raw TCP is still not in the API (IPP over `http_request` is used).

Limits: only 1-bit PNGs of Sketch's writer are read (`pngio.c`); a full text page is about 520 KB as PWG (150 KB as PCL), a job holds 1 MB, more pages go as further jobs; a page that alone exceeds the buffer is refused with a message. Unproven on the real printer.

`make test` (host, ASan/UBSan), `make shots`, `make render` (PWG through CUPS rastertopdf, PCL through gpcl6), `make elf`, `tools/mutate.sh` (red checks).
