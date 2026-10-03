// Golf solitaire rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// Seven columns of five cards, all face up; the other 17 are the stock,
// one of them turned onto the waste to start. Play the top card of any
// column onto the waste if it is one rank higher or lower (suits don't
// matter; Ace and King don't wrap, and nothing goes on a King). When stuck,
// turn the next stock card onto the waste. Clear all 35 column cards to
// win; otherwise the cards left over are your score (lower is better).
// Cards: 0..51 = suit * 13 + rank - 1 (as common/cards.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace golf {

constexpr int kCols = 7, kDepth = 5, kStock = 16;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int rank(uint8_t c) { return c % 13 + 1; }

struct Game {
    uint8_t  col[kCols][kDepth] = {};
    uint8_t  col_n[kCols] = {};
    uint8_t  stock[kStock] = {};
    uint8_t  stock_n = 0;
    uint8_t  waste[52] = {};
    uint8_t  waste_n = 0;
    uint8_t  log[64] = {};          // each step: column 0..6, or 7 = stock turned
    uint8_t  log_n = 0;
    uint32_t seed = 0;

    void deal(uint32_t seed);
    int  left() const;              // cards still in the columns
    bool can_play(int c) const;
    bool play(int c);
    bool draw();                    // turn a stock card
    bool undo();
    bool won() const { return left() == 0; }
    bool stuck() const;             // no play and no stock: the game is over
    int  hint() const;              // a playable column, 7 = turn the stock, -1 none

    size_t serialize(uint8_t* buf, size_t cap) const;   // "GLF1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kCols * kDepth + kCols + kStock + 1 + 52 + 1 + 64 + 1 + 4;
};

} // namespace golf
