#include "solitaire_solve.h"

#include <cstring>
#include <new>

namespace solitaire {

namespace {

constexpr int kSeenBits = 13;                       // 8192 hashes, 32 KB
constexpr int kMaxDepth = 180;                      // under the undo log's 200

struct Search {
    Game*         g;
    uint32_t*     seen;
    uint32_t      nodes, limit;
    volatile bool* stop;
    bool          gave_up;
};

uint32_t hash_of(const Game& g)
{
    uint32_t h = 2166136261u;
    for (int p = 0; p < kPiles; ++p) {
        const Stack& s = g.pile[p];
        h = (h ^ s.n) * 16777619u;
        // foundations: only their size matters
        if (p >= Found0 && p < Tab0) continue;
        for (int i = 0; i < s.n; ++i) h = (h ^ s.c[i]) * 16777619u;
    }
    return h | 1;                                    // 0 = empty slot
}

bool seen_before(Search& S, uint32_t h)
{
    uint32_t& slot = S.seen[(h ^ (h >> 15)) & ((1u << kSeenBits) - 1)];
    if (slot == h) return true;
    slot = h;                                         // lossy: replace whatever was there
    return false;
}

// A foundation move nothing could ever want back: low cards, or a card
// whose opposite-color cards one lower are both already up
bool safe_up(const Game& g, uint8_t c)
{
    const int r = rank(c);
    if (r <= 2) return true;
    for (int f = 0; f < 4; ++f) {
        const int s = kFoundSuit[f];
        const bool other = (s == 1 || s == 2) != red(c);
        if (other && g.pile[Found0 + f].n < r - 1) return false;
    }
    return true;
}

bool dfs(Search& S, int depth)
{
    Game& g = *S.g;
    if (g.won()) return true;
    if (S.stop && *S.stop) { S.gave_up = true; return false; }
    if (++S.nodes > S.limit) { S.gave_up = true; return false; }
    if (depth >= kMaxDepth) return false;
    if (seen_before(S, hash_of(g))) return false;

    // 1) a safe foundation move: no branching
    for (int p = Waste; p < kPiles; ++p) {
        if (p >= Found0 && p < Tab0) continue;
        const Stack& s = g.pile[p];
        if (!s.n) continue;
        const uint8_t c = s.top();
        if (Game::face_up(c) && g.can_move(p, s.n - 1, found_for(c)) && safe_up(g, c)) {
            g.move(p, s.n - 1, found_for(c));
            const bool ok = dfs(S, depth + 1);
            g.undo();
            return ok;
        }
    }

    // Candidate moves, best first
    struct M { uint8_t from, idx, to; };
    M ms[40];
    int n = 0;
    auto add = [&](int f, int i, int t) { if (n < 40) ms[n++] = M{uint8_t(f), uint8_t(i), uint8_t(t)}; };
    // foundation moves
    for (int p = Waste; p < kPiles; ++p) {
        if ((p >= Found0 && p < Tab0) || !g.pile[p].n) continue;
        const int i = g.pile[p].n - 1;
        if (g.can_move(p, i, found_for(g.pile[p].c[i]))) add(p, i, found_for(g.pile[p].c[i]));
    }
    // column runs: the whole face-up run when it turns up a card (or empties
    // a column while a King waits for one), or part of a run when that frees
    // the card under it for its foundation
    bool king_waiting = g.pile[Waste].n && rank(g.pile[Waste].top()) == 13;
    for (int t = 0; t < 7 && !king_waiting; ++t) {
        const int fu = g.first_up(Tab0 + t);
        king_waiting = fu > 0 && fu < g.pile[Tab0 + t].n && rank(g.pile[Tab0 + t].c[fu]) == 13;
    }
    for (int t = 0; t < 7; ++t) {
        const int p = Tab0 + t;
        const Stack& s = g.pile[p];
        const int fu = g.first_up(p);
        if (fu >= s.n) continue;
        for (int d = 0; d < 7; ++d) {
            if (d == t) continue;
            const int to = Tab0 + d;
            if (g.can_move(p, fu, to) && (fu > 0 || (king_waiting && g.pile[to].n)))
                add(p, fu, to);
            for (int j = fu + 1; j < s.n; ++j)
                if (g.can_move(p, j, to) && g.can_move(p, j - 1, found_for(s.c[j - 1])) && g.pile[p].n - j > 0)
                    add(p, j, to);
        }
    }
    // waste to a column
    if (g.pile[Waste].n)
        for (int d = 0; d < 7; ++d)
            if (g.can_move(Waste, g.pile[Waste].n - 1, Tab0 + d)) add(Waste, g.pile[Waste].n - 1, Tab0 + d);

    for (int k = 0; k < n; ++k) {
        if (!g.move(ms[k].from, ms[k].idx, ms[k].to)) continue;
        const bool ok = dfs(S, depth + 1);
        g.undo();
        if (ok) return true;
        if (S.gave_up) return false;
    }
    // turn the stock
    if (g.can_draw()) {
        g.draw_stock();
        const bool ok = dfs(S, depth + 1);
        g.undo();
        if (ok) return true;
    }
    return false;
}

} // namespace

Verdict check_deal(Game& work, uint32_t seed, int draw, Scoring scoring, uint32_t node_limit,
                   volatile bool* stop, uint32_t* nodes)
{
    uint32_t* seen = new (std::nothrow) uint32_t[1u << kSeenBits];
    if (!seen) return Verdict::Unknown;
    memset(seen, 0, sizeof(uint32_t) << kSeenBits);
    work.deal(seed, draw, scoring);
    Search S{&work, seen, 0, node_limit, stop, false};
    const bool won = dfs(S, 0);
    delete[] seen;
    if (nodes) *nodes = S.nodes;
    return won ? Verdict::Win : S.gave_up ? Verdict::Unknown : Verdict::NoWin;
}

uint32_t find_winnable(uint32_t seed, int draw, Scoring scoring, uint32_t node_limit,
                       volatile bool* stop, int* tries)
{
    Game* work = new (std::nothrow) Game();
    if (!work) { if (tries) *tries = 0; return seed; }
    int t = 0;
    uint32_t s = seed;
    for (;;) {
        if (stop && *stop) { t = 0; break; }
        ++t;
        if (check_deal(*work, s, draw, scoring, node_limit, stop) == Verdict::Win) break;
        s = s * 1664525u + 1013904223u;              // next candidate
        if (!s) s = 1;
    }
    delete work;
    if (tries) *tries = t;
    return s;
}

} // namespace solitaire
