# Host build: tests with sanitizers, the shots tool, the baked fonts, the PCL render (make render). The app ELF is built by the firmware fork's
# apps/build.sh from apps/trmnl-print (run `make fonts` first).
CC ?= gcc
FW ?= $(HOME)/workspace/trmnl-fw-gb
PY ?= $(HOME)/workspace/trmnl-fpv/.venv/bin/python
CFLAGS = -Itest -std=c11 -O1 -g -Wall -Wextra -Werror -Wshadow -Isrc -Ihost -Igen -I$(FW)/include -fsanitize=address,undefined -fno-sanitize-recover=all
CORE = src/pcl.c src/ipp.c src/imgfit.c src/textlay.c src/flist.c src/printjob.c src/pr.c src/prui.c src/pngio.c src/font.c src/gfx.c gen/fonts_gen.c
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
# The Xtensa linker (ld 2.43.1, --gc-sections --strip-all) aborts with "double free or corruption" for some output
# paths on this app (heap layout; no ELF is made then, so nothing broken can ship): each try uses another path length.
elf: gen/fonts_gen.c
	@mkdir -p build/elf; ok=0; for n in sk sk2 a bb cccc ddddddd eeeeeeee; do d=$${TMPDIR:-/tmp}/print-elf-$$n; rm -rf $$d; \
	  if OUT=$$d $(FW)/apps/build.sh $(CURDIR)/apps/trmnl-print >/dev/null 2>&1; then cp $$d/trmnl-print.elf build/elf/; rm -rf $$d; ok=1; break; fi; \
	  rm -rf $$d; done; [ $$ok = 1 ] || { echo "the linker crashed on every path: see Makefile"; exit 1; }; \
	  ls -l build/elf/trmnl-print.elf; sha256sum build/elf/trmnl-print.elf
.PHONY: elf
# Looks at the PCL the app makes, with Artifex's gpcl6 in docker (no printer involved): make render, then view shots/r/*.png
render: shots
	docker build -q -t tp-gpcl6 tools/gpcl >/dev/null
	@mkdir -p shots/r; for j in sketch text; do docker run --rm -v $(CURDIR)/shots:/w tp-gpcl6 -dNOPAUSE -dBATCH -sDEVICE=pnggray -r150 -sOutputFile=/w/r/$$j-%d.png /w/$$j.pcl; done
.PHONY: render
