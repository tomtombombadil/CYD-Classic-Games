// Solitaire deal checker, written for this project (MIT). Plain C++,
// host-tested. Tom wants only winnable deals (fun, not annoying), so a new
// deal is kept only when this search, seeing every card, finds a way to
// win under the deal's own rules (draw 1/3, Vegas recycle limits).
//
// Depth-first search: safe foundation moves are made without branching;
// then foundation moves, moves that turn up a hidden card or free a card
// for a foundation, waste to the columns, and turning the stock. A small
// table of position hashes stops it searching the same position twice.
// It gives up after `node_limit` positions: such deals are skipped too
// (never offered), so the player only gets deals with a known win.
#pragma once

#include <cstdint>
#include "solitaire_core.h"

namespace solitaire {

enum class Verdict : uint8_t { Win, NoWin, Unknown };

// `work` must be a Game the search may use freely (it's big: keep it on the heap)
Verdict check_deal(Game& work, uint32_t seed, int draw, Scoring scoring, uint32_t node_limit,
                   volatile bool* stop = nullptr, uint32_t* nodes = nullptr);

// Try seeds from `seed` on until one checks out as winnable; returns it
// (and how many were tried). 0 tries = stopped.
uint32_t find_winnable(uint32_t seed, int draw, Scoring scoring, uint32_t node_limit,
                       volatile bool* stop = nullptr, int* tries = nullptr);

} // namespace solitaire
