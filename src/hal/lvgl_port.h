// Glue between LovyanGFX (display + touch hardware) and LVGL (UI).
#pragma once

#include <lvgl.h>
#include "boards/board_select.h"

// The single LovyanGFX device for this board. Valid after lvgl_port_init().
// Normal UI code should draw through LVGL, not call this directly.
LGFX& lvgl_port_gfx();

// Bring up the panel, backlight, touch (with saved calibration) and LVGL.
// rotation: 0/2 = portrait, 1/3 = landscape (LovyanGFX numbering).
// Returns the LVGL display, or nullptr if the draw buffers couldn't be
// allocated.
lv_display_t* lvgl_port_init(uint8_t rotation);

// Call from loop(). Runs LVGL timers/refresh and returns ms until the next
// time LVGL wants to run.
uint32_t lvgl_port_loop();

// One unfiltered touch reading in screen coordinates (calibration and
// rotation applied, no press filtering). For the touch test screen.
bool lvgl_port_raw_touch(int16_t* x, int16_t* y);

// Backlight 0-255.
void lvgl_port_set_brightness(uint8_t level);
