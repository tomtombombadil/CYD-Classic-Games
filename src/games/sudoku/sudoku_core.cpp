#include "sudoku_core.h"
#include "sudoku_grader.h"

namespace sudoku {

namespace {

// Bitmask solver: bit d (1..9) set in a row/col/box mask = digit used there.
struct Solver {
    uint8_t  cell[N];
    uint16_t row[9], col[9], box[9];
    int      solutions = 0;
    int      limit     = 2;
    Grid*    out       = nullptr;
    Rng*     rng       = nullptr;   // when set, digits are tried in random order

    bool load(const Grid& g)
    {
        for (int i = 0; i < 9; ++i) row[i] = col[i] = box[i] = 0;
        for (int i = 0; i < N; ++i) {
            cell[i] = g.c[i];
            if (!g.c[i]) continue;
            const uint16_t bit = 1u << g.c[i];
            const int r = row_of(i), c = col_of(i), b = box_of(i);
            if ((row[r] | col[c] | box[b]) & bit) return false;  // givens conflict
            row[r] |= bit; col[c] |= bit; box[b] |= bit;
        }
        return true;
    }

    void search()
    {
        // Pick the empty cell with the fewest candidates (MRV).
        int best = -1, best_count = 10;
        uint16_t best_cand = 0;
        for (int i = 0; i < N; ++i) {
            if (cell[i]) continue;
            const uint16_t cand = ~(row[row_of(i)] | col[col_of(i)] | box[box_of(i)]) & 0x3FE;
            const int n = __builtin_popcount(cand);
            if (n < best_count) {
                best = i; best_count = n; best_cand = cand;
                if (n <= 1) break;
            }
        }
        if (best < 0) {                       // grid full: a solution
            if (solutions == 0 && out) for (int i = 0; i < N; ++i) out->c[i] = cell[i];
            ++solutions;
            return;
        }
        if (best_count == 0) return;          // dead end

        uint8_t order[9];
        int n = 0;
        for (int d = 1; d <= 9; ++d) if (best_cand & (1u << d)) order[n++] = d;
        if (rng) for (int k = n - 1; k > 0; --k) {
            const int j = rng->below(k + 1);
            const uint8_t t = order[k]; order[k] = order[j]; order[j] = t;
        }

        const int r = row_of(best), c = col_of(best), b = box_of(best);
        for (int k = 0; k < n && solutions < limit; ++k) {
            const uint16_t bit = 1u << order[k];
            cell[best] = order[k];
            row[r] |= bit; col[c] |= bit; box[b] |= bit;
            search();
            row[r] &= ~bit; col[c] &= ~bit; box[b] &= ~bit;
        }
        cell[best] = 0;
    }
};

} // namespace

const char* difficulty_name(Difficulty d)
{
    switch (d) {
        case Difficulty::Easy:   return "Easy";
        case Difficulty::Medium: return "Medium";
        case Difficulty::Hard:   return "Hard";
        default:                 return "Expert";
    }
}

int count_solutions(const Grid& g, int limit, Grid* out)
{
    Solver s;
    if (!s.load(g)) return 0;
    s.limit = limit;
    s.out = out;
    s.search();
    return s.solutions;
}

int clue_count(const Grid& g)
{
    int n = 0;
    for (int i = 0; i < N; ++i) n += g.c[i] != 0;
    return n;
}

namespace {

// What each difficulty means, in terms of the grader's technique levels
// (see grader.h) and how many clues to leave:
//   max_level  the puzzle must be solvable using techniques up to this level
//   min_level  ...and must actually need a technique of at least this level
//   floor      stop removing clues at this many (0 = dig as far as possible)
struct LevelSpec { int max_level, min_level, floor; };

LevelSpec spec_for(Difficulty d)
{
    switch (d) {
        case Difficulty::Easy:   return {1, 1, 36};   // singles only, plenty of clues
        case Difficulty::Medium: return {2, 2, 30};   // needs locked candidates
        case Difficulty::Hard:   return {3, 3, 0};    // needs pairs/triples/X-Wing
        default:                 return {4, 4, 0};    // needs Swordfish/XY/XYZ-Wing
    }
}

void make_full_grid(Rng& rng, Grid& out)
{
    Grid empty{};
    Solver s;
    s.load(empty);
    s.limit = 1;
    s.out = &out;
    s.rng = &rng;
    s.search();
}

// One attempt: random full grid, then remove clues in symmetric pairs while
// the puzzle stays solvable with the allowed techniques. (Solvable by logic
// from a valid grid also means the solution is unique.)
void dig(const LevelSpec& spec, Rng& rng, Grid& puzzle, Grid& solution)
{
    make_full_grid(rng, solution);
    puzzle = solution;
    uint8_t order[41];                     // cells 0..40 (40 is the centre)
    for (int i = 0; i <= 40; ++i) order[i] = i;
    for (int k = 40; k > 0; --k) {
        const int j = rng.below(k + 1);
        const uint8_t t = order[k]; order[k] = order[j]; order[j] = t;
    }
    int clues = N;
    for (int k = 0; k <= 40; ++k) {
        const int a = order[k], b = 80 - a;
        const int take = (a == b) ? 1 : 2;
        if (spec.floor && clues - take < spec.floor) continue;
        const uint8_t va = puzzle.c[a], vb = puzzle.c[b];
        puzzle.c[a] = 0; puzzle.c[b] = 0;
        if (grade(puzzle, spec.max_level).solved) {
            clues -= take;
        } else {
            puzzle.c[a] = va; puzzle.c[b] = vb;    // too hard (or not unique)
        }
    }
}

} // namespace

int difficulty_level(const Grid& puzzle)
{
    const GradeResult r = grade(puzzle, 4);
    return r.solved ? r.level : 5;
}

void generate(Difficulty d, Rng& rng, Grid& puzzle, Grid& solution, void (*yield)())
{
    // Keep digging fresh grids until one needs the level's techniques. The
    // attempt cap is a safety net; the best (hardest within range) attempt
    // so far is kept if it is reached.
    const LevelSpec spec = spec_for(d);
    constexpr int kMaxAttempts = 400;
    int best_level = -1;
    for (int a = 0; a < kMaxAttempts; ++a) {
        Grid p, s;
        dig(spec, rng, p, s);
        const int level = grade(p, spec.max_level).level;
        if (level > best_level) { best_level = level; puzzle = p; solution = s; }
        if (level >= spec.min_level) return;
        if (yield) yield();
    }
}

} // namespace sudoku
