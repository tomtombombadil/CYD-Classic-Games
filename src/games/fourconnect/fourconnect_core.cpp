#include "fourconnect_core.h"

#include <cstring>

namespace fourconnect {

namespace {

constexpr int kWin = 100000;
constexpr int kInf = 1000000;
const int kOrder[kCols] = {3, 2, 4, 1, 5, 0, 6};      // centre first: better pruning

bool four(uint64_t b)
{
    // Directions: vertical 1, horizontal 7, diagonals 6 and 8
    const int dir[4] = {1, 7, 6, 8};
    for (int d : dir) {
        const uint64_t m = b & (b >> d);
        if (m & (m >> (2 * d))) return true;
    }
    return false;
}

uint64_t four_cells(uint64_t b)
{
    uint64_t out = 0;
    const int dir[4] = {1, 7, 6, 8};
    for (int d : dir) {
        const uint64_t m = b & (b >> d);
        const uint64_t starts = m & (m >> (2 * d));         // first cell of each line of 4
        for (int k = 0; k < 4; ++k) out |= starts << (k * d);
    }
    return out;
}

// All 69 lines of four on the board, as masks
uint64_t windows[69];
int      window_n = 0;

void init_windows()
{
    if (window_n) return;
    const int dc[4] = {0, 1, 1, 1}, dr[4] = {1, 0, 1, -1};
    for (int c = 0; c < kCols; ++c)
        for (int r = 0; r < kRows; ++r)
            for (int d = 0; d < 4; ++d) {
                const int ec = c + 3 * dc[d], er = r + 3 * dr[d];
                if (ec < 0 || ec >= kCols || er < 0 || er >= kRows) continue;
                uint64_t m = 0;
                for (int k = 0; k < 4; ++k) m |= bit_of(c + k * dc[d], r + k * dr[d]);
                windows[window_n++] = m;
            }
}

int popcount(uint64_t x) { return __builtin_popcountll(x); }

// Static score from `side`'s point of view: open lines with 2 or 3 of a
// side's pieces, and pieces in the centre column.
int evaluate(const Board& b, int side)
{
    const uint64_t me = b.bits[side], op = b.bits[side ^ 1];
    int s = 0;
    for (int k = 0; k < window_n; ++k) {
        const uint64_t w = windows[k];
        const int a = popcount(me & w), o = popcount(op & w);
        if (a && o) continue;
        if (a == 3) s += 50; else if (a == 2) s += 6;
        if (o == 3) s -= 50; else if (o == 2) s -= 6;
    }
    const uint64_t centre = 0x3Full << (3 * 7);
    s += 4 * (popcount(me & centre) - popcount(op & centre));
    return s;
}

struct Search {
    Board b;
    volatile bool* stop;
    bool stopped = false;
};

int negamax(Search& S, int depth, int alpha, int beta, int ply)
{
    if (S.stop && *S.stop) { S.stopped = true; return 0; }
    Board& b = S.b;
    if (b.moves == kCols * kRows) return 0;
    const int me = b.turn();
    if (depth == 0) return evaluate(b, me);
    // A move that wins at once ends the search here
    for (int c = 0; c < kCols; ++c) {
        if (!b.can_play(c)) continue;
        const uint64_t after = b.bits[me] | bit_of(c, b.height[c]);
        if (four(after)) return kWin - ply;
    }
    int best = -kInf;
    for (int c : kOrder) {
        if (!b.can_play(c)) continue;
        b.play(c);
        const int v = -negamax(S, depth - 1, -beta, -alpha, ply + 1);
        b.undo();
        if (S.stopped) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

} // namespace

bool Board::play(int col)
{
    if (!can_play(col)) return false;
    bits[turn()] |= bit_of(col, height[col]);
    ++height[col];
    history[moves++] = static_cast<uint8_t>(col);
    return true;
}

bool Board::undo()
{
    if (!moves) return false;
    const int col = history[--moves];
    --height[col];
    bits[turn()] &= ~bit_of(col, height[col]);
    return true;
}

int Board::cell(int col, int row) const
{
    const uint64_t m = bit_of(col, row);
    if (bits[0] & m) return 0;
    if (bits[1] & m) return 1;
    return -1;
}

int Board::result() const
{
    if (four(bits[0])) return 0;
    if (four(bits[1])) return 1;
    return moves == kCols * kRows ? 2 : -1;
}

uint64_t Board::winning_cells() const
{
    return four_cells(bits[0]) | four_cells(bits[1]);
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "FCN1", 4);
    buf[4] = moves;
    memcpy(buf + 5, history, sizeof history);
    return kSaveBytes;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "FCN1", 4) != 0) return false;
    Board t;
    const int n = buf[4];
    if (n > kCols * kRows) return false;
    for (int k = 0; k < n; ++k) {
        if (t.result() != -1 || !t.play(buf[5 + k])) return false;   // replay = full check
    }
    *this = t;
    return true;
}

int depth_for_level(int level)
{
    static const int d[3] = {1, 3, 8};
    return d[level < 0 ? 0 : level > 2 ? 2 : level];
}

int best_move(const Board& start, int depth, uint32_t seed, volatile bool* stop)
{
    init_windows();
    Search S{start, stop};
    int best_col = -1, best = -kInf;
    int ties = 0;
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    const int me = start.turn();
    for (int c : kOrder) {
        if (!S.b.can_play(c)) continue;
        if (best_col < 0) best_col = c;                       // any legal move, if stopped at once
        if (four(S.b.bits[me] | bit_of(c, S.b.height[c]))) return c;   // win now
        S.b.play(c);
        const int v = -negamax(S, depth - 1, -kInf, kInf, 1);
        S.b.undo();
        if (S.stopped) break;
        if (v > best) { best = v; best_col = c; ties = 1; }
        else if (v == best) {
            // Equal moves: pick one at random (reservoir sampling)
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if (rng % ++ties == 0) best_col = c;
        }
    }
    return best_col;
}

} // namespace fourconnect
