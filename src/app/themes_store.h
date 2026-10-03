// Saves the three custom themes to LittleFS: /themes.bin.
#pragma once

#include "ui/theme.h"

ui::CustomThemes themes_store_load();     // blank slots (copies of Light) if none saved
void             themes_store_save(const ui::CustomThemes& t);
