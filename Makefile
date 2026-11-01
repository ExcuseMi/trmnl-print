# Host build: tests with sanitizers, the shots tool, the baked fonts, the PCL render (make render). The app ELF is built by the firmware fork's
# apps/build.sh from apps/trmnl-print (run `make fonts` first).
CC ?= gcc
# API 2.9 (app_launch, mouse mode) is in trmnl-fw-files (merged)
FW ?= $(HOME)/workspace/trmnl-fw-files
PY ?= $(HOME)/workspace/trmnl-fpv/.venv/bin/python
CFLAGS = -Itest -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Isrc -Ihost -Igen -I$(FW)/include -fsanitize=address,undefined -fno-sanitize-recover=all
CORE = src/pcl.c src/pwg.c src/ipp.c src/imgfit.c src/textlay.c src/flist.c src/appfs.c src/printjob.c src/pr.c src/prcfg.c src/prsetup.c src/printer.c src/prui.c src/pngio.c src/font.c src/gfx.c gen/fonts_gen.c
HOST = build/tbfw.o
TESTS = $(patsubst test/%.c,build/%,$(wildcard test/test_*.c))

all: test shots
fonts: gen/fonts_gen.c
gen/fonts_gen.c: tools/bake_fonts.py fonts/sizes.txt $(wildcard fonts/*.ttf)
	@mkdir -p gen
	$(PY) tools/bake_fonts.py $@
build/tbfw.o: host/tbfw.cpp $(FW)/lib/trmnl/src/touchbar_layout.cpp
	@mkdir -p build
	g++ -std=gnu++11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -I$(FW)/include -I$(FW)/lib/trmnl/include -I$(FW)/lib/trmnl/src -c $< -o $@
build/%: test/%.c $(CORE) $(HOST) test/check.h $(wildcard test/*.h src/*.h host/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) $< $(CORE) $(HOST) -o $@ -lm -lz -lstdc++
test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done
build/shots: host/shots.c host/png.c $(CORE) $(HOST) $(wildcard src/*.h host/*.h)
	@mkdir -p build
	$(CC) $(CFLAGS) host/shots.c host/png.c $(CORE) $(HOST) -o $@ -lz -lm -lstdc++
shots: build/shots
	@mkdir -p shots
	./build/shots shots
clean:
	rm -rf build shots gen
.PHONY: all fonts test shots clean
# The app ELF, with the firmware fork's import check (every libc call must be in the loader's table).
elf: gen/fonts_gen.c
	@mkdir -p build/elf; d=$${TMPDIR:-/tmp}/print-elf-$$$$; rm -rf $$d; \
	  OUT=$$d $(FW)/apps/build.sh $(CURDIR)/apps/trmnl-print >/dev/null || { rm -rf $$d; exit 1; }; \
	  cp $$d/trmnl-print.elf build/elf/; rm -rf $$d; ls -l build/elf/trmnl-print.elf; sha256sum build/elf/trmnl-print.elf
.PHONY: elf
# Looks at what the app sends, rendered by software that is not ours (no printer involved): PWG raster through CUPS'
# rastertopdf and ghostscript, PCL through Artifex's gpcl6. make render, then view shots/r/*.png
render: shots
	@mkdir -p shots/r
	docker run --rm -v $(CURDIR)/shots:/w debian:12 sh -c 'apt-get update -qq >/dev/null && apt-get install -y -qq --no-install-recommends cups-filters ghostscript >/dev/null 2>&1; \
	  for j in sketch text; do /usr/lib/cups/filter/rastertopdf 1 u t 1 "" /w/$$j.pwg > /w/r/$$j.pdf 2>/dev/null; gs -q -dNOPAUSE -dBATCH -sDEVICE=pnggray -r110 -sOutputFile=/w/r/$$j-pwg-%d.png /w/r/$$j.pdf; done; chmod -R a+rw /w/r'
	@docker build -q -t tp-gpcl6 tools/gpcl >/dev/null; for j in sketch text; do docker run --rm -v $(CURDIR)/shots:/w tp-gpcl6 -dNOPAUSE -dBATCH -sDEVICE=pnggray -r110 -sOutputFile=/w/r/$$j-pcl-%d.png /w/$$j.pcl; done
.PHONY: render
