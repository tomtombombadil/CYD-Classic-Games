// The screen side of wireless play (CYD to CYD): the radio switch, this
// board's player name, and "Play Nearby" - the overlay where two boards
// find each other. The protocol itself is plain C++ in src/net/wireless.*;
// the game side (turns, waiting, Play Again) is in match.*.
//
// Play Nearby lists the boards nearby that are in Play Nearby too. A board
// that wants the same game with the same firmware is a key: tap it to ask
// it to play. Others show greyed with the reason (another game, another
// version). The asked board shows "Ann asks you to play Chess" with Play |
// No Thanks. The player's name is set here (Change Name, the shared
// keyboard); boards start as "CYD-" and the last 4 hex digits of the address.
#pragma once

#include <cstddef>
#include <cstdint>
#include "net/wireless.h"

namespace nearby {

bool available();                          // this build has a radio (the shell's hooks)
bool radio_start();                        // reference counted: lobby and game share it
void radio_stop();
bool radio_running();
const net::Air& air();                     // sends through the radio
net::Mac my_mac();
// Next packet that came in (false = none)
bool receive(net::Mac* from, uint8_t* buf, size_t* len);

const char* player_name();                 // loaded on first use
void        set_player_name(const char* name);

// Play Nearby for one game. `started` runs when a game was agreed (the
// overlay is closed, the radio stays on for the game); `back` when the
// player leaves with Back (the radio goes off).
using Started = void (*)(const net::Mac& peer, const char* peer_name, uint32_t session, bool inviter);
void lobby_open(const char* game_id, const char* title, Started started, void (*back)());
bool lobby_active();
void lobby_tick(uint32_t now);             // from the game's tick while active
void lobby_close();                        // stop (the game is closing); no callback

} // namespace nearby
