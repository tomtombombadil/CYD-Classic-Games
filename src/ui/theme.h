// Colors, fonts and user-facing UI settings, shared by every game.
//
// Cheap CYD panels (TN) lose contrast at an angle, so highlight tints are
// deliberately strong and use different hues, not just slightly different
// lightness: row/column/box = blue-grey, same digit = yellow, selected cell
// = amber. Both themes keep that scheme. Games add fields here rather than
// hard-coding colors.
//
// Custom themes: the player starts from Light or Dark and picks colors for
// 10 roles from a 48-color palette. A role sets one or more palette fields
// (see apply_custom in theme.cpp), and colors that depend on it (muted
// text, pressed keys, text on accent) are worked out from the picks.
#pragma once

#include <cstdint>
#include <lvgl.h>

namespace ui {

enum class Theme : uint8_t { Light = 0, Dark = 1, Custom1 = 2, Custom2 = 3, Custom3 = 4 };
constexpr int kCustomThemes = 3;
constexpr int kThemeCount = 2 + kCustomThemes;
inline bool  is_custom(Theme t)    { return static_cast<uint8_t>(t) >= 2; }
inline int   custom_slot(Theme t)  { return static_cast<int>(t) - 2; }
inline Theme custom_theme(int slot) { return static_cast<Theme>(2 + slot); }
enum class InputMode : uint8_t { CellFirst = 0, DigitFirst = 1 };

// Volume 50 % by default: tiny CYD speakers distort near 100 % (Tom)
constexpr uint8_t kDefaultVolume = 50;

// Saved on the device by the app (see src/app/settings_store.*), shared by
// every game.
struct UiSettings {
    Theme     theme = Theme::Light;
    InputMode input = InputMode::DigitFirst;   // Sudoku's input mode; Tom's default
    uint8_t   brightness = 200;                // backlight, kMinBrightness..255
    char      last_game[16] = "";              // registry id for "Continue" on the picker
    uint8_t   volume = kDefaultVolume;         // 0..100 %, 0 = silent play
    uint8_t   splash_next = 0;                 // which splash image the next boot shows
    uint8_t   card_back = 3;                   // card games: cards::Back design (shared); 3 = Blue Lattice
    bool      flip = false;                    // screen turned 180 degrees (USB cord the other way)
    bool      left_handed = false;             // layouts put the most-tapped things on the left
};
constexpr uint8_t kMinBrightness = 20;         // never let the screen go fully dark

struct Palette {
    lv_color_t screen, cell, line_thin, line_thick;
    lv_color_t given, entry, hinted, note, note_match;  // note_match: note = highlighted digit
    lv_color_t conflict, conflict_bg;
    lv_color_t peer, same, selected;
    lv_color_t key, key_border, key_pressed, key_on, key_on_text, key_dim_text;
    lv_color_t ink, muted;
    // Two-player pieces and boards
    lv_color_t piece_a, piece_b;   // first / second player's pieces (red, yellow)
    lv_color_t frame;              // FourConnect board
    lv_color_t lit;                // Light Switch: a light that is on
    lv_color_t win;                // winning line, solved highlight
    // 8x8 boards (Reversi, Checkers, Chess)
    lv_color_t sq_light, sq_dark;  // checkerboard squares
    lv_color_t felt;               // Reversi table
    lv_color_t stone_dark, stone_light;   // Reversi discs, checkers, chess men
    lv_color_t target;             // dots: where the picked piece may go
    lv_color_t absent;             // word games: a letter not in the word
};

// ---- Custom themes -------------------------------------------------------------
enum class Role : uint8_t {
    Background, Board, GridLines, Text, Marks, Selected, Matching, RowCol, Buttons, Accent,
};
constexpr int kRoles = 10;
const char* role_name(Role r);

constexpr int kPaletteColors = 48;             // 8 columns x 6 rows
extern const uint32_t kPalette[kPaletteColors];  // 0xRRGGBB

struct CustomTheme {
    uint8_t  base = 0;                         // 0 = Light, 1 = Dark
    uint16_t set = 0;                          // bit r = role r picked
    uint32_t color[kRoles] = {};               // 0xRRGGBB for picked roles
};
struct CustomThemes {
    CustomTheme slot[kCustomThemes];
};
// Install the saved custom themes (rebuilds the current palette).
void                set_custom_themes(const CustomThemes& t);
const CustomThemes& custom_themes();
// Change one slot (rebuilds the palette if it is the current theme).
void                set_custom_theme(int slot, const CustomTheme& t);
// The color a role shows in a theme right now (picked or from the base).
uint32_t            role_color(Theme t, Role r);
// Black or white, whichever reads better on `bg`.
lv_color_t          contrast_text(lv_color_t bg);

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
