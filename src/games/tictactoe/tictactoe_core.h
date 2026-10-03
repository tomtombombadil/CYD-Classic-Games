// Tic-Tac-Toe rules and computer player. Plain C++, host-tested.
// Cells 0..8, row by row. Side 0 = X (always moves first), side 1 = O.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tictactoe {

struct Board {
    int8_t  cell[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};   // -1 empty, else side
    uint8_t moves = 0;
    uint8_t history[9] = {};

    int  turn() const             { return moves & 1; }
    bool can_play(int i) const    { return i >= 0 && i < 9 && cell[i] < 0 && result() == -1; }
    bool play(int i);
    bool undo();
    // -1 = game on, 0/1 = that side has three in a row, 2 = draw
    int  result() const;
    // The winning line's cells (a[0..2]); false if nobody has won
    bool winning_line(int a[3]) const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "TTT1" + moves + history
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 9;
};

// Computer move for the side to move. depth: Easy 1, Medium 2, Hard 9
// (perfect play). Equal moves are chosen between by `seed`.
int best_move(const Board& b, int depth, uint32_t seed);
int depth_for_level(int level);

} // namespace tictactoe
