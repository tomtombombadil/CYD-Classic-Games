// Hollywood CYDs rules and computer player, written for this project
// (MIT). Plain C++, host-tested. (Tom's name, 2026-10-06, for the classic
// tic-tac-toe quiz show.) Questions come from the shared trivia bank
// (trivia_bank.h, Open Trivia DB, CC BY-SA 4.0).
//
// A 3 x 3 board of nine stars (our own made-up characters). On your turn
// you pick a star; the star is asked a question and gives an answer -
// right, or a bluff. You Agree or Disagree. Judge right and the square is
// yours; judge wrong and it goes to the other player - unless that would
// win them the game: a winning square must be earned. Three in a row, or
// any five squares, wins. X (side 0) starts.
//
// Moves for the side to move: 0-8 pick a star (Pick), 9 Agree, 10
// Disagree (Judge). The same side picks and then judges; then the turn
// passes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hcyd {

constexpr int kSquares = 9, kBankMax = 6144;
constexpr int kAgree = 9, kDisagree = 10;
enum class Phase : uint8_t { Pick, Judge, Over };

extern const char* const kStars[kSquares];

struct Board {
    int8_t   owner[kSquares];        // -1 open, 0 X, 1 O
    uint8_t  turn = 0;
    Phase    phase = Phase::Pick;
    int8_t   square = -1;            // the star being asked
    int16_t  q = -1;                 // the question
    int8_t   star_says = 0;          // which of the question's answers the star gives (0 = the right one)
    int8_t   winner = -1;            // 0 / 1 (no draws: the board always ends with someone at 5)
    // the last judgment (for the screen)
    int8_t   last_square = -1, last_side = -1, last_got = -1;   // last_got: who got the square (-1 nobody)
    uint8_t  last_right = 0;         // the judge was right
    uint16_t moves = 0;
    uint32_t rng = 1;
    uint8_t  played[kBankMax / 8] = {};

    Board();
    void reset(uint32_t seed);       // keeps the played questions
    uint32_t rand_next();
    int  result() const { return winner; }
    bool can_play(int m) const;
    bool play(int m);
    bool star_right() const { return star_says == 0; }
    int  count(int side) const;
    bool wins_with(int side, int square) const;     // would taking `square` win it for `side`?

    size_t serialize(uint8_t* buf, size_t cap) const;   // "HCY1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kSquares + 1 + 1 + 1 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 2 + 4 + kBankMax / 8;

private:
    void ask();
};

// Computer: the move for the side to move. Easy picks any star and knows
// 40 % of answers; Medium plays tic-tac-toe well and knows 55 %; Hard
// knows 70 % (less on hard questions). Not knowing, it trusts the star.
int best_move(const Board& b, int level, uint32_t seed);

} // namespace hcyd
