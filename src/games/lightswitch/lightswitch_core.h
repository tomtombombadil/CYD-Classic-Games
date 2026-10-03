// Light Switch (a Lights Out style puzzle). Plain C++, host-tested.
// 5x5 lights; pressing one flips it and its four neighbours. Turn them all
// off. Puzzles are made by pressing random lights on a dark board, so they
// can always be solved; "par" is the fewest presses that solve it (found
// by solving the press equations over GF(2)).
#pragma once

#include <cstddef>
#include <cstdint>

namespace lightswitch {

constexpr int kN = 5;
constexpr int kCells = kN * kN;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

uint32_t press_mask(int i);               // the lights pressing i flips

// Fewest presses that turn `lights` all off (bit i = light i on).
// Writes the presses to `*solution` (bit i = press i). -1 if unsolvable.
int  min_presses(uint32_t lights, uint32_t* solution = nullptr);

struct Puzzle {
    uint32_t lights = 0;                  // bit i = light i is on
    uint32_t start  = 0;                  // for Restart
    uint16_t moves  = 0;
    uint16_t par    = 0;

    // level 0..2: 4, 8 or 14 random distinct presses
    void generate(int level, Rng& rng);
    void press(int i)    { lights ^= press_mask(i); ++moves; }
    bool on(int i) const { return (lights >> i) & 1; }
    bool solved() const  { return lights == 0; }
    void restart()       { lights = start; moves = 0; }
    // A press that is part of a fewest-presses solution from here, -1 if solved
    int  hint() const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "LSW1" + lights + start + moves + par
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 4 + 4 + 2 + 2;
};

} // namespace lightswitch
