// Screen pieces the newer games share: top bar, clock, win flash, the
// standard menus (two-player and solo puzzle) and stats screens. Built on
// src/ui/widgets.* and the app shell, so it runs in the PC preview too.
#pragma once

#include <cstdint>
#include <lvgl.h>
#include "puzzle_stats.h"
#include "two_player.h"

namespace kit {

// ---- Top bar ----------------------------------------------------------------------
// Left: small info (clock or mode), center: status, right: ☰.
struct TopBar {
    lv_obj_t* left   = nullptr;
    lv_obj_t* center = nullptr;
    int       h      = 0;          // height incl. margin; content starts below
    int       text_y = 0;          // y of the labels (re-centre `center` with it)
};
// Set the center text and keep it centred
void top_bar_status(const TopBar& t, const char* text);
TopBar top_bar(lv_event_cb_t menu_cb);

// The screen, cleaned and painted for a new layout.
lv_obj_t* screen_begin();

// ---- Clock --------------------------------------------------------------------------
// Counts a game's seconds only while it is on screen with no overlay and
// there was a touch in the last 2 minutes (nothing counts unattended time).
struct Clock {
    uint32_t last_ms = 0;
    bool     paused  = false;
    // Call often; returns true when `seconds` went up or the pause state
    // changed (redraw the clock).
    bool tick(uint32_t now_ms, bool running, uint32_t& seconds);
};

// ---- Win flash ------------------------------------------------------------------------
void flash();                      // invert the panel a few times (about 1.2 s)
void flash_stop();

// ---- Menus -----------------------------------------------------------------------------
enum MenuId : intptr_t {
    kLevel0 = 0, kLevel1 = 1, kLevel2 = 2,   // new game: vs computer level / puzzle level
    kPassAndPlay = 3, kWireless = 4,
    kStats = 5, kExitGame = 6, kSettings = 7, kExitMenu = 8, kRestart = 9,
};
struct MenuHandlers {
    void (*pick)(int id);          // a level, pass-and-play or restart was tapped
    void (*stats)();               // open the game's stats screen
    void (*back)();                // "Exit Menu" (after the menu closed)
    void (*reopen)();              // show this menu again (Back from Settings)
};
// Two-player: "New game vs computer: Easy Medium Hard", Pass and play,
// Wireless (not yet), Stats | Settings, and at the bottom Exit Menu | Exit Game.
void menu_two_player(const char* title, const MenuHandlers& h);
// Solo puzzle: "New Game:" three levels, Restart, Stats | Settings, and at
// the bottom Exit Menu | Exit Game.
void menu_solo(const char* title, const char* const levels[3], const MenuHandlers& h);

// ---- Stats --------------------------------------------------------------------------------
void stats_two_player(const char* game_id, const twoplayer::Sides& sides, void (*back)());
void stats_solo(const char* game_id, const char* const levels[3], void (*back)());

// Record a finished game through the shell's stats store
void record_two_player(const char* game_id, const twoplayer::Record& r, const twoplayer::Sides& s);
void record_solo(const char* game_id, const puzzle::Record& r, const char* const levels[3]);

// ---- Drawing helpers for custom-drawn boards -------------------------------------------------
void fill_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c,
               int32_t radius = 0);
void fill_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r, lv_color_t c);
void ring(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r, int32_t width, lv_color_t c);
void line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t c);
void text(lv_layer_t* layer, const char* s, const lv_font_t* font, lv_color_t c,
          int32_t x1, int32_t y1, int32_t w, int32_t h);

} // namespace kit
