// Wireless Play: CYD-to-CYD games, from finding someone to play to the
// end of a session. The protocol is plain C++ in src/net/wireless.*; the
// game side (turns, Play Again, Forfeit) is in match.*. This file is the
// service that runs all the time (from app_tick) and its screens.
//
// The player's setup, kept in /games/player.bin:
//   - a name (boards start as "CYD-" + 4 hex digits of the address),
//   - Available To Play: on = the radio is on and this board beacons, so
//     others find it and can ask it to play - wherever the player is (the
//     picker, a solo game, a menu). Off = hidden; the radio is off unless a
//     wireless game is going.
//   - the games it is willing to play (Games I'll Play: a toggle per game);
//     others see only those.
//
// Screens (overlays, reachable from the picker's "Wireless Play" row and a
// two-player game's Wireless key):
//   Wireless Play   - You are <name>; Available To Play (toggle); Games I'll
//                     Play; Find Players / Resume <game> With <name>;
//                     [Back | Change Name]
//   Games I'll Play - a toggle key per game, and All Games
//   Players Nearby  - everyone available or playing nearby; tap one
//   <name>'s Games  - the games that player is willing to play; tap one to
//                     ask them
//   Asking          - "Asking Bob to play Chess..." [Stop Asking]; the answer
//                     comes back as a note: Play starts the game, "Not Now",
//                     "Another Game", busy, or no answer within 30 s
//   An offer        - pops up over anything when someone asks this board:
//                     "Bob would like to play Chess with you" [Play],
//                     [Not Now | Other Game] (and a ding-dong)
//
// One session at a time (/games/wl_session.bin; a paused game carries on
// while the board stays on - a restart clears the session, Tom 2026-10-04). A board in a session is busy: others see
// "playing Chess" and can't ask it. The session's game screen closed =
// paused ("Bob closed the game for now" on the other board).
#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "net/wireless.h"

namespace wplay {

// The games that play wireless (kNetwork in games.def), in games.def order:
// their index is the bit in the "willing to play" mask
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
// Back to Wireless Play once a session is over (closes the game first)
void back_after_game(const char* note);
// Drop the session, unrecorded on both boards (Clear 2P Sessions; also done
// at every start and when a game finds a session it doesn't agree with)
void clear_sessions();

// ---- Screens ----------------------------------------------------------------------------------
// Play settings (Settings > Play, the wifi icon, a game's Wireless key):
// the stylus hand, Play Mode 1P / 2P and, in 2P, Find Players, Games I'll
// Play, Change Name. `back` runs on the header's back arrow (nullptr = close).
void open_menu(void (*back)() = nullptr);
// The picker's row: "Off", "On", "2 nearby" or "Playing"
void picker_status(char* buf, size_t cap);
bool available();
// The picker's row label to keep current (nullptr when it goes)
void set_picker_label(lv_obj_t* label);
void debug_state(char* buf, size_t cap);    // one line for tests and the log
// The header bar's icons (sysbar.*): wifi -1 = off (1P), 0 = on but no
// signal (just the dot), 1-3 bars; 2P 1 = a wireless game going or paused
int  wifi_level();
int  two_player_state();
void resume_session();                      // the 2P icon: open that game
void set_two_player(bool on);               // Play Mode 1P / 2P (tests)

} // namespace wplay
