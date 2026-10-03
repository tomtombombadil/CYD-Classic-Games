// Memory Match rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// Tiles lie face down in a grid, each picture on exactly two tiles. Turn
// two: a pair stays face up, two different pictures stay up until the next
// tap, which turns them back over and turns the tapped tile (no waiting on
// a timer). Find every pair in as few turns as you can.
// Levels: 4x4 (8 pairs), 4x5 (10), 5x6 (15).
#pragma once

#include <cstddef>
#include <cstdint>

namespace memory {

constexpr int kLevels = 3, kMaxTiles = 30, kPictures = 15;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

void level_size(int level, int* cols, int* rows);

enum class Tap : uint8_t { Ignored, First, Match, Miss };

struct Game {
    uint8_t  level = 0, cols = 4, rows = 4;
    uint8_t  pic[kMaxTiles] = {};          // picture per tile
    uint8_t  matched[kMaxTiles] = {};      // 1 = pair found
    int8_t   up_a = -1, up_b = -1;         // tiles face up and not yet matched
    uint16_t turns = 0;                    // pairs of tiles turned
    uint8_t  found = 0;                    // pairs found

    void start(int level, Rng& rng);
    int  tiles() const { return cols * rows; }
    int  pairs() const { return tiles() / 2; }
    bool face_up(int i) const { return matched[i] || i == up_a || i == up_b; }
    bool solved() const { return found == pairs(); }
    Tap  tap(int i);

    size_t serialize(uint8_t* buf, size_t cap) const;   // "MEM1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 3 + 2 * kMaxTiles + 2 + 2 + 1;
};

} // namespace memory
