// Per-unit display color fixes, set on the device and saved to flash.
//
// Panels sold as the same board differ between production runs: some show a
// negative image, some swap red and blue. Rather than rebuild with different
// flags, the user toggles these on screen once and they stick.
#pragma once

#include "boards/board_select.h"

struct PanelPrefs {
    bool invert  = false;   // flip relative to the board file's default
    bool swap_rb = false;   // swap red/blue relative to the board file's default
};

// Load saved prefs (or defaults) and apply them. Call after gfx.init().
void panel_prefs_begin(LGFX& gfx);

const PanelPrefs& panel_prefs_get();

// Briefly show the panel inverted (on) or normal (off), without saving.
// Used for the "solved" flash.
void panel_prefs_flash(LGFX& gfx, bool on);

// Change, apply immediately and save. The caller should redraw afterwards.
void panel_prefs_set(LGFX& gfx, const PanelPrefs& prefs);
