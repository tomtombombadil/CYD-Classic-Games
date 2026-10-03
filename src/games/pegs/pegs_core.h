// Peg Solitaire rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// A peg jumps over a neighbouring peg into an empty hole straight beyond
// it; the jumped peg is removed. The goal is one peg left. Boards (levels):
//   Triangle  15 holes in 5 rows, jumps in 6 directions, top hole empty
//   English   33 holes (the cross), centre empty
//   European  37 holes, the hole two above the centre empty (the centre
//             start can't be solved on this board)
// Holes live on a 7x7 grid (sq = row * 7 + col). The triangle uses rows
// 0..4 with col <= row; it is drawn shifted half a hole per row.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pegs {

constexpr int kN = 7, kSquares = kN * kN, kLevels = 3, kMaxMoves = 64;
enum Level : uint8_t { Triangle = 0, English = 1, European = 2 };

struct Move { uint8_t from, over, to; };

struct Game {
    uint8_t  level = English;
    uint64_t holes = 0;            // bit sq = a hole exists
    uint64_t pegs = 0;             // bit sq = a peg is in it
    uint8_t  history[kMaxMoves][3] = {};
    uint8_t  moves = 0;            // moves made (undo pops them)

    void start(int level);
    bool hole(int sq) const { return sq >= 0 && sq < kSquares && (holes >> sq & 1); }
    bool peg(int sq) const  { return sq >= 0 && sq < kSquares && (pegs >> sq & 1); }
    int  peg_count() const;
    // Legal jump from -> to? (fills `m`)
    bool find(int from, int to, Move* m = nullptr) const;
    int  legal(Move* out) const;   // every legal jump, returns the count (<= 4 * 37)
    bool can_move_from(int sq) const;
    bool play(int from, int to);
    bool undo();
    bool solved() const { return peg_count() == 1; }
    bool stuck() const;            // no jump left

    size_t serialize(uint8_t* buf, size_t cap) const;  // "PEG1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 8 + 1 + kMaxMoves * 3;
};

// Board shape per level (bit sq = a hole), the starting empty hole and
// the jump directions (as row/col steps)
uint64_t level_holes(int level);
int      level_start_hole(int level);
int      level_dirs(int level, const int8_t (**dirs)[2]);
const char* level_name(int level);

} // namespace pegs
