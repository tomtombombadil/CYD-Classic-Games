#include "sudoku_grader.h"

#include <cstring>

namespace sudoku {

namespace {

// ---- Board geometry (built once) ---------------------------------------------
struct Geometry {
    uint8_t unit[27][9];      // 0-8 rows, 9-17 cols, 18-26 boxes
    uint8_t peers[81][20];
    uint8_t cell_unit[81][3]; // row, col, box unit of each cell
    bool    sees[81][81];
    Geometry()
    {
        for (int r = 0; r < 9; ++r)
            for (int c = 0; c < 9; ++c) {
                const int i = r * 9 + c, b = (r / 3) * 3 + c / 3;
                unit[r][c] = i;
                unit[9 + c][r] = i;
                unit[18 + b][(r % 3) * 3 + c % 3] = i;
                cell_unit[i][0] = r;
                cell_unit[i][1] = 9 + c;
                cell_unit[i][2] = 18 + b;
            }
        for (int i = 0; i < 81; ++i) {
            int n = 0;
            for (int j = 0; j < 81; ++j) {
                sees[i][j] = (i != j) && same_unit(i, j);
                if (sees[i][j]) peers[i][n++] = j;
            }
        }
    }
};
const Geometry& geo() { static const Geometry g; return g; }

inline int popcount(uint16_t v) { return __builtin_popcount(v); }
inline int lowest_digit(uint16_t v) { return __builtin_ctz(v); }

struct State {
    uint8_t  value[81];
    uint16_t cand[81];        // bit d set = d still possible (0 for solved cells)
    int      filled = 0;
    bool     broken = false;  // contradiction (puzzle has no solution)

    void place(int i, int d)
    {
        const Geometry& G = geo();
        value[i] = d;
        cand[i] = 0;
        ++filled;
        const uint16_t bit = 1u << d;
        for (int k = 0; k < 20; ++k) {
            const int p = G.peers[i][k];
            if (cand[p] & bit) {
                cand[p] &= ~bit;
                if (!value[p] && !cand[p]) broken = true;
            }
        }
    }

    // Remove digit bits from a cell; true if anything changed.
    bool eliminate(int i, uint16_t bits)
    {
        if (value[i] || !(cand[i] & bits)) return false;
        cand[i] &= ~bits;
        if (!cand[i]) broken = true;
        return true;
    }

