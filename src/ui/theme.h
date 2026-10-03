// Colors, fonts and user-facing UI settings, shared by every game.
//
// Cheap CYD panels (TN) lose contrast at an angle, so highlight tints are
// deliberately strong and use different hues, not just slightly different
// lightness: row/column/box = blue-grey, same digit = yellow, selected cell
// = amber. Both themes keep that scheme. Games add fields here rather than
// hard-coding colors.
#pragma once

#include <cstdint>
#include <lvgl.h>

namespace ui {

enum class Theme : uint8_t { Light = 0, Dark = 1 };
enum class InputMode : uint8_t { CellFirst = 0, DigitFirst = 1 };

// Saved on the device by the app (see src/app/settings_store.*), shared by
// every game.
struct UiSettings {
    Theme     theme = Theme::Light;
    InputMode input = InputMode::DigitFirst;   // Sudoku's input mode; Tom's default
    uint8_t   brightness = 200;                // backlight, kMinBrightness..255
    char      last_game[16] = "";              // registry id for "Continue" on the picker
};
constexpr uint8_t kMinBrightness = 20;         // never let the screen go fully dark

struct Palette {
    lv_color_t screen, cell, line_thin, line_thick;
    lv_color_t given, entry, hinted, note, note_match;  // note_match: note = highlighted digit
    lv_color_t conflict, conflict_bg;
    lv_color_t peer, same, selected;
    lv_color_t key, key_border, key_pressed, key_on, key_on_text, key_dim_text;
    lv_color_t ink, muted;
};

void           set_theme(Theme t);
Theme          theme();
const Palette& pal();
const char*    theme_name(Theme t);

// Fonts picked by the size they must fit
inline const lv_font_t* font_for_cell(int cell)
{
    return cell >= 33 ? &lv_font_montserrat_28 : cell >= 22 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
}
inline const lv_font_t* note_font_for_cell(int cell)
{
    return cell >= 33 ? &lv_font_montserrat_10 : &lv_font_montserrat_8;
}

} // namespace ui
