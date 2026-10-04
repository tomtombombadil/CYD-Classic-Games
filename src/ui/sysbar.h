// The header bar along the top of every screen (Tom, 2026-10-04): the
// standard bar the games always had, now on every page, built once on
// LVGL's system layer above the picker, the games and every overlay.
// Left to right:
//   <  back      - the page's back action: closes a page (its Back), leaves
//                  a category page, exits a game (a wireless game in
//                  progress = Forfeit). Greyed where there's nowhere to go.
//   middle       - a page's title ("Classic Games v0.19.0", "Display"), or in
//                  a game its clock (left) and status (centre)
//   2P           - two head-and-shoulders: filled while a wireless 2-player
//                  game is going (or paused); a tap opens it
//   wifi         - empty: wireless play off (1P); a dot and 0-3 arcs: the
//                  signal of the boards nearby. A tap opens Play settings
//   gear         - Settings; in a game, the game's menu (New Game, Restart,
//                  How To Play, Stats, Settings...) - no ☰ (Tom). Furthest
//                  right, away from back
// The icons are as tall as the bar: fine for a stylus, still a fingertip.
//
// Screens start below it: the screen and the top layer get a top padding
// of sysbar_height(), and metrics().h is the height under the bar, so every
// layout made from metrics() fits without knowing the bar is there.
#pragma once

#include <lvgl.h>

namespace ui {

int  sysbar_height();                      // from the screen size (before the bar exists too)
void sysbar_build();                       // after app_begin, theme or rotation changes
// The screen's title and back action (picker page, game). back = nullptr:
// the arrow is greyed. `note` is small text after the title (the version).
// Ends a game's use of the bar.
void sysbar_screen(const char* title, void (*back)(), const char* note = nullptr);
// A game takes the bar: the gear opens its menu (menu_cb, user data
// `user`), and it writes two labels - left (the clock in Sudoku and
// Minesweeper, chips in the casino games) and the status (sysbar_status).
// No game name, no level: it's plain which game is up (Tom).
// left: 0 = the left label is never shown (most games don't show their
// clock - Tom; it still counts for the stats), 1 = shown while there's room
// (a clock), 2 = always shown, the status gets what's left (chips)
void sysbar_game(lv_event_cb_t menu_cb, intptr_t user, int left);
lv_obj_t* sysbar_left();
lv_obj_t* sysbar_center();
// The status; `short_text` is used if the full one doesn't fit (the clock
// gives way too if neither fits beside it)
void sysbar_status(const char* text, const char* short_text = nullptr);
// An overlay (page) is up: its title; the back arrow presses `back_key`
// (an object on the page with a click handler) or, if nullptr, closes it
void sysbar_overlay(const char* title, lv_obj_t* back_key);
void sysbar_overlay_closed();
void sysbar_page_back(lv_obj_t* back_key);   // the open page's back key
void sysbar_tick(uint32_t now_ms);         // the icons follow wireless play
// What the back arrow / the gear do right now (also for tests)
void sysbar_back();
void sysbar_gear();

} // namespace ui
