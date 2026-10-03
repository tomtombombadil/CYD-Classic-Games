// FreeCell rules, written for this project (MIT). Plain C++, host-tested
// in tools/host_tests/test_games.cpp.
//
// All 52 cards face up in eight columns (7, 7, 7, 7, 6, 6, 6, 6). Four free
// cells each hold one card; four foundations (Spades, Hearts, Clubs,
// Diamonds, left to right) build up by suit from Ace. On the columns cards
// go down in alternating colors; an empty column takes any card. Runs move
// together as far as the free spaces allow: (free cells + 1) x 2^(empty
// columns), one empty column fewer when the run goes into an empty column.
// Cards no other card could still need go up to the foundations by
// themselves (as in Windows). Nearly every deal can be won.
// Cards: 0..51 = suit * 13 + rank - 1 (as common/cards.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace freecell {

// Piles: 0..3 free cells, 4..7 foundations, 8..15 columns
enum : uint8_t { Cell0 = 0, Found0 = 4, Col0 = 8, kPiles = 16 };
constexpr int kColMax = 20, kLog = 300;
constexpr uint8_t kFoundSuit[4] = {0, 1, 3, 2};      // Spades, Hearts, Clubs, Diamonds

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int  rank(uint8_t c) { return c % 13 + 1; }
inline int  suit(uint8_t c) { return c / 13; }
inline bool red(uint8_t c)  { return suit(c) == 1 || suit(c) == 2; }
int found_for(uint8_t c);

struct Step { uint8_t from, to, count, autos; };   // autos: automatic foundation moves after it

struct Game {
    uint8_t  cell[4] = {0xFF, 0xFF, 0xFF, 0xFF};    // 0xFF = empty
    uint8_t  found[4] = {};                         // cards on each foundation (top = rank found[i])
    uint8_t  col[8][kColMax] = {};
    uint8_t  n[8] = {};
    uint32_t seed = 0;
    uint16_t moves = 0;
    Step     log[kLog];
    uint16_t log_n = 0;
    uint8_t  auto_from[64] = {};                    // automatic moves, in order: the pile ...
    uint8_t  auto_to[64] = {};                      // ... and the foundation (0..3)
    uint16_t auto_n = 0;

    void deal(uint32_t seed);
    int  free_cells() const;
    int  empty_cols() const;
    int  max_run(bool to_empty) const;
    int  run_start(int c) const;                    // lowest index of the top run in a column
    bool can_move(int from, int idx, int to) const; // idx: card index in a column, else 0
    bool move(int from, int idx, int to);           // then the automatic foundation moves
    int  best_target(int from, int idx) const;      // foundation, else column, else free cell; -1
    bool undo();
    bool won() const { return found[0] + found[1] + found[2] + found[3] == 52; }
    bool hint(int* from, int* idx, int* to) const;
    uint8_t top(int pile) const;                    // 0xFF if empty

    size_t serialize(uint8_t* buf, size_t cap) const;   // "FRC1" + state (no undo log)
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 4 + 4 + 8 * (1 + kColMax) + 4 + 2;

private:
    uint8_t auto_up();
};

} // namespace freecell
