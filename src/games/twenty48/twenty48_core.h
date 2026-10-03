// 2048 rules and play history, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// 4x4 board; a cell holds an exponent (1 = 2, 2 = 4, ... 11 = 2048), 0 is
// empty. A slide moves every tile as far as it goes in one direction; two
// equal tiles that meet merge into one of double value (each tile merges at
// most once per slide; the pair nearest the wall merges first). A slide that
// moves something adds a new tile, a 2 (90 %) or a 4 (10 %), on a random
// empty cell. Score = sum of all merged tiles. Reaching 2048 wins; play may
// go on. The game is over when no slide moves anything.
#pragma once

#include <cstddef>
#include <cstdint>

namespace twenty48 {

constexpr int kSize = 4, kCells = kSize * kSize;
constexpr uint8_t kWinExp = 11;        // 2048

enum class Dir : uint8_t { Up = 0, Right = 1, Down = 2, Left = 3 };

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Game {
    uint8_t  cell[kCells] = {};        // exponents, row by row, 0 = empty
    uint32_t score = 0;
    uint16_t moves = 0;
    int8_t   spawned = -1;             // cell of the newest tile (for a marker)
    bool     won = false;              // 2048 reached (stays true)

    void start(Rng& rng);              // empty board + two tiles
    // Slide; true if anything moved (then a tile is added and moves++).
    // `merged_to` (optional) gets the largest exponent made by a merge, or 0.
    bool slide(Dir d, Rng& rng, uint8_t* merged_to = nullptr);
    bool can_slide(Dir d) const;
    bool over() const;                 // no slide moves anything
    uint8_t max_exp() const;
    static uint32_t value(uint8_t e) { return e ? (1u << e) : 0; }

    size_t serialize(uint8_t* buf, size_t cap) const;    // "T48A" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kCells + 4 + 2 + 1 + 1;
};

// ---- Play history ----------------------------------------------------------------------
//   #,Score,Best Tile,Moves,Seconds,Time
//   4,20512,2048,1043,1840,30:40
struct Record {
    uint32_t score = 0;
    uint32_t tile = 0;                 // largest tile value
    uint16_t moves = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, best = 0, total = 0, best_tile = 0, wins = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
    uint32_t average() const { return games ? (total + games / 2) / games : 0; }
};

} // namespace twenty48
