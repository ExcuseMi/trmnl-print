#!/bin/sh
# Every source file carries the PolyForm Noncommercial SPDX line and no MIT one (licence changed 2026-11-03; a file
# copied from an old version must not bring MIT back). Run by `make test`.
fail=0
for f in $(git ls-files 'src/*.[ch]' 'test/*.[ch]' 'host/*.[ch]' 'host/*.cpp' 'apps/*.c' 'apps/*.h'); do
  grep -q 'SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0' "$f" || { echo "licence line missing: $f"; fail=1; }
  grep -q 'SPDX-License-Identifier: MIT' "$f" && { echo "MIT line: $f"; fail=1; }
done
grep -q '^# PolyForm Noncommercial License 1.0.0' LICENSE || { echo "LICENSE is not PolyForm Noncommercial"; fail=1; }
exit $fail
