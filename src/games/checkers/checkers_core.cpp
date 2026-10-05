#include "checkers_core.h"

#include <cstring>

namespace checkers {

namespace {

inline uint64_t bit(int sq) { return 1ull << sq; }
inline bool on_board(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }

// Continue jumping from `sq` with the piece already moved there; record
// every maximal sequence.
void jumps_from(const Position& p, int s, bool king, int origin, int sq, Move& cur,
                uint64_t captured, MoveList& out)
{
    const int r = sq / 8, f = sq % 8;
    const uint64_t own = p.pieces(s) & ~bit(origin);
    const uint64_t opp = p.pieces(s ^ 1) & ~captured;
    const int dir_fwd = s == 0 ? 1 : -1;
    bool extended = false;
    for (int dr = -1; dr <= 1; dr += 2) {
        if (!king && dr != dir_fwd) continue;
        for (int df = -1; df <= 1; df += 2) {
            const int mr = r + dr, mf = f + df, lr = r + 2 * dr, lf = f + 2 * df;
            if (!on_board(lr, lf)) continue;
            const int mid = mr * 8 + mf, land = lr * 8 + lf;
            if (!(opp & bit(mid))) continue;
            if ((own | (p.pieces(s ^ 1))) & bit(land)) continue;     // must land on empty
            if (land != origin && (p.pieces(s) & bit(land))) continue;
            if (cur.n >= kMaxPath) continue;
            extended = true;
            cur.path[cur.n++] = static_cast<uint8_t>(land);
            const uint64_t cap = captured | bit(mid);
            const bool crowned = !king && (lr == (s == 0 ? 7 : 0));
            if (crowned) {                                            // the move ends on crowning
                if (out.n < kMaxMoves) { out.m[out.n] = cur; out.m[out.n].captured = cap; ++out.n; }
            } else {
                jumps_from(p, s, king, origin, land, cur, cap, out);
            }
            --cur.n;
        }
    }
    if (!extended && cur.n > 0 && out.n < kMaxMoves) {
        out.m[out.n] = cur;
        out.m[out.n].captured = captured;
        ++out.n;
    }
}

} // namespace

void Position::start()
{
    *this = Position{};
    for (int r = 0; r < 8; ++r)
        for (int f = 0; f < 8; ++f) {
            if ((r + f) & 1) continue;                               // light square
            if (r <= 2) men[0] |= bit(r * 8 + f);
            if (r >= 5) men[1] |= bit(r * 8 + f);
        }
}

int Position::cell(int sq, bool* king) const
{
    for (int s = 0; s < 2; ++s) {
        if (men[s] & bit(sq))   { if (king) *king = false; return s; }
        if (kings[s] & bit(sq)) { if (king) *king = true; return s; }
    }
    return -1;
}

void Position::apply(const Move& mv)
{
    const int s = side;
    const bool king = (kings[s] >> mv.from) & 1;
    const uint64_t from = bit(mv.from), to = bit(mv.to());
    if (king) kings[s] = (kings[s] & ~from) | to;
    else      men[s] = (men[s] & ~from) | to;
    men[s ^ 1] &= ~mv.captured;
    kings[s ^ 1] &= ~mv.captured;
    if (!king && (mv.to() / 8) == (s == 0 ? 7 : 0)) {               // crown
        men[s] &= ~to;
        kings[s] |= to;
    }
    quiet = (mv.captured || !king) ? 0 : static_cast<uint8_t>(quiet + 1);
    side ^= 1;
}

void generate(const Position& p, int s, MoveList& out)
{
    out.n = 0;
    const uint64_t mine = p.pieces(s), occ = p.occupied();
    // Jumps first: compulsory
    for (uint64_t b = mine; b; b &= b - 1) {
        const int sq = __builtin_ctzll(b);
        Move cur;
        cur.from = static_cast<uint8_t>(sq);
        jumps_from(p, s, (p.kings[s] >> sq) & 1, sq, sq, cur, 0, out);
    }
    if (out.n) return;
    const int dir_fwd = s == 0 ? 1 : -1;
    for (uint64_t b = mine; b; b &= b - 1) {
        const int sq = __builtin_ctzll(b), r = sq / 8, f = sq % 8;
        const bool king = (p.kings[s] >> sq) & 1;
        for (int dr = -1; dr <= 1; dr += 2) {
            if (!king && dr != dir_fwd) continue;
            for (int df = -1; df <= 1; df += 2) {
                const int nr = r + dr, nf = f + df;
                if (!on_board(nr, nf) || (occ & bit(nr * 8 + nf)) || out.n >= kMaxMoves) continue;
                Move m;
                m.from = static_cast<uint8_t>(sq);
                m.n = 1;
                m.path[0] = static_cast<uint8_t>(nr * 8 + nf);
                out.m[out.n++] = m;
            }
        }
    }
}

bool Game::play(int index)
{
    if (result() != -1 || plies >= kMaxPlies) return false;
    MoveList l;
    legal(l);
    if (index < 0 || index >= l.n) return false;
    last_from = l.m[index].from;
    last_to = l.m[index].to();
    pos.apply(l.m[index]);
    history[plies++] = static_cast<uint8_t>(index);
    return true;
}

int Game::result() const
{
    MoveList l;
    generate(pos, pos.side, l);
    if (l.n == 0) return pos.side ^ 1;                               // can't move: lose
    if (pos.quiet >= 80 || plies >= kMaxPlies) return 2;
    return -1;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "CHK1", 4);
    buf[4] = plies & 0xFF;
    buf[5] = plies >> 8;
    memcpy(buf + 6, history, sizeof history);
    return kSaveBytes;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "CHK1", 4) != 0) return false;
    const int n = buf[4] | (buf[5] << 8);
    if (n > kMaxPlies) return false;
    Game t;
    for (int k = 0; k < n; ++k) if (!t.play(buf[6 + k])) return false;
    *this = t;
    return true;
}

