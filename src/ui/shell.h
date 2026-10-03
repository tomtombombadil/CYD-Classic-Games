// The app shell: the platform services every game uses (saves, stats,
// panel and touch functions), the boot screen (game picker), the shared
// Display & touch screen, and switching between games.
//
// Hardware actions come in through `Shell` function pointers set by
// main.cpp, so all of this also builds in the PC preview (tools/preview).
#pragma once

#include <cstddef>
#include <cstdint>
#include "theme.h"

namespace ui {

struct Shell {
    uint32_t (*random_seed)();                 // fresh seed (new puzzle, AI)

    // Per-game save file. load returns the byte count read, 0 = no save.
    size_t (*load_game)(const char* id, uint8_t* buf, size_t cap);
    void   (*save_game)(const char* id, const uint8_t* buf, size_t len);
    void   (*save_settings)(const UiSettings&);

    void (*toggle_invert)();                   // panel color fixes
    void (*toggle_swap_rb)();
    void (*flash_invert)(bool on);             // win flash: invert panel briefly
    void (*set_brightness)(uint8_t level);     // backlight
    void (*recalibrate_touch)();               // may not return (device restarts)
    // Unfiltered touch reading for the touch test (nullptr = not available)
    bool (*raw_touch)(int16_t* x, int16_t* y);

    // Per-game play history, CSV (nullptr = no stats). The store numbers the
    // lines: `body` is a record without its "seq," prefix, ending in "\n";
    // stats_read hands over whole lines ("seq,body"), header included.
    bool (*stats_append)(const char* id, const char* header, const char* body);
    bool (*stats_read)(const char* id, void (*line)(const char* text, void* ctx), void* ctx);
    bool (*stats_delete_last)(const char* id);
    bool (*stats_clear)(const char* id);
    const char* (*stats_location)();           // "SD card" / "board memory"

    const char* firmware_version;
    const char* board_name;
};

// Start the app: applies theme and brightness and shows the game picker.
void app_begin(const Shell& shell, const UiSettings& settings);
// Call often from loop(): runs the open game's clock, autosave, tasks.
void app_tick(uint32_t now_ms);

const Shell& shell();
UiSettings&  settings();
void         save_settings();

// Switching (deferred to the next LVGL cycle, so these are safe to call
// from a click handler of an object they delete).
void app_open_game(int index);                 // registry index
void app_go_home();                            // close the open game -> picker
int  app_current_game();                       // registry index, -1 = picker

void app_save_current();                       // save the open game now
void app_theme_changed();                      // rebuild whatever is on screen

// Display & touch (shared). `back` runs on its Back button.
void settings_open(void (*back)());
void settings_open_touch_test();             // from Display & touch

// ---- PC preview: stage screenshots without the deferred switch -------------
void app_open_game_now(int index);
void app_go_home_now();
void picker_open_menu();

} // namespace ui
