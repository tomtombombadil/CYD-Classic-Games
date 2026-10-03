// MasterCYD (a Mastermind-style code breaker) rules, written for this
// project (MIT). Plain C++, host-tested in tools/host_tests/test_games.cpp.
//
// The CYD hides a code of 4 or 5 pegs, each one of 6 colors. You have 10
// guesses. After each guess you learn how many pegs are the right color in
// the right place ("exact") and how many more are a right color in the
// wrong place ("near"). Levels:
//   Easy    4 pegs, no color used twice
//   Normal  4 pegs, colors may repeat
//   Hard    5 pegs, colors may repeat
#pragma once

#include <cstddef>
#include <cstdint>

namespace mastercyd {

constexpr int kColors = 6, kMaxPegs = 5, kRows = 10, kLevels = 3;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Feedback { uint8_t exact = 0, near = 0; };

// Score `guess` against `code` (n pegs, colors 0..kColors-1)
Feedback score(const uint8_t* code, const uint8_t* guess, int n);

constexpr uint8_t kEmpty = 0xFF;

struct Game {
    uint8_t  level = 1;
    uint8_t  secret[kMaxPegs] = {};
    uint8_t  guess[kRows][kMaxPegs] = {};
    Feedback fb[kRows];
    uint8_t  rows = 0;                         // guesses made
    uint8_t  cur[kMaxPegs] = {kEmpty, kEmpty, kEmpty, kEmpty, kEmpty};   // row being entered

    int  pegs() const { return level >= 2 ? 5 : 4; }
    bool repeats() const { return level >= 1; }
    void start(int level, Rng& rng);
    bool solved() const { return rows && fb[rows - 1].exact == pegs(); }
    bool over() const { return solved() || rows >= kRows; }

    // Entering a guess: put a color in the first empty slot (false if the
    // row is full, or Easy and the color is already in the row); clear a
    // slot; submit when every slot is filled.
    bool place(uint8_t color);
    void clear(int slot);
    bool full() const;
    bool submit();

    size_t serialize(uint8_t* buf, size_t cap) const;   // "MCD1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + kMaxPegs + kRows * kMaxPegs + 2 * kRows + 1 + kMaxPegs;
};

} // namespace mastercyd
