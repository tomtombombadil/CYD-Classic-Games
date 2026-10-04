#include "morris_core.h"

#include <cstring>

namespace morris {

const int8_t kMills[16][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {9, 10, 11}, {12, 13, 14}, {15, 16, 17}, {18, 19, 20}, {21, 22, 23},
    {0, 9, 21}, {3, 10, 18}, {6, 11, 15}, {1, 4, 7}, {16, 19, 22}, {8, 12, 17}, {5, 13, 20}, {2, 14, 23},
};
const uint8_t kX[kPoints] = {0, 3, 6, 1, 3, 5, 2, 3, 4, 0, 1, 2, 4, 5, 6, 2, 3, 4, 1, 3, 5, 0, 3, 6};
const uint8_t kY[kPoints] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6, 6};

namespace {
// Neighbours: the points next to each other along a mill line
struct Adj {
    uint32_t m[kPoints] = {};
    Adj()
    {
        for (const auto& l : kMills) {
            m[l[0]] |= 1u << l[1]; m[l[1]] |= 1u << l[0];
            m[l[1]] |= 1u << l[2]; m[l[2]] |= 1u << l[1];
        }
    }
};
const Adj kAdj;

constexpr uint32_t kAll = (1u << kPoints) - 1;
} // namespace

uint32_t neighbours(int p) { return kAdj.m[p]; }

bool Position::in_mill(int s, int p) const
{
    for (const auto& l : kMills)
        if ((l[0] == p || l[1] == p || l[2] == p) && (men[s] >> l[0] & 1) && (men[s] >> l[1] & 1) && (men[s] >> l[2] & 1))
            return true;
    return false;
}

bool Position::makes_mill(int s, int from, int to) const
{
    uint32_t b = men[s];
    if (from >= 0) b &= ~(1u << from);
    b |= 1u << to;
    for (const auto& l : kMills)
        if ((l[0] == to || l[1] == to || l[2] == to) && (b >> l[0] & 1) && (b >> l[1] & 1) && (b >> l[2] & 1))
            return true;
    return false;
}

uint32_t Position::removable(int s) const
{
    uint32_t free_men = 0;
    for (uint32_t b = men[s]; b; b &= b - 1) {
        const int p = __builtin_ctz(b);
        if (!in_mill(s, p)) free_men |= 1u << p;
    }
    return free_men ? free_men : men[s];
}

void Position::apply(const Move& m)
{
    const int s = side;
    if (m.from < 0) --hand[s];
    else men[s] &= ~(1u << m.from);
    men[s] |= 1u << m.to;
    if (m.remove >= 0) men[s ^ 1] &= ~(1u << m.remove);
    if (hand[0] == 0 && hand[1] == 0) quiet = m.remove >= 0 ? 0 : uint8_t(quiet < 255 ? quiet + 1 : 255);
    side ^= 1;
}

void generate(const Position& p, MoveList& out)
{
    out.n = 0;
    const int s = p.side;
    const uint32_t empty = ~(p.men[0] | p.men[1]) & kAll;
    auto add = [&](int from, int to) {
        if (p.makes_mill(s, from, to)) {
            const uint32_t r = p.removable(s ^ 1);
            if (r) {
                for (uint32_t b = r; b && out.n < kMaxMoves; b &= b - 1) {
                    Move m; m.from = int8_t(from); m.to = int8_t(to); m.remove = int8_t(__builtin_ctz(b));
                    out.m[out.n++] = m;
                }
                return;
            }
        }
        if (out.n < kMaxMoves) { Move m; m.from = int8_t(from); m.to = int8_t(to); out.m[out.n++] = m; }
    };
    if (p.hand[s] > 0) {
        for (uint32_t b = empty; b; b &= b - 1) add(-1, __builtin_ctz(b));
        return;
    }
    const bool fly = p.flying(s);
    for (uint32_t b = p.men[s]; b; b &= b - 1) {
        const int from = __builtin_ctz(b);
        const uint32_t to = fly ? empty : (kAdj.m[from] & empty);
        for (uint32_t t = to; t; t &= t - 1) add(from, __builtin_ctz(t));
    }
}

bool Game::play(int code)
{
    if (result() != -1) return false;
    MoveList l;
    legal(l);
    for (int k = 0; k < l.n; ++k)
        if (l.m[k].code() == code) {
            pos.apply(l.m[k]);
            last = l.m[k];
            ++plies;
            return true;
        }
    return false;
}

int Game::result() const
{
    const int s = pos.side;
    if (pos.left(s) < 3) return s ^ 1;
    if (pos.left(s ^ 1) < 3) return s;
    MoveList l;
    generate(pos, l);
    if (l.n == 0) return s ^ 1;                        // blocked: loses
    if (pos.quiet >= 100 || plies >= 600) return 2;
    return -1;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "MOR1", 4);
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 4; ++k) buf[4 + s * 4 + k] = uint8_t(pos.men[s] >> (8 * k));
    buf[12] = pos.hand[0];
    buf[13] = pos.hand[1];
    buf[14] = pos.side;
    buf[15] = pos.quiet;
    buf[16] = uint8_t(plies);
    buf[17] = uint8_t(plies >> 8);
    buf[18] = uint8_t(last.from);
    buf[19] = uint8_t(last.to);
    buf[20] = uint8_t(last.remove);
    return kSaveBytes;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "MOR1", 4) != 0) return false;
    Game g;
    for (int s = 0; s < 2; ++s) {
        g.pos.men[s] = 0;
        for (int k = 0; k < 4; ++k) g.pos.men[s] |= uint32_t(buf[4 + s * 4 + k]) << (8 * k);
    }
    g.pos.hand[0] = buf[12];
    g.pos.hand[1] = buf[13];
    g.pos.side = buf[14];
    g.pos.quiet = buf[15];
    g.plies = uint16_t(buf[16] | buf[17] << 8);
    g.last.from = int8_t(buf[18]);
    g.last.to = int8_t(buf[19]);
    g.last.remove = int8_t(buf[20]);
    if ((g.pos.men[0] | g.pos.men[1]) & ~kAll || (g.pos.men[0] & g.pos.men[1]) || g.pos.side > 1
        || g.pos.hand[0] > kMen || g.pos.hand[1] > kMen
        || g.pos.on_board(0) + g.pos.hand[0] > kMen || g.pos.on_board(1) + g.pos.hand[1] > kMen)
        return false;
    if (g.last.to < -1 || g.last.to >= kPoints) g.last.to = -1;
    *this = g;
    return true;
}

