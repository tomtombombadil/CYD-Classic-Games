// The wireless games' fixed keys and versions (Tom, 2026-10-04).
//
// Boards play a game together when their link version (net::kLink) and
// that game's version here match - not their firmware versions. A game's
// KEY is what goes on the air and never changes (it is not the game's
// place in any list). Its VERSION goes up whenever what the game sends
// changes meaning: its move keys, its rules, who moves first. CI plays
// fixed games of every wireless game (tools/host_tests/test_movekeys.cpp)
// and fails when the moves sent change while the version didn't.
//
// Adding a wireless game: a new line with the next unused key, version 1,
// and a line in tools/host_tests/net_moves.txt (the test prints it).
// Plain C++.
#pragma once

#include <cstdint>
#include <cstring>

namespace netgames {

struct Entry {
    const char* id;          // registry id (games.def)
    uint8_t     key;         // on the air, forever
    uint8_t     version;     // its moves and rules
};

constexpr Entry kGames[] = {
    {"fourconnect", 1, 1},   // move = column 0-6
    {"tictactoe",   2, 1},   // move = cell 0-8
    {"reversi",     3, 1},   // move = square 0-63 (passes are automatic)
    {"checkers",    4, 1},   // move = checkers::move_key (from + each landing's direction)
    {"chess",       5, 1},   // move = chess::move_key (from, to, promotion)
    {"mancala",     6, 1},   // move = pit 0-5 of the side to move
    {"morris",      7, 1},   // move = morris::Move::code() (from, to, man taken)
    {"sank",        8, 2},   // plies 0-9 = the ships (sank::ship_key: cell | down << 7), then cell 0-99
                             // (v1: a fleet seed a side, ships apart)
    {"ultimate",    9, 1},   // move = cell 0-80 (small board * 9 + square)
    {"gomoku",     10, 1},   // move = point 0-224 (row * 15 + column)
};
constexpr int kCount = int(sizeof kGames / sizeof kGames[0]);

inline const Entry* by_id(const char* id)
{
    for (const Entry& e : kGames) if (strcmp(e.id, id) == 0) return &e;
    return nullptr;
}

inline const Entry* by_key(int key)
{
    for (const Entry& e : kGames) if (e.key == key) return &e;
    return nullptr;
}

} // namespace netgames
