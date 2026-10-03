// FourConnect (a Connect Four style game) rules and computer player.
// Plain C++, host-tested in tools/host_tests/test_fourconnect.cpp.
//
// 7 columns x 6 rows. Pieces drop to the lowest free row of a column.
// Bitboards: bit = col * 7 + row (row 0 = bottom); bit 6 of each column is
// a spare so lines can't wrap from one column into the next.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fourconnect {

constexpr int kCols = 7;
constexpr int kRows = 6;

struct Board {
    uint64_t bits[2] = {0, 0};          // side 0 (moves first), side 1
    uint8_t  height[kCols] = {};
    uint8_t  moves = 0;
    uint8_t  history[kCols * kRows] = {};

    int  turn() const              { return moves & 1; }      // side to move
    bool can_play(int col) const   { return col >= 0 && col < kCols && height[col] < kRows; }
    bool play(int col);                                      // false if the column is full
    bool undo();
    int  cell(int col, int row) const;                       // -1 empty, else side
    int  last_col() const          { return moves ? history[moves - 1] : -1; }
    // -1 = game on, 0/1 = that side has four in a row, 2 = board full (draw)
    int      result() const;
    uint64_t winning_cells() const;                          // the four (or more) cells, 0 if none

    // Save image: "FCN1" + moves + history. Returns bytes, 0 if cap too small.
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + kCols * kRows;
};

inline uint64_t bit_of(int col, int row) { return 1ull << (col * 7 + row); }

// Computer move for the side to move. depth = plies searched (Easy 1,
// Medium 3, Hard 8). Ties between equally good moves go by `seed`, so
// games vary without the computer making deliberate mistakes. `stop`
// (optional) ends the search early; the best move so far is returned.
int  best_move(const Board& b, int depth, uint32_t seed, volatile bool* stop = nullptr);
int  depth_for_level(int level);     // 0..2

} // namespace fourconnect