// ---- Computer ----------------------------------------------------------------------------
namespace {

constexpr int kInf = 1 << 20, kWin = 100000;

int evaluate(const Position& p, int s)
{
    int score = 0;
    for (int side = 0; side < 2; ++side) {
        int v = 0;
        for (uint64_t b = p.men[side]; b; b &= b - 1) {
            const int sq = __builtin_ctzll(b), r = sq / 8, f = sq % 8;
            const int adv = side == 0 ? r : 7 - r;                   // rows advanced
            v += 100 + 3 * adv;
            if (adv == 0) v += 6;                                    // back row guard
            if (f >= 2 && f <= 5 && r >= 2 && r <= 5) v += 3;        // centre
        }
        for (uint64_t b = p.kings[side]; b; b &= b - 1) {
            const int sq = __builtin_ctzll(b), r = sq / 8, f = sq % 8;
            v += 165;
            if (f >= 2 && f <= 5 && r >= 2 && r <= 5) v += 5;
        }
        score += side == s ? v : -v;
    }
    return score;
}

struct Search { volatile bool* stop; bool stopped = false; };

int negamax(Search& S, const Position& p, int depth, int alpha, int beta, int ply)
{
    if (S.stop && *S.stop) { S.stopped = true; return 0; }
    MoveList l;
    generate(p, p.side, l);
    if (l.n == 0) return -(kWin - ply);
    if (p.quiet >= 80) return 0;
    // Jumps are forced, so keep searching through them (no horizon there)
    if (depth <= 0 && !l.m[0].jump()) return evaluate(p, p.side);
    if (ply >= 24) return evaluate(p, p.side);
    int best = -kInf;
    for (int k = 0; k < l.n; ++k) {
        Position q = p;
        q.apply(l.m[k]);
        const int v = -negamax(S, q, depth - 1, -beta, -alpha, ply + 1);
        if (S.stopped) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

} // namespace

int best_move(const Game& g, int level, uint32_t seed, volatile bool* stop)
{
    static const int kDepth[3] = {2, 5, 7};
    const int depth = kDepth[level < 0 ? 0 : level > 2 ? 2 : level];
    MoveList l;
    g.legal(l);
    if (l.n == 0) return -1;
    if (l.n == 1) return 0;
    Search S{stop};
    int best = -kInf, pick = 0, ties = 0;
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    for (int k = 0; k < l.n; ++k) {
        Position q = g.pos;
        q.apply(l.m[k]);
        const int v = -negamax(S, q, depth - 1, -kInf, kInf, 1);
        if (S.stopped) break;
        if (v > best) { best = v; pick = k; ties = 1; }
        else if (v == best) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if (rng % ++ties == 0) pick = k;
        }
    }
    return pick;
}

} // namespace checkers

namespace checkers {

uint32_t move_key(const Move& m)
{
    uint32_t k = uint32_t(m.from) | uint32_t(m.n) << 6;
    int prev = m.from;
    for (int i = 0; i < m.n && i < kMaxPath; ++i) {
        const int to = m.path[i];
        const uint32_t dir = (to / 8 > prev / 8 ? 2u : 0u) | (to % 8 > prev % 8 ? 1u : 0u);
        k |= dir << (10 + 2 * i);
        prev = to;
    }
    return k;
}

int find_key(const Game& g, uint32_t key)
{
    MoveList l;
    g.legal(l);
    for (int k = 0; k < l.n; ++k) if (move_key(l.m[k]) == key) return k;
    return -1;
}

} // namespace checkers
