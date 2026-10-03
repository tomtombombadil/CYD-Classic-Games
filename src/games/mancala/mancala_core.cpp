#include "mancala_core.h"

#include <cstring>

namespace mancala {

int Board::seeds_on_side(int s) const
{
    int n = 0;
    for (int p = 0; p < kPits; ++p) n += pit[pit_index(s, p)];
    return n;
}

bool Board::over() const { return seeds_on_side(0) == 0 || seeds_on_side(1) == 0; }

int Board::result() const
{
    if (!over()) return -1;
    // Seeds still on a side belong to that side (play() sweeps them; a
    // loaded or hand-made board may not have been swept)
    const int a = pit[kStore[0]] + seeds_on_side(0), b = pit[kStore[1]] + seeds_on_side(1);
    return a > b ? 0 : b > a ? 1 : 2;
}

bool Board::play(int p, Sowing* how)
{
    if (over() || !can_play(p)) return false;
    const int s = side;
    const int from = pit_index(s, p);
    int seeds = pit[from];
    pit[from] = 0;
    if (how) { *how = Sowing{}; how->from = uint8_t(from); }
    int i = from;
    while (seeds > 0) {
        i = (i + 1) % 14;
        if (i == kStore[s ^ 1]) continue;             // never the other store
        ++pit[i];
        --seeds;
        if (how && how->n < sizeof how->path) how->path[how->n++] = uint8_t(i);
    }
    last_from = int8_t(from);
    ++moves;
    bool again = i == kStore[s];
    // Capture: last seed in an empty pit of ours (it now holds 1), seeds opposite
    if (!again && i != kStore[s ^ 1] && i / 7 == s && i % 7 < kPits && pit[i] == 1 && pit[opposite(i)] > 0) {
        const int o = opposite(i);
        const int took = pit[o] + 1;
        pit[kStore[s]] += uint8_t(took);
        pit[o] = 0;
        pit[i] = 0;
        if (how) { how->captured_from = int8_t(o); how->captured = uint8_t(took); }
    }
    if (how) how->again = again;
    if (over()) {                                     // sweep what's left
        for (int t = 0; t < 2; ++t)
            for (int q = 0; q < kPits; ++q) {
                pit[kStore[t]] += pit[pit_index(t, q)];
                pit[pit_index(t, q)] = 0;
            }
        if (how) how->ended = true;
        again = false;
    }
    if (!again) side ^= 1;
    return true;
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "MNC1", 4);
    memcpy(buf + 4, pit, 14);
    buf[18] = side;
    buf[19] = uint8_t(moves);
    buf[20] = uint8_t(moves >> 8);
    buf[21] = uint8_t(last_from);
    return kSaveBytes;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "MNC1", 4) != 0) return false;
    Board b;
    memcpy(b.pit, buf + 4, 14);
    int total = 0;
    for (int i = 0; i < 14; ++i) total += b.pit[i];
    if (total != 12 * kStart || buf[18] > 1) return false;
    b.side = buf[18];
    b.moves = uint16_t(buf[19] | buf[20] << 8);
    b.last_from = int8_t(buf[21]);
    if (b.last_from < -1 || b.last_from > 12) b.last_from = -1;
    *this = b;
    return true;
}

// ---- Computer ------------------------------------------------------------------------------
namespace {

constexpr int kWin = 10000;

struct Search {
    volatile bool* stop;
    long nodes = 0;
    long budget = 0;                   // 0 = none
    bool out_of_time() const { return (stop && *stop) || (budget && nodes > budget); }
};

// From the side to move: stores (and, at the end, everything) count; seeds
// on your own side are worth a little - they're yours if the game ends
int evaluate(const Board& b)
{
    const int s = b.side;
    if (b.over()) {
        const int d = (b.pit[kStore[s]] + b.seeds_on_side(s)) - (b.pit[kStore[s ^ 1]] + b.seeds_on_side(s ^ 1));
        return d > 0 ? kWin + d : d < 0 ? -kWin + d : 0;
    }
    return 4 * (b.pit[kStore[s]] - b.pit[kStore[s ^ 1]]) + (b.seeds_on_side(s) - b.seeds_on_side(s ^ 1));
}

// Move order: extra turns first, then captures, then the rest (right to left)
int ordered(const Board& b, int* out)
{
    int n = 0, extra = 0;
    int rest[kPits], nr = 0;
    for (int p = kPits - 1; p >= 0; --p) {
        if (!b.can_play(p)) continue;
        const int seeds = b.pit[pit_index(b.side, p)];
        if (seeds == kPits - p) out[extra++] = p;      // lands in the store
        else rest[nr++] = p;
    }
    n = extra;
    for (int k = 0; k < nr; ++k) out[n++] = rest[k];
    return n;
}

int negamax(const Board& b, int depth, int alpha, int beta, Search& S)
{
    ++S.nodes;
    if (depth <= 0 || b.over()) return evaluate(b);
    int moves[kPits];
    const int n = ordered(b, moves);
    int best = -kWin * 2;
    for (int k = 0; k < n; ++k) {
        Board c = b;
        c.play(moves[k]);
        // The same side moves again after an extra turn: no sign flip
        const int v = c.side == b.side ? negamax(c, depth - 1, alpha, beta, S)
                                       : -negamax(c, depth - 1, -beta, -alpha, S);
        if (v > best) best = v;
        if (best > alpha) alpha = best;
        if (alpha >= beta || S.out_of_time()) break;
    }
    return best;
}

// Values of every root move at `depth`; false if the search was cut short
bool root(const Board& b, int depth, Search& S, int* value)
{
    for (int p = 0; p < kPits; ++p) {
        value[p] = -kWin * 4;
        if (!b.can_play(p)) continue;
        Board c = b;
        c.play(p);
        value[p] = c.side == b.side ? negamax(c, depth - 1, -kWin * 2, kWin * 2, S)
                                    : -negamax(c, depth - 1, -kWin * 2, kWin * 2, S);
        if (S.out_of_time()) return false;
    }
    return true;
}

} // namespace

int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop)
{
    Search S{stop};
    int value[kPits];
    int fallback = -1;
    for (int p = 0; p < kPits; ++p) if (b.can_play(p)) { fallback = p; break; }
    if (fallback < 0) return -1;
    if (level <= 0) {
        root(b, 1, S, value);
    } else if (level == 1) {
        root(b, 5, S, value);
    } else {
        // Deepen within a node budget; keep the last depth that finished
        constexpr long kBudget = 400000;
        int done[kPits];
        root(b, 6, S, done);
        S.budget = kBudget;
        for (int d = 7; d <= 24; ++d) {
            const long before = S.nodes;
            if (!root(b, d, S, value)) break;
            memcpy(done, value, sizeof done);
            // Each depth costs a few times the last: stop when the next won't fit
            if (S.nodes + 3 * (S.nodes - before) > kBudget) break;
        }
        memcpy(value, done, sizeof done);
    }
    // The best value; ties broken by the seed
    int best = -kWin * 8, pick = fallback, ties = 0;
    uint32_t r = seed ? seed : 1;
    for (int p = 0; p < kPits; ++p) {
        if (!b.can_play(p)) continue;
        if (value[p] > best) { best = value[p]; pick = p; ties = 1; }
        else if (value[p] == best) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            if (r % uint32_t(++ties) == 0) pick = p;
        }
    }
    return pick;
}

} // namespace mancala
