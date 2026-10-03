// Saves the settings every game shares (theme, brightness, Sudoku's input
// mode, last game played) to LittleFS: /ui_settings.bin. Reads the
// CYD-Sudoku v1.0.0 file too.
#pragma once

#include "ui/theme.h"

ui::UiSettings settings_store_load();   // defaults if nothing saved
void           settings_store_save(const ui::UiSettings& s);
