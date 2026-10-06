// Named sounds for every game. Games say what happened (a piece was placed,
// a move was wrong, the game was won) and this file decides how it sounds,
// so the whole collection sounds alike. Played at Settings > Volume (0 =
// silent).
//
// Tom's rule: sounds add to the game, they don't narrate every tap. Plain
// key presses (picker, menus, Settings, keyboards), picking up a piece and
// holding a die are silent. Sounds are for moves, the other side's reply,
// mistakes, hints and the end of a game. Card games (Tom, 2026-10-03) are
// silent while playing - no tap or move sounds - except Error for a move
// that isn't allowed, and play Fanfare when won.
#pragma once

#include <cstdint>

namespace ui {

enum class Sound : uint8_t {
    Place,      // a digit, mark or piece put down
    Move,       // a piece moved / tiles slid
    Error,      // a clash, an illegal move: two tones, high then low ("aww")
    Hint,       // a hint filled in
    Win,        // solved / won
    Lose,       // the computer won
    Draw,       // a tie
    Turn,       // the other side moved (computer or the other player)
    Fanfare,    // a playful tune: a solitaire-type card game won
    Trill,      // a happy little victory trill: a Blackjack hand won
    Call,       // ding-dong: a board nearby asks to play
    Whistle,    // a shell falling: a high note sliding down (You Sank My CYD!)
    Splash,     // the shell hits water
    Boom,       // the shell hits a ship
};

void sound(Sound s);

} // namespace ui
