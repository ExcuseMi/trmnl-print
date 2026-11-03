// The firmware's own touch bar geometry (touchbar_layout.cpp of the fork) for the host tests and shots, so they
// check against the code the X runs. SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
#include <touchbar_layout.cpp>

extern "C" int tbfw_layout(int orient, app_touchbar_t *out) { return touchbar_layout_fill(orient, 1872, 1404, out); }
