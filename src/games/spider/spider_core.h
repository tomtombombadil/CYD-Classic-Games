// Spider solitaire rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// Two decks (104 cards) in one, two or four suits (the levels). Ten
// columns: the first four get six cards, the rest five, only the top one
// face up; the other 50 are the stock, dealt ten at a time (one on every
// column; not while a column is empty). A card goes on any card one rank
// higher, any suit; an empty column takes anything. A run of cards in one
// suit going down by one moves together. A full run King..Ace in one suit
// is taken off by itself. Take off all eight to win.
// Score (Windows): 500 to start, -1 a move (deals too), +100 a run.
// Cards: 0..51 = suit * 13 + rank - 1 (duplicates); bit 7 = face down.
#pragma once

#include <cstddef>
#include <cstdint>

namespace spider {

constexpr int kCols = 10, kColMax = 60, kLog = 300, kLevels = 3;
constexpr uint8_t kDown = 0x80;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int rank(uint8_t c) { return (c & 0x3F) % 13 + 1; }
inline int suit(uint8_t c) { return (c & 0x3F) / 13; }
inline bool up(uint8_t c)  { return !(c & kDown); }
int suits_for(int level);              // 1, 2, 4

struct Step {
    uint8_t  kind;                     // 0 move, 1 deal
    uint8_t  from, to, count;
    uint8_t  flipped_from;             // the move turned up the card it left
    uint16_t done_mask;                // columns where a run came off after this step
    uint16_t flip_mask;                // ... and the card under that run turned up
    uint32_t done_suits;               // 2 bits a column: the suit of that run
    int16_t  score;                    // score before
};

struct Game {
    uint8_t  level = 0;
    uint8_t  col[kCols][kColMax] = {};
    uint8_t  n[kCols] = {};
    uint8_t  stock[50] = {};
    uint8_t  stock_n = 0;
    uint8_t  done = 0;                 // runs taken off
    uint8_t  done_suit[8] = {};
    int16_t  score = 500;
    uint16_t moves = 0;
    uint32_t seed = 0;
    Step     log[kLog];
    uint16_t log_n = 0;

    void deal(uint32_t seed, int level);
    int  first_up(int c) const;
    int  run_start(int c) const;       // lowest index of the movable run on top
    bool can_move(int from, int idx, int to) const;
    bool move(int from, int idx, int to);
    bool can_deal() const;
    bool deal_row();
    bool undo();
    bool won() const { return done == 8; }
    int  best_target(int from, int idx) const;   // a same-suit card first, then any, then empty; -1
    // Hint: a move (from, idx, to) or from = -1 for "deal"
    bool hint(int* from, int* idx, int* to) const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "SPD1" + state (no undo log)
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + kCols * (1 + kColMax) + 1 + 50 + 1 + 8 + 2 + 2 + 4;

private:
    uint16_t take_runs(uint16_t cols, uint16_t* flips, uint32_t* suits);
};

} // namespace spider
