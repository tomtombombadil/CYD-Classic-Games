#include "reversi_core.h"

#include <cstring>

namespace reversi {

namespace {

constexpr uint64_t kNotA = 0xFEFEFEFEFEFEFEFEull;   // no file A (col 0)
constexpr uint64_t kNotH = 0x7F7F7F7F7F7F7F7Full;   // no file H (col 7)

// Shift the whole board one step in direction d (0..7), dropping wraps
uint64_t shift(uint64_t b, int d)
{
    switch (d) {
        case 0: return (b << 1) & kNotA;    // east (col + 1)
        case 1: return (b >> 1) & kNotH;    // west
        case 2: return b << 8;              // south (row + 1)
        case 3: return b >> 8;              // north
        case 4: return (b << 9) & kNotA;    // south-east
        case 5: return (b << 7) & kNotH;    // south-west
        case 6: return (b >> 7) & kNotA;    // north-east
        case 7: return (b >> 9) & kNotH;    // north-west
    }
    return 0;
}

uint64_t legal_for(uint64_t me, uint64_t op)
{
    const uint64_t empty = ~(me | op);
    uint64_t moves = 0;
    for (int d = 0; d < 8; ++d) {
        uint64_t x = shift(me, d) & op;
        for (int k = 0; k < 5; ++k) x |= shift(x, d) & op;
        moves |= shift(x, d) & empty;
    }
    return moves;
}

int popcount(uint64_t x) { return __builtin_popcountll(x); }

// Square weights: corners good, squares next to empty corners bad
const int8_t kWeight[64] = {
    100, -20, 10,  5,  5, 10, -20, 100,
    -20, -50, -2, -2, -2, -2, -50, -20,
     10,  -2,  1,  1,  1,  1,  -2,  10,
      5,  -2,  1,  0,  0,  1,  -2,   5,
      5,  -2,  1,  0,  0,  1,  -2,   5,
     10,  -2,  1,  1,  1,  1,  -2,  10,
    -20, -50, -2, -2, -2, -2, -50, -20,
    100, -20, 10,  5,  5, 10, -20, 100,
};

int evaluate(uint64_t me, uint64_t op)
{
    int s = 0;
    for (uint64_t b = me; b; b &= b - 1) s += kWeight[__builtin_ctzll(b)];
    for (uint64_t b = op; b; b &= b - 1) s -= kWeight[__builtin_ctzll(b)];
    s += 8 * (popcount(legal_for(me, op)) - popcount(legal_for(op, me)));
    return s;
}

struct Search {
    volatile bool* stop;
    bool stopped = false;
    bool exact;                      // few squares left: search to the end of the game
};

constexpr int kInf = 1 << 20;
constexpr int kWinBase = 10000;

int negamax(Search& S, uint64_t me, uint64_t op, int depth, int alpha, int beta, bool passed)
{
    if (S.stop && *S.stop) { S.stopped = true; return 0; }
    uint64_t moves = legal_for(me, op);
    if (!moves) {
        if (passed || !legal_for(op, me)) {          // game over
            const int d = popcount(me) - popcount(op);
            return d > 0 ? kWinBase + d : d < 0 ? -kWinBase + d : 0;
        }
        return -negamax(S, op, me, depth, -beta, -alpha, true);
    }
    if (depth <= 0 && !S.exact) return evaluate(me, op);
    int best = -kInf;
    // Corners first: better cut-offs
    const uint64_t corners = 0x8100000000000081ull;
    for (int pass = 0; pass < 2; ++pass) {
        uint64_t set = pass == 0 ? (moves & corners) : (moves & ~corners);
        for (; set; set &= set - 1) {
            const int sq = __builtin_ctzll(set);
            const uint64_t f = flips(me, op, sq);
            const int v = -negamax(S, op ^ f, me | f | (1ull << sq), depth - 1, -beta, -alpha, false);
            if (S.stopped) return 0;
            if (v > best) best = v;
            if (v > alpha) alpha = v;
            if (alpha >= beta) return best;
        }
    }
    return best;
}

} // namespace

uint64_t flips(uint64_t me, uint64_t op, int sq)
{
    const uint64_t start = 1ull << sq;
    uint64_t total = 0;
    for (int d = 0; d < 8; ++d) {
        uint64_t line = 0, x = shift(start, d);
        while (x & op) { line |= x; x = shift(x, d); }
        if (x & me) total |= line;
    }
    return total;
}

Board::Board()
{
    disc[1] = (1ull << 27) | (1ull << 36);           // d4, e5 white
    disc[0] = (1ull << 28) | (1ull << 35);           // e4, d5 black
}

uint64_t Board::legal() const { return legal_for(disc[side], disc[side ^ 1]); }

bool Board::play(int sq)
{
    if (!can_play(sq) || plies >= sizeof history - 1) return false;
    const uint64_t f = flips(disc[side], disc[side ^ 1], sq);
    disc[side] |= f | (1ull << sq);
    disc[side ^ 1] &= ~f;
    history[plies++] = static_cast<uint8_t>(sq);
    side ^= 1;
    if (!legal() && !over()) {                        // no move: pass
        history[plies++] = kPass;
        side ^= 1;
    }
    return true;
}

bool Board::over() const
{
    return !legal_for(disc[0], disc[1]) && !legal_for(disc[1], disc[0]);
}

int Board::cell(int sq) const
{
    const uint64_t b = 1ull << sq;
    return (disc[0] & b) ? 0 : (disc[1] & b) ? 1 : -1;
}

int Board::result() const
{
    if (!over()) return -1;
    const int b = count(0), w = count(1);
    return b > w ? 0 : w > b ? 1 : 2;
}

int Board::moves() const
{
    int n = 0;
    for (int k = 0; k < plies; ++k) n += history[k] != kPass;
    return n;
}

int Board::last_move() const
{
    for (int k = plies - 1; k >= 0; --k) if (history[k] != kPass) return history[k];
    return -1;
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "RVS1", 4);
    buf[4] = plies;
    memcpy(buf + 5, history, sizeof history);
    return kSaveBytes;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "RVS1", 4) != 0 || buf[4] >= 80) return false;
    Board t;
    for (int k = 0; k < buf[4]; ++k) {
        const int sq = buf[5 + k];
        if (sq == kPass) {                           // play() already passed here
            if (t.plies <= k || t.history[k] != kPass) return false;
            continue;
        }
        if (t.plies != k || !t.play(sq)) return false;
    }
    if (t.plies != buf[4]) return false;
    *this = t;
    return true;
}

int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop)
{
    static const int kDepth[3] = {2, 4, 6};
    const int depth = kDepth[level < 0 ? 0 : level > 2 ? 2 : level];
    // Hard plays the last 10 squares perfectly (decided at the root, so a
    // midgame search never runs on into a full endgame solve)
    const int empties = 64 - __builtin_popcountll(b.disc[0] | b.disc[1]);
    Search S{stop, false, level >= 2 && empties <= 10};
    const uint64_t me = b.disc[b.side], op = b.disc[b.side ^ 1];
    uint64_t moves = legal_for(me, op);
    int best = -kInf, pick = moves ? __builtin_ctzll(moves) : -1, ties = 0;
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    for (; moves; moves &= moves - 1) {
        const int sq = __builtin_ctzll(moves);
        const uint64_t f = flips(me, op, sq);
        const int v = -negamax(S, op ^ f, me | f | (1ull << sq), depth - 1, -kInf, kInf, false);
        if (S.stopped) break;
        if (v > best) { best = v; pick = sq; ties = 1; }
        else if (v == best) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if (rng % ++ties == 0) pick = sq;
        }
    }
    return pick;
}

} // namespace reversi
