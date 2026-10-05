// Wireless Play: CYD-to-CYD games, from finding someone to play to the
// end of a session. The protocol is plain C++ in src/net/wireless.*; the
// game side (turns, the move timer, Play Again, Forfeit) is in match.*.
// This file is the service that runs all the time (from app_tick) and its
// pages. Tom's design (2026-10-04) is docs/SPEC.md section 5.
//
// HARD RULE: no free-form communication between players. Names are two
// words picked from fixed lists (src/net/names.*), never typed; everything
// on the air is a fixed code.
//
// The player's setup, kept in /games/player.bin ("PLR3"): the name (two
// word numbers), Play Mode 1P / 2P (2P = the radio listens, and answers
// players who look for others), the games this board will play (Games I'll
// Play) and the Move Timer.
//
// Pages (Settings > Play, the header's wifi icon, a game's Wireless key):
//   Play           - Left / Right Hand; Play Mode 1P / 2P; Find Players;
//                    Games I'll Play; Name; Move Timer; Clear 2P Sessions
//   Find Players   - "Searching...", then "Bob - available / busy / no
//                    games / needs update / later version"; tap one
//   Play With Bob  - every wireless game, the ones both play lit; tap = ask
//   Requesting     - "Asking Bob to play Chess." [Stop Asking]; answers
//                    come back as a line on Play With Bob
//   A request      - pops up anywhere: "Ann would like to play Chess."
//                    [Play] [No Thanks | Other Game] (and a ding-dong)
//   Connecting     - until both boards hear each other; a trill, the game opens
//   Back in range  - a put-away session meets its partner again:
//                    "Bob is back in range. Continue Chess?"
//   Your Name      - [Random] [Pick From List] (a word from each list)
//   Move Timer     - 30 s, 1, 2, 5 minutes, Off
//
// One session at a time (/games/wl_session.bin "WLS2"), kept through a
// restart (it comes back put away until both players meet again). While it
// is going the board is busy: nobody can ask it, and it can't ask anyone.
// A one-player game of the session's game is put aside when the session
// starts and comes back when it ends ("Resuming your previous one player game.").
#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "net/wireless.h"

namespace wplay {

// The games that play wireless (kNetwork in games.def, with a key in net_games.h)
int  game_count();
int  game_registry(int g);                 // registry index of wireless game g
int  game_of(const char* id);              // wireless index of a registry id, -1 = none

bool radio_present();                      // this build has a radio (the shell's hooks)

void tick(uint32_t now);                   // always, from app_tick

// ---- The session (the game side, used by match.*) --------------------------------------------
// The current session's link if it belongs to game `id` (else nullptr)
net::Link* session_for(const char* id);
// True once when a session for game `id` was just agreed: start its first game
bool take_start(const char* id);
void session_save();                       // write the link's state (with every game save)
// The session's game is over (or a new one began): a board whose game is
// over isn't busy - others may ask it, and accepting ends the old session
void session_over(bool over);
// The game noted how the session ended (recorded the result): let it go
void session_finished();
// Back to the Play page once a session is over (closes the game first)
void back_after_game(const char* note);
// The boards lost touch and the player kept waiting a whole move time more:
// put the session away (it carries on when they meet again) and leave the game
void suspend_session();
// The partner's name, e.g. "Wobbly Llama" ("" with no session)
const char* partner_name();
// Game over, New Game: the Play With page for the partner
void new_game_with_partner();
// "Resuming your previous one player game." is due for game `id` (once)
bool take_resumed_note(const char* id);
// The game `id` closed: if its session is over, its one-player game comes back
void game_closed(const char* id);
// Drop the session: a forfeit if the partner is connected and the game is
// going, else unrecorded on both boards (Clear 2P Sessions)
void clear_sessions();

// ---- Screens ----------------------------------------------------------------------------------
// The Play page (Settings > Play, the wifi icon, a game's Wireless key).
// `back` runs on the header's back arrow (nullptr = close).
void open_menu(void (*back)() = nullptr);
bool available();                           // Play Mode 2P
uint16_t move_timer();                      // this board's Move Timer setting (s), 0 = Off
const char* my_name();
void debug_state(char* buf, size_t cap);    // one line for tests and the log
// The header bar's icons (sysbar.*): wifi -1 = off (1P), 0 = on but no
// signal (just the dot), 1-3 bars; 2P 1 = a session going, put away or starting
int  wifi_level();
int  two_player_state();
const char* radio_state();
// For the log: "radio sent N (M failed, error E), got K (D dropped), F KB free"
void radio_report(char* buf, size_t cap);                  // About: "Off (1P)", "Dozing (2P)", "Listening", "None"
void resume_session();                      // the 2P icon
// Tests (preview agent)
void set_two_player(bool on);
void set_name(uint16_t a, uint16_t b);
void set_move_timer(uint16_t seconds);

} // namespace wplay
