// Pyramid solitaire rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// 28 cards in a pyramid of 7 rows, each row overlapping the one above; the
// other 24 are the stock. Remove pairs that add up to 13 (Ace 1, Jack 11,
// Queen 12); a King (13) goes by itself. Only uncovered cards count: a card
// is uncovered once both cards resting on it below are gone. Tap the stock
// to turn its next card onto the waste; the waste's top card can pair too.
// The stock can be gone through three times. Clear the pyramid to win.
// Slots: 0..27 = pyramid (row r, card k: r * (r + 1) / 2 + k), 28 = waste top.
#pragma once

#include <cstddef>
#include <cstdint>

namespace pyramid {

constexpr int kRows = 7, kPyr = 28, kWasteSlot = 28, kPasses = 3, kLog = 160;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int rank(uint8_t c) { return c % 13 + 1; }
inline int row_of(int i) { int r = 0; while ((r + 1) * (r + 2) / 2 <= i) ++r; return r; }

struct Step { uint8_t kind, a, b, ca, cb; };   // kind 0 pair/king, 1 draw, 2 recycle

struct Game {
    uint8_t  pyr[kPyr] = {};
    uint32_t gone = 0;                 // bit i = pyramid card i removed
    uint8_t  stock[24] = {}, stock_n = 0;
    uint8_t  waste[24] = {}, waste_n = 0;
    uint8_t  passes = 1;               // times through the stock (this one included)
    Step     log[kLog];
    uint8_t  log_n = 0;
    uint32_t seed = 0;
    uint16_t moves = 0;

    void deal(uint32_t seed);
    bool present(int slot) const;      // a card is there
    bool free(int slot) const;         // present and uncovered
    uint8_t card(int slot) const;
    bool can_pair(int a, int b) const; // b = -1: a King alone
    bool pair(int a, int b);
    bool can_draw() const;
    bool draw();                       // turn a stock card, or turn the waste back
    bool undo();
    int  removed() const;              // pyramid cards gone
    bool won() const { return removed() == kPyr; }
    bool stuck() const;                // nothing left to do
    // Hint: a pair (a, b) or King (a, -1); a = -2 means "turn the stock"
    bool hint(int* a, int* b) const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "PYR1" + state (no log)
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kPyr + 4 + 25 + 25 + 1 + 4 + 2;
};

} // namespace pyramid
