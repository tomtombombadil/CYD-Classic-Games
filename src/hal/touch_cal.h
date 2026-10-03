// Touch calibration for resistive (XPT2046) boards.
//
// A resistive panel's raw readings drift between units, so each board gets
// calibrated once (tap the four corner arrows) and the result is saved to
// LittleFS. Capacitive boards skip all of this.
#pragma once

#include "boards/board_select.h"

// Load saved calibration, or run the on-screen calibration if there is none
// or if the BOOT button is held while the board powers up. Must be called
// after the display is initialised and before LVGL starts drawing.
// Needs LittleFS mounted (it will format the partition if it's blank).
void touch_cal_begin(LGFX& gfx);

// Force a new calibration (e.g. from a settings menu later) and save it.
void touch_cal_run(LGFX& gfx);
