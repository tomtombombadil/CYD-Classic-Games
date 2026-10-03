// Named sounds for every game. Games say what happened (a piece was placed,
// a move was wrong, the game was won) and this file decides how it sounds,
// so the whole collection sounds alike. Silent when Settings > Sound is Off.
#pragma once

#include <cstdint>

namespace ui {

enum class Sound : uint8_t {
    Tap,        // any key (played by make_key)
    Place,      // a digit, mark or piece put down
    Move,       // a piece moved / tiles slid
    Select,     // a piece or cell picked up
    Error,      // a clash, an illegal move
    Hint,       // a hint filled in
    Win,        // solved / won
    Lose,       // the computer won
    Draw,       // a tie
    Turn,       // the other side moved (computer or the other player)
};

void sound(Sound s);

} // namespace ui
