// Sliding Tiles (the 15-Puzzle and its 3x3 / 5x5 cousins). Plain C++,
// host-tested. Tap a tile in the gap's row or column and every tile between
// it and the gap slides one place toward the gap.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sliding {

constexpr int kMaxN = 5;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Puzzle {
    uint8_t  n = 4;                       // 3, 4 or 5
    uint8_t  tile[kMaxN * kMaxN] = {};    // 1..n*n-1, 0 = the gap
    uint8_t  gap = 0;                     // index of the gap
    uint16_t moves = 0;                   // tiles moved so far

    void reset(int size);                 // solved order, gap last
    // Mix by random legal slides from the solved position, so it is always
    // solvable. Never leaves it solved.
    void shuffle(Rng& rng);
    // Tap cell i: slides the tiles between i and the gap. Returns how many
    // tiles moved (0 = not in the gap's row or column).
    int  tap(int i);
    bool solved() const;
    bool in_place(int i) const { return tile[i] && tile[i] == i + 1; }
    int  cells() const { return n * n; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "SLD1" + n + moves + tiles
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 2 + kMaxN * kMaxN;
};

int size_for_level(int level);            // 0..2 -> 3, 4, 5

} // namespace sliding
