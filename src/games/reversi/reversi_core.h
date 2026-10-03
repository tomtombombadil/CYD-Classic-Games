// Reversi rules and computer player. Plain C++, host-tested.
// 8x8, Black (side 0) moves first. A move must outflank: it flips every
// straight line of opponent discs ended by one of yours. A side with no
// move passes (done automatically inside play()); the game ends when
// neither side can move. Squares: sq = row * 8 + col, row 0 = top.
#pragma once

#include <cstddef>
#include <cstdint>

namespace reversi {

constexpr int kPass = 64;

struct Board {
    uint64_t disc[2] = {0, 0};       // Black, White
    uint8_t  side = 0;               // to move
    uint8_t  plies = 0;              // entries in history (passes included)
    uint8_t  history[80] = {};       // squares, kPass for a pass

    Board();                         // the starting position
    uint64_t legal() const;          // moves for the side to move
    bool     can_play(int sq) const { return sq >= 0 && sq < 64 && ((legal() >> sq) & 1); }
    // Make a legal move; then, if the other side has no move but the game
    // isn't over, it passes automatically. Returns false if illegal.
    bool     play(int sq);
    bool     over() const;
    int      count(int s) const      { return __builtin_popcountll(disc[s]); }
    int      cell(int sq) const;     // -1 empty, 0 Black, 1 White
    // -1 game on, 0/1 winner (more discs), 2 draw
    int      result() const;
    int      moves() const;          // real moves made (passes not counted)
    int      last_move() const;      // last real move's square, -1 if none
    bool     last_was_pass() const   { return plies && history[plies - 1] == kPass; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "RVS1" + plies + history
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 80;
};

uint64_t flips(uint64_t me, uint64_t op, int sq);     // discs flipped by playing sq

// Computer move (a square) for the side to move. depth: Easy 2, Medium 4,
// Hard 6; Hard also plays the last 10 empty squares perfectly. Ties go by
// `seed`. `stop` ends the search early (best so far).
int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace reversi
