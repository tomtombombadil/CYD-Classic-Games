// Gomoku rules and computer player. See gomoku_core.h.
#include "gomoku_core.h"

#include <cstring>

namespace gomoku {

namespace {

const int kDr[4] = {0, 1, 1, 1}, kDc[4] = {1, 0, 1, -1};
bool on(int r, int c) { return r >= 0 && r < kN && c >= 0 && c < kN; }

// Stones of `v` in a row through p in direction d (p included)
int run(const uint8_t* s, int p, int d, uint8_t v, int* first = nullptr, int* last = nullptr)
{
    const int r = p / kN, c = p % kN;
    int n = 1, a = p, b = p;
    for (int k = 1; on(r + k * kDr[d], c + k * kDc[d]) && s[(r + k * kDr[d]) * kN + c + k * kDc[d]] == v; ++k) {
        ++n;
        b = (r + k * kDr[d]) * kN + c + k * kDc[d];
    }
    for (int k = 1; on(r - k * kDr[d], c - k * kDc[d]) && s[(r - k * kDr[d]) * kN + c - k * kDc[d]] == v; ++k) {
        ++n;
        a = (r - k * kDr[d]) * kN + c - k * kDc[d];
    }
    if (first) *first = a;
    if (last) *last = b;
    return n;
}

} // namespace

bool Board::play(int p)
{
    if (!can_play(p)) return false;
    const uint8_t v = uint8_t(turn() + 1);
    stone[p] = v;
    for (int d = 0; d < 4; ++d)
        if (run(stone, p, d, v) >= 5) winner = int8_t(v - 1);
    last = int16_t(p);
    ++moves;
    return true;
}

int Board::result() const
{
    if (winner >= 0) return winner;
    return moves >= kPoints ? 2 : -1;
}

bool Board::winning_line(int* a, int* b) const
{
    if (winner < 0 || last < 0) return false;
    for (int d = 0; d < 4; ++d)
        if (run(stone, last, d, stone[last], a, b) >= 5) return true;
    return false;
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "GMK1", 4);
    p += 4;
    for (int k = 0; k < 57; ++k) {
        uint8_t v = 0;
        for (int j = 0; j < 4; ++j) {
            const int i = k * 4 + j;
            if (i < kPoints) v |= uint8_t(stone[i] << (2 * j));
        }
        *p++ = v;
    }
    *p++ = uint8_t(moves);
    *p++ = uint8_t(moves >> 8);
    *p++ = uint8_t(last);
    *p++ = uint8_t(uint16_t(last) >> 8);
    return size_t(p - buf);
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "GMK1", 4) != 0) return false;
    Board b;
    const uint8_t* p = buf + 4;
    int blacks = 0, whites = 0;
    for (int k = 0; k < 57; ++k, ++p)
        for (int j = 0; j < 4; ++j) {
            const int i = k * 4 + j;
            const uint8_t v = uint8_t((*p >> (2 * j)) & 3);
            if (i >= kPoints) { if (v) return false; continue; }
            if (v == 3) return false;
            b.stone[i] = v;
            blacks += v == 1;
            whites += v == 2;
        }
    b.moves = uint16_t(p[0] | p[1] << 8);
    b.last = int16_t(p[2] | p[3] << 8);
    if (b.moves != blacks + whites || blacks - whites < 0 || blacks - whites > 1) return false;
    if ((b.last < 0) != (b.moves == 0) || b.last < -1 || b.last >= kPoints || (b.last >= 0 && !b.stone[b.last])) return false;
    // Only the last move can have made a five
    if (b.last >= 0)
        for (int d = 0; d < 4; ++d)
            if (run(b.stone, b.last, d, b.stone[b.last]) >= 5) b.winner = int8_t(b.stone[b.last] - 1);
    *this = b;
    return true;
}

// ---- Computer ---------------------------------------------------------------------------------

