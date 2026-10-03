// Sudoku core: grid type, solver and generator.
// Plain C++ (no Arduino/LVGL) so it can be unit-tested on a PC.
#pragma once

#include <cstdint>

namespace sudoku {

constexpr int N = 81;

// 0 = empty, 1..9 = digit. Index = row * 9 + col.
struct Grid {
    uint8_t c[N];
};

// Small, fast RNG (xorshift32). Seed must be non-zero.
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    uint32_t below(uint32_t n) { return next() % n; }
};

enum class Difficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2, Expert = 3 };
const char* difficulty_name(Difficulty d);

inline int row_of(int i) { return i / 9; }
inline int col_of(int i) { return i % 9; }
inline int box_of(int i) { return (i / 27) * 3 + (i % 9) / 3; }
inline bool same_unit(int a, int b)
{
    return row_of(a) == row_of(b) || col_of(a) == col_of(b) || box_of(a) == box_of(b);
}

// Number of solutions, counting stops at `limit` (use 2 to test uniqueness).
// If `out` is non-null, the first solution found is written there.
int count_solutions(const Grid& g, int limit, Grid* out = nullptr);

// Make a puzzle with exactly one solution, graded by the solving techniques
// it needs (see grader.h):
//   Easy    singles only, 36+ clues
//   Medium  needs locked candidates (pointing/claiming), ~30 clues
//   Hard    needs pairs/triples or X-Wing, nothing harder
//   Expert  needs Swordfish, XY-Wing or XYZ-Wing; always solvable by logic
// Removal keeps 180-degree symmetry. `puzzle` gets the clues, `solution`
// the full grid.
// `yield` (optional) is called between attempts so a background task can
// let other tasks run.
void generate(Difficulty d, Rng& rng, Grid& puzzle, Grid& solution, void (*yield)() = nullptr);

// Technique level a puzzle needs: 1-4 as in grader.h, 5 = beyond level 4.
int difficulty_level(const Grid& puzzle);

int clue_count(const Grid& g);

} // namespace sudoku
