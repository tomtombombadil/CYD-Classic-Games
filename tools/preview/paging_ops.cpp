// Preview only: ops for the fake entries in paging_test.def.
#include "games/registry.h"
namespace games {
extern const GameOps sudoku_ops;
extern const Help sudoku_help;
#define FAKE(x) extern const GameOps x##_ops; const GameOps x##_ops = sudoku_ops; \
    extern const Help x##_help; const Help x##_help = sudoku_help;
FAKE(fake_a) FAKE(fake_b) FAKE(fake_c) FAKE(fake_d) FAKE(fake_e) FAKE(fake_f)
FAKE(fake_g) FAKE(fake_h) FAKE(fake_i) FAKE(fake_j) FAKE(fake_k) FAKE(fake_l)
}