namespace {

// Five-point windows: one with stones of only one colour is worth more the
// more of them it has; a window with both colours is dead.
const int32_t kWindow[6] = {0, 1, 12, 120, 1500, 1000000};

// What a stone of `v` at p does to every window through it: dv = what v
// gains, dother = what the other colour loses (its windows blocked), and
// how many "fours" (a window one stone short of five) each side gains / loses
struct Delta { int32_t dv = 0, dother = 0; int fours_v = 0, fours_other = 0; bool five = false; };

Delta delta_at(const uint8_t* s, int p, uint8_t v)
{
    Delta d;
    const int r = p / kN, c = p % kN;
    for (int dir = 0; dir < 4; ++dir)
        for (int start = -4; start <= 0; ++start) {
            const int r0 = r + start * kDr[dir], c0 = c + start * kDc[dir];
            const int r4 = r0 + 4 * kDr[dir], c4 = c0 + 4 * kDc[dir];
            if (!on(r0, c0) || !on(r4, c4)) continue;
            int mine = 0, other = 0;
            for (int k = 0; k < 5; ++k) {
                const uint8_t x = s[(r0 + k * kDr[dir]) * kN + c0 + k * kDc[dir]];
                if (x == v) ++mine;
                else if (x) ++other;
            }
            if (!other) {
                d.dv += kWindow[mine + 1] - kWindow[mine];
                if (mine == 3) ++d.fours_v;
                if (mine == 4) { --d.fours_v; d.five = true; }
            }
            if (!mine && other) {
                d.dother -= kWindow[other];
                if (other == 4) --d.fours_other;
            }
        }
    return d;
}

// The search's board: stones, each side's window total and its fours
struct Pos {
    uint8_t  s[kPoints];
    int32_t  score[2];
    int      fours[2];
    int      moves;
    int      to_move;                 // 0 / 1
};

void pos_from(const Board& b, Pos& q)
{
    memset(&q, 0, sizeof q);
    q.moves = b.moves;
    q.to_move = b.turn();
    // Build the totals by placing the stones one by one on an empty board
    for (int p = 0; p < kPoints; ++p) {
        if (!b.stone[p]) continue;
        const uint8_t v = b.stone[p];
        const Delta d = delta_at(q.s, p, v);
        q.score[v - 1] += d.dv;
        q.score[2 - v] += d.dother;
        q.fours[v - 1] += d.fours_v;
        q.fours[2 - v] += d.fours_other;
        q.s[p] = v;
    }
}

struct Cand { int16_t p; int32_t score; };

// Empty points within two of a stone, best first (attack + defence)
int candidates(const Pos& q, Cand* out, int cap, int defence = 9)
{
    const uint8_t me = uint8_t(q.to_move + 1), them = uint8_t(me ^ 3);
    int n = 0;
    if (!q.moves) { out[0] = {int16_t(kPoints / 2), 1}; return 1; }
    for (int p = 0; p < kPoints; ++p) {
        if (q.s[p]) continue;
        const int r = p / kN, c = p % kN;
        bool near = false;
        for (int dr = -2; dr <= 2 && !near; ++dr)
            for (int dc = -2; dc <= 2 && !near; ++dc)
                if (on(r + dr, c + dc) && q.s[(r + dr) * kN + c + dc]) near = true;
        if (!near) continue;
        const int32_t sc = delta_at(q.s, p, me).dv + delta_at(q.s, p, them).dv * defence / 10;
        if (n == cap && sc <= out[cap - 1].score) continue;
        int i = n < cap ? n++ : cap - 1;
        while (i > 0 && out[i - 1].score < sc) { out[i] = out[i - 1]; --i; }
        out[i] = {int16_t(p), sc};
    }
    return n;
}

struct Undo { int32_t score[2]; int fours[2]; };

// Returns true when the stone made five
bool place(Pos& q, int p, Undo& u)
{
    const uint8_t v = uint8_t(q.to_move + 1);
    const Delta d = delta_at(q.s, p, v);
    u.score[0] = q.score[0]; u.score[1] = q.score[1];
    u.fours[0] = q.fours[0]; u.fours[1] = q.fours[1];
    q.score[v - 1] += d.dv;
    q.score[2 - v] += d.dother;
    q.fours[v - 1] += d.fours_v;
    q.fours[2 - v] += d.fours_other;
    q.s[p] = v;
    ++q.moves;
    q.to_move ^= 1;
    return d.five;
}

void unplace(Pos& q, int p, const Undo& u)
{
    q.s[p] = 0;
    --q.moves;
    q.to_move ^= 1;
    q.score[0] = u.score[0]; q.score[1] = u.score[1];
    q.fours[0] = u.fours[0]; q.fours[1] = u.fours[1];
}

struct Search {
    uint32_t nodes = 0, budget = 0;
    volatile bool* stop = nullptr;
    bool out = false;
};

constexpr int32_t kWin = 100000000;

// From the side to move's view: a four of mine = I make five next;
// two of theirs (an open four) = they make five whatever I do
int32_t evaluate(const Pos& q)
{
    const int me = q.to_move, them = me ^ 1;
    if (q.fours[me] > 0) return kWin / 2;
    if (q.fours[them] >= 2) return -kWin / 2;
    return q.score[me] - q.score[them] * 11 / 10;
}

int32_t negamax(Pos& q, int depth, int32_t alpha, int32_t beta, int width, Search& S, int ply)
{
    if (q.moves >= kPoints) return 0;
    if (depth == 0) return evaluate(q);
    if (++S.nodes > S.budget || (S.stop && *S.stop)) { S.out = true; return 0; }
    Cand c[12];
    const int n = candidates(q, c, width);
    int32_t best = -kWin * 2;
    for (int i = 0; i < n; ++i) {
        Undo u;
        const bool five = place(q, c[i].p, u);
        const int32_t v = five ? kWin - ply : -negamax(q, depth - 1, -beta, -alpha, width, S, ply + 1);
        unplace(q, c[i].p, u);
        if (S.out) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

} // namespace

int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop)
{
    if (b.result() != -1) return -1;
    Pos* q = new Pos;                 // ~250 bytes: off the AI task's stack in case it is small
    pos_from(b, *q);
    Cand c[12];
    const int width = level == 0 ? 12 : level == 1 ? 5 : 7;
    // Easy plays its own lines first (it blocks a five, but not much else)
    const int n = candidates(*q, c, width, level == 0 ? 4 : 9);
    int best = -1;
    if (!n) {
        for (int p = 0; p < kPoints && best < 0; ++p) if (!b.stone[p]) best = p;
        delete q;
        return best;
    }
    // Equal candidates: a random one of them first
    int ties = 1;
    while (ties < n && c[ties].score == c[0].score) ++ties;
    seed = seed * 1103515245u + 12345u;
    const int t = int((seed >> 8) % uint32_t(ties));
    const Cand tc = c[0]; c[0] = c[t]; c[t] = tc;
    const uint8_t me = uint8_t(b.turn() + 1);
    // A five now, else block theirs
    for (int i = 0; i < n && best < 0; ++i) if (delta_at(b.stone, c[i].p, me).five) best = c[i].p;
    for (int i = 0; i < n && best < 0; ++i) if (delta_at(b.stone, c[i].p, uint8_t(me ^ 3)).five) best = c[i].p;
    if (best < 0 && level == 0) best = c[0].p;
    if (best < 0) {
        Search S;
        S.stop = stop;
        S.budget = 200000;
        const int depth = level == 1 ? 4 : 6;
        best = c[0].p;
        int32_t alpha = -kWin * 2;
        for (int i = 0; i < n; ++i) {
            Undo u;
            const bool five = place(*q, c[i].p, u);
            const int32_t v = five ? kWin : -negamax(*q, depth - 1, -kWin * 2, -alpha, width, S, 1);
            unplace(*q, c[i].p, u);
            if (S.out) break;
            if (v > alpha) { alpha = v; best = c[i].p; }
        }
    }
    delete q;
    return best;
}

} // namespace gomoku
