// Human-style solver used to grade puzzle difficulty.
//
// It solves the way a person does: always apply the easiest technique that
// makes progress, and note the hardest one ever needed. That hardest
// technique is the puzzle's level. Plain C++ (tested on the PC).
//
//   Level 1  Singles         naked single, hidden single
//   Level 2  Locked cand.    pointing, claiming
//   Level 3  Subsets / fish  naked + hidden pairs and triples, X-Wing
//   Level 4  Advanced        Swordfish, XY-Wing, XYZ-Wing
//
// A puzzle that needs something beyond level 4 (chains, guessing) can't be
// finished by this solver.
#pragma once

#include <cstdint>
#include "sudoku_core.h"

namespace sudoku {

struct GradeResult {
    bool    solved = false;   // finished using techniques up to max_level
    uint8_t level  = 0;       // hardest technique level used (0 = nothing to do)
    uint16_t steps = 0;       // placements + elimination rounds
    Grid    grid{};           // the (partial) result
};

// Solve `g` logically using techniques up to `max_level` (1..4).
GradeResult grade(const Grid& g, int max_level = 4);

} // namespace sudoku
