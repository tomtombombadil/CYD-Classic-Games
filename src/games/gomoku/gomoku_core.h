// Gomoku (five in a row) rules and computer player, written for this
// project (MIT). Plain C++, host-tested.
//
// A 15x15 board; Black places first, then the players take turns placing
// one stone on any empty point. Five or more of your stones in a line -
// across, down or diagonal - wins (freestyle: a longer line counts too).
// A full board is a draw.
//
// Points 0..224 (row * 15 + column). A move is its point (also the
// wireless move key).
#pragma once

#include <cstddef>
#include <cstdint>

namespace gomoku {

constexpr int kN = 15;
constexpr int kPoints = kN * kN;

struct Board {
    uint8_t  stone[kPoints] = {};     // 0 empty, 1 Black, 2 White
    uint16_t moves = 0;
    int16_t  last = -1;
    int8_t   winner = -1;             // set by play(): 0 Black, 1 White

    int  turn() const { return moves & 1; }          // 0 = Black
    bool can_play(int p) const { return p >= 0 && p < kPoints && !stone[p] && result() == -1; }
    bool play(int p);
    int  result() const;              // -1 on, 0/1 winner, 2 draw (board full)
    // The winning five (or more): first and last point
    bool winning_line(int* a, int* b) const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "GMK1" + stones packed + moves + last
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 57 + 2 + 2;
};

// Computer move for the side to move. Level 0 takes the point that looks
// best right now, thinking mostly of its own lines; 1 looks 2 moves ahead,
// 2 looks 4 ahead, among the most promising points. All take a win and
// block a five.
int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace gomoku
