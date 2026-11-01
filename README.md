# trmnl-print

Print for the TRMNL X: lists files, shows a sheet preview, sends the page to a network printer (target: Brother MFC-L2710DW). Pages are PCL5 raster (1 bit, 300 dpi, A4), packed into IPP Print-Job requests and posted with the app API's `http_request`.

| Part | Where |
|---|---|
| PCL stream, PackBits | `src/pcl.c` |
| IPP request header and answer status | `src/ipp.c` |
| picture to A4 (turned if wide, aspect kept, centred) | `src/imgfit.c` |
| text and Markdown headings to pages (word wrap, breaks, bands) | `src/textlay.c` |
| file list (grouped, newest first) | `src/flist.c` |
| jobs, error words, job splitting | `src/printjob.c` |
| controller and screens | `src/pr.c`, `src/prui.c` |
| app glue | `apps/trmnl-print/main.c` |

Setting `printer` (phone settings page): `host` or `host:port`, default 192.0.2.40, port 631.

## Blockers of the app API (firmware fork 2.7), nothing changed there
| Need | Today | Smallest addition |
|---|---|---|
| read other apps' files | `file_*` is limited to `/appdata/<app>/`, no `..` | `file_list_app(app, dir, cb, ctx)` and `file_open_app(app, path)`, read only (API 2.8). Until then the app lists its own folder, one sub folder per source app |
| raw TCP (port 9100, streaming) | none; the loader's table does export `lwip_socket/connect/send/recv`, not in the API | `tcp_connect(host, port, timeout_ms)`, `tcp_send`, `tcp_close`. Only `printjob.c` changes |
| file dates | `file_list` gives name and size | newest first is by name (natural order) |

Limits: only 1-bit PNGs of Sketch's writer are read (`pngio.c`); IPP needs the printer to accept `application/vnd.hp-PCL` on port 631 (unproven); a text page is about 150 KB as PCL, a job holds 1 MB, more pages go as further jobs.

`make test` (host, ASan/UBSan), `make shots`, `make render` (PCL to PNG with gpcl6), `make elf`, `tools/mutate.sh` (red checks).
