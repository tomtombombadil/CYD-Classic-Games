// Nonograms (paint-by-numbers) rules and generator, written for this
// project (MIT). Plain C++, host-tested in tools/host_tests/test_games.cpp.
//
// A hidden picture on an n x n grid. Each row and column has a clue: the
// lengths of its runs of filled cells, in order. Fill the cells so every
// clue matches. Levels: 5x5, 8x8, 10x10.
//
// Puzzles are random pictures, mirrored left-right so they look like small
// pictograms, kept only when line-by-line logic alone solves them (a row or
// column at a time, from its clue and the cells already known). So every
// puzzle has exactly one answer and never needs a guess.
#pragma once

#include <cstddef>
#include <cstdint>

namespace nonogram {

constexpr int kMaxN = 10, kMaxClues = 5, kLevels = 3;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

int level_size(int level);                       // 5, 8, 10

struct Clue { uint8_t n = 0, run[kMaxClues] = {}; };

// Clue of a line given as bits (bit i = cell i filled)
Clue clue_of(uint16_t line, int n);

enum Cell : uint8_t { Unknown = 0, Filled = 1, Marked = 2 };   // Marked = known empty

// Solve the clues by line logic only. Writes the result into `out`
// (n*n, row by row); true if every cell got decided.
bool line_solve(int n, const Clue* rows, const Clue* cols, uint8_t* out);

struct Game {
    uint8_t  level = 0, n = 5;
    uint16_t picture[kMaxN] = {};                // solution rows (bit c = column c)
    Clue     rows[kMaxN], cols[kMaxN];
    uint8_t  cell[kMaxN * kMaxN] = {};           // the player's grid
    uint16_t taps = 0;

    // Generate a puzzle; tries random pictures until line logic solves one
    void start(int level, Rng& rng);
    void set_picture(int n, const uint16_t* rows_bits);   // fixed picture (tests, preview)
    // Tap in a mode: Filled toggles filled/unknown, Marked toggles marked/unknown
    void tap(int r, int c, Cell mode);
    bool row_done(int r) const;                  // the row's filled cells match its clue
    bool col_done(int c) const;
    bool solved() const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "NON1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 + 2 * kMaxN + kMaxN * kMaxN + 2;
};

} // namespace nonogram
