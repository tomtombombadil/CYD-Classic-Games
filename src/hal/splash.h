// Boot splash: one of the title images, drawn straight to the panel with
// LovyanGFX's JPEG decoder (block by block, no frame buffer), then wait for
// a tap. Runs after lvgl_port_init() and before LVGL draws anything.
#pragma once

#include <cstdint>
#include "boards/board_select.h"

int  splash_count();
// Show image `index` (wrapped to the count) and return after a tap
// (press and release, so the tap doesn't carry into the first screen).
void splash_show(LGFX& gfx, int index);