    bool init(const Grid& g)
    {
        for (int i = 0; i < 81; ++i) { value[i] = 0; cand[i] = 0x3FE; }
        filled = 0;
        broken = false;
        for (int i = 0; i < 81; ++i) {
            const int d = g.c[i];
            if (!d) continue;
            if (!(cand[i] & (1u << d))) return false;    // clashing givens
            place(i, d);
        }
        return !broken;
    }
};

// ---- Level 1: singles ------------------------------------------------------------
bool naked_single(State& s)
{
    for (int i = 0; i < 81; ++i)
        if (!s.value[i] && popcount(s.cand[i]) == 1) { s.place(i, lowest_digit(s.cand[i])); return true; }
    return false;
}

bool hidden_single(State& s)
{
    const Geometry& G = geo();
    for (int u = 0; u < 27; ++u) {
        for (int d = 1; d <= 9; ++d) {
            const uint16_t bit = 1u << d;
            int where = -1, n = 0;
            for (int k = 0; k < 9 && n < 2; ++k) {
                const int i = G.unit[u][k];
                if (s.value[i] == d) { n = 2; break; }       // already placed
                if (s.cand[i] & bit) { where = i; ++n; }
            }
            if (n == 1) { s.place(where, d); return true; }
        }
    }
    return false;
}

// ---- Level 2: locked candidates -----------------------------------------------
// Pointing: in a box, digit confined to one row/col -> remove from rest of it.
// Claiming: in a row/col, digit confined to one box -> remove from rest of box.
bool locked_candidates(State& s)
{
    const Geometry& G = geo();
    bool changed = false;
    for (int u = 0; u < 27; ++u) {
        for (int d = 1; d <= 9; ++d) {
            const uint16_t bit = 1u << d;
            int first = -1;
            bool same[3] = {true, true, true};      // same row / col / box as first
            int n = 0;
            for (int k = 0; k < 9; ++k) {
                const int i = G.unit[u][k];
                if (!(s.cand[i] & bit)) continue;
                if (first < 0) first = i;
                else for (int t = 0; t < 3; ++t)
                    if (G.cell_unit[i][t] != G.cell_unit[first][t]) same[t] = false;
                ++n;
            }
            if (n < 2) continue;
            for (int t = 0; t < 3; ++t) {
                const int other = G.cell_unit[first][t];
                if (!same[t] || other == u) continue;
                for (int k = 0; k < 9; ++k) {
                    const int i = G.unit[other][k];
                    bool inside = false;
                    for (int t2 = 0; t2 < 3; ++t2) if (G.cell_unit[i][t2] == u) inside = true;
                    if (!inside && s.eliminate(i, bit)) changed = true;
                }
            }
            if (changed) return true;
        }
    }
    return false;
}

// ---- Level 3: subsets and X-Wing -------------------------------------------------
// Naked subset: k cells in a unit whose candidates together are k digits.
bool naked_subset(State& s, int k)
{
    const Geometry& G = geo();
    for (int u = 0; u < 27; ++u) {
        int cells[9], n = 0;
        for (int t = 0; t < 9; ++t) {
            const int i = G.unit[u][t];
            const int c = popcount(s.cand[i]);
            if (!s.value[i] && c >= 2 && c <= k) cells[n++] = i;
        }
        if (n < k) continue;
        int idx[3];
        for (idx[0] = 0; idx[0] < n; ++idx[0])
        for (idx[1] = idx[0] + 1; idx[1] < n; ++idx[1])
        for (idx[2] = (k == 3 ? idx[1] + 1 : n); idx[2] <= (k == 3 ? n - 1 : n); ++idx[2]) {
            uint16_t uni = s.cand[cells[idx[0]]] | s.cand[cells[idx[1]]];
            if (k == 3) uni |= s.cand[cells[idx[2]]];
            if (popcount(uni) != k) continue;
            bool changed = false;
            for (int t = 0; t < 9; ++t) {
                const int i = G.unit[u][t];
                bool member = (i == cells[idx[0]] || i == cells[idx[1]] || (k == 3 && i == cells[idx[2]]));
                if (!member && s.eliminate(i, uni)) changed = true;
            }
            if (changed) return true;
        }
    }
    return false;
}

// Hidden subset: k digits that, in a unit, only fit in the same k cells.
bool hidden_subset(State& s, int k)
{
    const Geometry& G = geo();
    for (int u = 0; u < 27; ++u) {
        uint16_t pos[10] = {};                  // bit t = unit cell t holds digit
        int digits[9], n = 0;
        for (int d = 1; d <= 9; ++d) {
            for (int t = 0; t < 9; ++t)
                if (s.cand[G.unit[u][t]] & (1u << d)) pos[d] |= 1u << t;
            const int c = popcount(pos[d]);
            if (c >= 2 && c <= k) digits[n++] = d;
        }
        if (n < k) continue;
        int idx[3];
        for (idx[0] = 0; idx[0] < n; ++idx[0])
        for (idx[1] = idx[0] + 1; idx[1] < n; ++idx[1])
        for (idx[2] = (k == 3 ? idx[1] + 1 : n); idx[2] <= (k == 3 ? n - 1 : n); ++idx[2]) {
            uint16_t cells = pos[digits[idx[0]]] | pos[digits[idx[1]]];
            uint16_t keep = (1u << digits[idx[0]]) | (1u << digits[idx[1]]);
            if (k == 3) { cells |= pos[digits[idx[2]]]; keep |= 1u << digits[idx[2]]; }
            if (popcount(cells) != k) continue;
            bool changed = false;
            for (int t = 0; t < 9; ++t)
                if ((cells >> t) & 1)
                    if (s.eliminate(G.unit[u][t], static_cast<uint16_t>(0x3FE & ~keep))) changed = true;
            if (changed) return true;
        }
    }
    return false;
}

// Fish of size k (2 = X-Wing, 3 = Swordfish) for one digit, rows as base
// (by_rows) or columns as base.
bool fish(State& s, int k)
{
    for (int d = 1; d <= 9; ++d) {
        const uint16_t bit = 1u << d;
        for (int by_rows = 0; by_rows < 2; ++by_rows) {
            uint16_t mask[9];
            int lines[9], n = 0;
            for (int a = 0; a < 9; ++a) {
                mask[a] = 0;
                for (int b = 0; b < 9; ++b) {
                    const int i = by_rows ? a * 9 + b : b * 9 + a;
                    if (s.cand[i] & bit) mask[a] |= 1u << b;
                }
                const int c = popcount(mask[a]);
                if (c >= 2 && c <= k) lines[n++] = a;
            }
            if (n < k) continue;
            int idx[3];
            for (idx[0] = 0; idx[0] < n; ++idx[0])
            for (idx[1] = idx[0] + 1; idx[1] < n; ++idx[1])
            for (idx[2] = (k == 3 ? idx[1] + 1 : n); idx[2] <= (k == 3 ? n - 1 : n); ++idx[2]) {
                uint16_t cover = mask[lines[idx[0]]] | mask[lines[idx[1]]];
                if (k == 3) cover |= mask[lines[idx[2]]];
                if (popcount(cover) != k) continue;
                bool changed = false;
                for (int a = 0; a < 9; ++a) {
                    if (a == lines[idx[0]] || a == lines[idx[1]] || (k == 3 && a == lines[idx[2]])) continue;
                    for (int b = 0; b < 9; ++b) {
                        if (!((cover >> b) & 1)) continue;
                        const int i = by_rows ? a * 9 + b : b * 9 + a;
                        if (s.eliminate(i, bit)) changed = true;
                    }
                }
                if (changed) return true;
            }
        }
    }
    return false;
}

// ---- Level 4: wings ------------------------------------------------------------
// XY-Wing: pivot {a,b}, pincers {a,c} and {b,c} both seeing the pivot:
// c goes from every cell seeing both pincers.
bool xy_wing(State& s)
{
    const Geometry& G = geo();
    for (int p = 0; p < 81; ++p) {
        if (popcount(s.cand[p]) != 2) continue;
        for (int x = 0; x < 20; ++x) {
            const int a = G.peers[p][x];
            if (popcount(s.cand[a]) != 2) continue;
            const uint16_t shared_pa = s.cand[p] & s.cand[a];
            if (popcount(shared_pa) != 1) continue;
            for (int y = x + 1; y < 20; ++y) {
                const int b = G.peers[p][y];
                if (popcount(s.cand[b]) != 2) continue;
                const uint16_t shared_pb = s.cand[p] & s.cand[b];
                if (popcount(shared_pb) != 1 || shared_pb == shared_pa) continue;
                const uint16_t c = (s.cand[a] & s.cand[b]) & ~s.cand[p];
                if (popcount(c) != 1) continue;
                if ((s.cand[a] | s.cand[b] | s.cand[p]) != (s.cand[p] | c)) continue;
                bool changed = false;
                for (int i = 0; i < 81; ++i)
                    if (i != a && i != b && i != p && G.sees[i][a] && G.sees[i][b] && s.eliminate(i, c))
                        changed = true;
                if (changed) return true;
            }
        }
    }
    return false;
}

// XYZ-Wing: pivot {a,b,c}, pincers {a,c} and {b,c} seeing the pivot:
// c goes from every cell seeing all three.
bool xyz_wing(State& s)
{
    const Geometry& G = geo();
    for (int p = 0; p < 81; ++p) {
        if (popcount(s.cand[p]) != 3) continue;
        for (int x = 0; x < 20; ++x) {
            const int a = G.peers[p][x];
            if (popcount(s.cand[a]) != 2 || (s.cand[a] & ~s.cand[p])) continue;
            for (int y = x + 1; y < 20; ++y) {
                const int b = G.peers[p][y];
                if (popcount(s.cand[b]) != 2 || (s.cand[b] & ~s.cand[p]) || s.cand[b] == s.cand[a]) continue;
                const uint16_t c = s.cand[a] & s.cand[b];
                if (popcount(c) != 1) continue;
                bool changed = false;
                for (int i = 0; i < 81; ++i)
                    if (i != a && i != b && i != p && G.sees[i][a] && G.sees[i][b] && G.sees[i][p]
                        && s.eliminate(i, c))
                        changed = true;
                if (changed) return true;
            }
        }
    }
    return false;
}

} // namespace

GradeResult grade(const Grid& g, int max_level)
{
    GradeResult r;
    State s;
    if (!s.init(g)) { r.grid = g; return r; }

    while (s.filled < 81 && !s.broken) {
        int used = 0;
        if (naked_single(s) || hidden_single(s))                         used = 1;
        else if (max_level >= 2 && locked_candidates(s))                 used = 2;
        else if (max_level >= 3 && (naked_subset(s, 2) || hidden_subset(s, 2) || fish(s, 2) ||
                                    naked_subset(s, 3) || hidden_subset(s, 3)))
                                                                         used = 3;
        else if (max_level >= 4 && (fish(s, 3) || xy_wing(s) || xyz_wing(s)))
                                                                         used = 4;
        if (!used) break;                                // stuck at this level
        if (used > r.level) r.level = used;
        ++r.steps;
    }
    r.solved = (s.filled == 81 && !s.broken);
    for (int i = 0; i < 81; ++i) r.grid.c[i] = s.value[i];
    return r;
}

} // namespace sudoku
