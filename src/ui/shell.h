// The app shell: the platform services every game uses (saves, stats,
// panel and touch functions), the boot screen (game picker), the shared
// Settings screen, and switching between games.
//
// Hardware actions come in through `Shell` function pointers set by
// main.cpp, so all of this also builds in the PC preview (tools/preview).
#pragma once

#include <cstddef>
#include <cstdint>
#include "theme.h"

namespace ui {

struct Tone {
    uint16_t hz;                               // 0 = rest
    uint16_t ms;
};

struct Shell {
    uint32_t (*random_seed)();                 // fresh seed (new puzzle, AI)

    // Per-game save file. load returns the byte count read, 0 = no save.
    size_t (*load_game)(const char* id, uint8_t* buf, size_t cap);
    void   (*save_game)(const char* id, const uint8_t* buf, size_t len);
    void   (*save_settings)(const UiSettings&);
    void   (*save_themes)(const CustomThemes&);

    // Sound (nullptr = no speaker). Replaces whatever is playing.
    void (*play_tones)(const Tone* tones, int n);

    void (*toggle_invert)();                   // panel color fixes
    void (*toggle_swap_rb)();
    void (*flash_invert)(bool on);             // win flash: invert panel briefly
    void (*set_brightness)(uint8_t level);     // backlight
    void (*set_flip)(bool flipped);            // turn the screen 180 degrees (nullptr = can't)
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

    // Device log (nullptr = none). log: one line; to_file = false only
    // prints it and marks it as the last step a crash report names.
    // log_copy_sd is nullptr on boards without a usable SD slot.
    void (*log)(const char* text, bool to_file);
    bool (*log_read)(void (*line)(const char* text, void* ctx), void* ctx);
    void (*log_clear)();
    bool (*log_copy_sd)();
    void (*memory)(uint32_t* free_bytes, uint32_t* largest_block);

    const char* firmware_version;              // "v1.2.3" (VERSION file)
    const char* firmware_build;                // git commit of the build, may be empty
    const char* board_name;
};

// Device log lines (printf style). log_event goes to the log file; log_step is for
// frequent events: serial only, and kept as the "last step" for a crash.
void log_event(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_step(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Start the app: applies theme and brightness and shows the game picker.
void app_begin(const Shell& shell, const UiSettings& settings, const CustomThemes& themes);
// Call often from loop(): runs the open game's clock, autosave, tasks.
void app_tick(uint32_t now_ms);

const Shell& shell();
UiSettings&  settings();
void         save_settings();
// Layouts put what's tapped most on the stylus hand's side (Settings)
inline bool  right_handed() { return !settings().left_handed; }

// Switching (deferred to the next LVGL cycle, so these are safe to call
// from a click handler of an object they delete).
void app_open_game(int index);                 // registry index
void app_go_home();                            // close the open game -> picker
int  app_current_game();                       // registry index, -1 = picker

void app_save_current();                       // save the open game now
void app_theme_changed();                      // rebuild whatever is on screen
// Switch theme everywhere: palette, styles, the screen underneath. Saves.
void app_set_theme(Theme t);

// Settings (shared). `back` runs on its Back button.
void settings_open(void (*back)());
void settings_open_touch_test();               // from Diagnostics
void diagnostics_open();                       // from Settings
void device_log_open(int page = -1);           // from Diagnostics; -1 = newest page
void send_log_open();                          // from Diagnostics: the log as a QR code
void settings_reopen();                        // Settings again, same Back
// Theme screens (src/ui/theme_screen.cpp), reached from Settings
void theme_open();
void theme_open_editor(int slot);
void theme_open_palette(int slot, Role role);

// ---- PC preview: stage screenshots without the deferred switch -------------
void app_open_game_now(int index);
void app_go_home_now();
void picker_open_menu();
void picker_open_category(int category);      // -1 = the category list
void picker_next_page();

} // namespace ui