// ---- Computer ------------------------------------------------------------------------------
namespace {

constexpr int kWin = 100000;

struct Search {
    volatile bool* stop;
    long nodes = 0, budget = 0;
    bool out() const { return (stop && *stop) || (budget && nodes > budget); }
};

// Lines with two of a side's men and the third point empty
int open_twos(const Position& p, int s)
{
    const uint32_t empty = ~(p.men[0] | p.men[1]) & kAll;
    int n = 0;
    for (const auto& l : kMills) {
        const int mine = (p.men[s] >> l[0] & 1) + (p.men[s] >> l[1] & 1) + (p.men[s] >> l[2] & 1);
        const int free_pts = (empty >> l[0] & 1) + (empty >> l[1] & 1) + (empty >> l[2] & 1);
        n += mine == 2 && free_pts == 1;
    }
    return n;
}

int mobility(const Position& p, int s)
{
    if (p.hand[s] > 0 || p.flying(s)) return 8;
    const uint32_t empty = ~(p.men[0] | p.men[1]) & kAll;
    int n = 0;
    for (uint32_t b = p.men[s]; b; b &= b - 1) n += __builtin_popcount(kAdj.m[__builtin_ctz(b)] & empty);
    return n;
}

int evaluate(const Position& p)
{
    const int s = p.side, o = s ^ 1;
    int v = 120 * (p.left(s) - p.left(o));
    v += 12 * (open_twos(p, s) - open_twos(p, o));
    v += 4 * (mobility(p, s) - mobility(p, o));
    return v;
}

int terminal(const Position& p, int depth_left)
{
    const int s = p.side;
    if (p.left(s) < 3) return -kWin - depth_left;
    if (p.left(s ^ 1) < 3) return kWin + depth_left;
    return 0;
}

int negamax(const Position& p, int depth, int alpha, int beta, Search& S)
{
    ++S.nodes;
    if (p.left(p.side) < 3 || p.left(p.side ^ 1) < 3) return terminal(p, depth);
    if (p.quiet >= 100) return 0;
    if (depth <= 0) return evaluate(p);
    MoveList l;                                         // ~0.8 KB a ply: see ai_stack
    generate(p, l);
    if (l.n == 0) return -kWin - depth;                 // blocked
    // Removals first: they matter most and cut the search well
    uint8_t order[kMaxMoves];
    int n = 0;
    for (int k = 0; k < l.n; ++k) if (l.m[k].remove >= 0) order[n++] = uint8_t(k);
    for (int k = 0; k < l.n; ++k) if (l.m[k].remove < 0) order[n++] = uint8_t(k);
    int best = -kWin * 2;
    for (int i = 0; i < n; ++i) {
        Position c = p;
        c.apply(l.m[order[i]]);
        const int v = -negamax(c, depth - 1, -beta, -alpha, S);
        if (v > best) best = v;
        if (best > alpha) alpha = best;
        if (alpha >= beta || S.out()) break;
    }
    return best;
}

} // namespace

int best_move(const Game& g, int level, uint32_t seed, volatile bool* stop)
{
    MoveList* l = new MoveList;
    g.legal(*l);
    if (l->n == 0) { delete l; return -1; }
    Search S{stop};
    int* value = new int[l->n];
    int* done = new int[l->n];
    auto search_root = [&](int depth) {
        for (int k = 0; k < l->n; ++k) {
            Position c = g.pos;
            c.apply(l->m[k]);
            value[k] = -negamax(c, depth - 1, -kWin * 2, kWin * 2, S);
            if (S.out()) return false;
        }
        return true;
    };
    if (level <= 0) {
        search_root(1);
        memcpy(done, value, sizeof(int) * l->n);
    } else if (level == 1) {
        search_root(3);
        memcpy(done, value, sizeof(int) * l->n);
    } else {
        constexpr long kBudget = 150000;            // ~0.06 s on a PC, a few seconds on the board at most
        search_root(2);
        memcpy(done, value, sizeof(int) * l->n);
        S.budget = kBudget;
        for (int d = 3; d <= 12; ++d) {
            const long before = S.nodes;
            if (!search_root(d)) break;
            memcpy(done, value, sizeof(int) * l->n);
            if (S.nodes + 4 * (S.nodes - before) > kBudget) break;
        }
    }
    int best = -kWin * 4, pick = 0, ties = 0;
    uint32_t r = seed ? seed : 1;
    for (int k = 0; k < l->n; ++k) {
        if (done[k] > best) { best = done[k]; pick = k; ties = 1; }
        else if (done[k] == best) {
            r ^= r << 13; r ^= r >> 17; r ^= r << 5;
            if (r % uint32_t(++ties) == 0) pick = k;
        }
    }
    const int code = l->m[pick].code();
    delete[] value;
    delete[] done;
    delete l;
    return code;
}

} // namespace morris
