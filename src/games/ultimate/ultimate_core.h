// Ultimate Tic-Tac-Toe rules and computer player, written for this project
// (MIT). Plain C++, host-tested.
//
// Nine small boards in a 3x3 big board. X starts anywhere. The square you
// play in a small board sends the other player to the small board in the
// same place of the big board; if that board is already won or full, they
// may play in any open board. Three in a row in a small board wins it;
// three won boards in a row on the big board wins the game. All boards
// won or full without that = a draw.
//
// Cells 0..80: board * 9 + square (both 0..8, row by row). A move is its
// cell (also the wireless move key).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ultimate {

constexpr int kCells = 81;

struct Board {
    uint8_t  cell[kCells] = {};       // 0 empty, 1 X, 2 O
    uint8_t  small[9] = {};           // 0 open, 1 X won, 2 O won, 3 full (nobody)
    int8_t   next = -1;               // the board the player must play in, -1 any open
    uint16_t moves = 0;
    int8_t   last = -1;               // the last cell played

    int  turn() const { return moves & 1; }          // 0 = X, 1 = O
    bool board_open(int b) const { return small[b] == 0; }
    // May the side to move play in small board b?
    bool board_live(int b) const { return board_open(b) && (next < 0 || next == b); }
    bool can_play(int c) const;
    bool play(int c);
    int  result() const;              // -1 on, 0 X won, 1 O won, 2 draw
    int  legal(uint8_t* out) const;   // the legal cells, returns how many
    bool winning_line(int* a, int* b) const;   // the big board's three in a row (board numbers)

    size_t serialize(uint8_t* buf, size_t cap) const;   // "UTT1" + cells packed + next + moves + last
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 21 + 1 + 2 + 1;
};

// Computer move for the side to move. Level 0 looks 2 moves ahead, 1
// looks 4, 2 deepens within a node budget (about a second on the board).
int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace ultimate
