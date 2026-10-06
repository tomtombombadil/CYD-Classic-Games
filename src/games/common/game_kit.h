// Screen pieces the newer games share: top bar, clock, win flash, the
// standard menus (two-player and solo puzzle) and stats screens. Built on
// src/ui/widgets.* and the app shell, so it runs in the PC preview too.
#pragma once

#include <cstdint>
#include <new>
#include <lvgl.h>
#include "puzzle_stats.h"
#include "two_player.h"

namespace kit {

// A game's state back to new, built in place. Never `*G = Game{}`: that
// makes a whole temporary Game on the stack first - Chess's is 5.5 KB, and
// starting a wireless Chess game that way overflowed the main task's stack
// (v0.18/v0.19 reboots, 2026-10-04).
template <class T> void renew(T& t) { t.~T(); new (&t) T(); }

// ---- Top bar ----------------------------------------------------------------------
// The game's part of the header bar (ui/sysbar.*): left = small info (the
// clock), center = status; the bar's gear opens menu_cb (the game's menu).
struct TopBar {
    lv_obj_t* left   = nullptr;
    lv_obj_t* center = nullptr;
    int       h      = 0;          // the screen's content starts at this y
};
// The status; `short_text` if the full one doesn't fit
void top_bar_status(const TopBar& t, const char* text, const char* short_text = nullptr);
// show_left (ui::sysbar_game): 0 = hidden (games may still write it),
// 1 = while there's room (Minesweeper's clock), 2 = always (chips)
TopBar top_bar(lv_event_cb_t menu_cb, int show_left = 0);

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
// The every-30-seconds save of an open game: due only when the game's clock
// has moved since the last one, so an idle, finished or paused game isn't
// rewritten to flash over and over (moves save straight away anyway).
bool save_due(uint32_t now_ms, uint32_t last_save_ms, uint32_t seconds);

void flash();                      // invert the panel a few times (about 1.2 s)
void flash_stop();

// ---- Menus -----------------------------------------------------------------------------
enum MenuId : intptr_t {
    kLevel0 = 0, kLevel1 = 1, kLevel2 = 2,   // new game: vs computer level / puzzle level
    kPassAndPlay = 3, kWireless = 4,
    kStats = 5, kExitGame = 6, kSettings = 7, kExitMenu = 8, kRestart = 9,
    kHowToPlay = 10, kOptions = 11, kForfeit = 12,
};
struct MenuHandlers {
    void (*pick)(int id);          // a level, pass-and-play or restart was tapped
    void (*stats)();               // open the game's stats screen
    void (*back)();                // "Exit Menu" (after the menu closed)
    void (*reopen)();              // show this menu again (Back from Settings)
    void (*exit_game)() = nullptr; // "Exit Game" (nullptr = save and go to the picker)
};
// Two-player: "New game vs computer: Easy Medium Hard", Pass and play,
// Wireless (greyed and ignored unless `wireless`; picks kWireless), How To
// Play, Stats | Settings, and at the bottom Exit Menu | Exit Game.
// options: label of an extra key (id kOptions) under the new-game keys,
// e.g. "Options"; nullptr = none (the same in menu_wireless).
void menu_two_player(const char* title, const MenuHandlers& h, bool wireless = false, const char* options = nullptr);
// A wireless game in progress: a line about it, Forfeit Game (picks
// kForfeit: a loss here, a win on the other board - at once), How To Play,
// Stats | Settings, Exit Menu | Exit Game (the game's exit_game asks
// first: leaving forfeits).
void menu_wireless(const char* title, const char* line, const MenuHandlers& h, const char* options = nullptr);
// Solo puzzle: "New Game:" three levels, Restart, How To Play,
// Stats | Settings, and at
// the bottom Exit Menu | Exit Game.
// levels == nullptr: one "New Game" key (id kLevel0). restart = false hides
// "Restart This Game".
// options: label of an extra key (id kOptions) below Restart, e.g.
// "Options" or "Card Back"; nullptr = none.
void menu_solo(const char* title, const char* const levels[3], const MenuHandlers& h,
               bool restart = true, const char* options = nullptr);

// ---- How To Play ----------------------------------------------------------------------
// The open game's help pages (games.def / <id>_help.cpp), one per screen:
// heading with "2 / 4", the text, and [<] [>] at the bottom; the header's back arrow returns to the menu.
// `back` reopens the game's menu.
void how_to_play(void (*back)(), int page = 0);
// The standard menu row that opens it (full width, above Stats | Settings);
// the standard menus include it already.
void how_to_play_key(lv_event_cb_t cb, intptr_t id);

// ---- Stats --------------------------------------------------------------------------------
void stats_two_player(const char* game_id, const twoplayer::Sides& sides, void (*back)());
// win_loss: columns Level | Won | Lost | Best time (games you can lose, like
// Minesweeper) instead of Level | Solved | Best | Moves.
// n_levels: rows in the levels table (1..3; Solitaire has Draw 1 / Draw 3).
void stats_solo(const char* game_id, const char* const levels[3], void (*back)(),
                bool win_loss = false, int n_levels = 3);

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
// The same, left-aligned in the box
void text_left(lv_layer_t* layer, const char* s, const lv_font_t* font, lv_color_t c,
               int32_t x1, int32_t y1, int32_t w, int32_t h);

} // namespace kit
