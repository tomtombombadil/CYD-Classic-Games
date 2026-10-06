#include "strategygo_core.h"

#include <cstring>

namespace sgo {

const uint8_t kCount[kRanks] = {1, 1, 8, 5, 4, 4, 4, 3, 2, 1, 1, 6};

namespace {
const char* const kNames[kRanks] = {"Flag", "Spy", "Scout", "Miner", "Sergeant", "Lieutenant", "Captain",
                                    "Major", "Colonel", "General", "Marshal", "Bomb"};
const char* const kShort[kRanks] = {"F", "S", "2", "3", "4", "5", "6", "7", "8", "9", "10", "B"};
constexpr int kStep[4] = {-kN, 1, kN, -1};

bool step_ok(int c, int d)
{
    const int r = c / kN, k = c % kN;
    switch (d) {
        case 0: return r > 0;
        case 1: return k < kN - 1;
        case 2: return r < kN - 1;
        default: return k > 0;
    }
}

uint32_t next_rand(uint32_t& s)
{
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

// Who wins when `a` strikes `d`
Outcome resolve(int a, int d)
{
    if (d == kFlag) return kFlagTaken;
    if (d == kBomb) return a == kMiner ? kBombDefused : kBombHit;
    if (a == kSpy && d == kMarshal) return kAttackerWins;
    if (a > d) return kAttackerWins;
    if (a == d) return kBothLost;
    return kDefenderWins;
}
} // namespace

const char* rank_name(int r) { return r >= 0 && r < kRanks ? kNames[r] : "?"; }
const char* rank_short(int r) { return r >= 0 && r < kRanks ? kShort[r] : "?"; }

int army_cell(int side, int i)
{
    const int row = i / kN, col = i % kN;                 // row 0 = the back row
    return side == 0 ? (kN - 1 - row) * kN + col : row * kN + (kN - 1 - col);
}

int Board::placed(int side) const
{
    if (!setup()) return kArmy;
    return side == 0 ? (moves < kArmy ? moves : kArmy) : (moves > kArmy ? moves - kArmy : 0);
}

int Board::left(int side, int rank) const
{
    int n = kCount[rank];
    for (int c = 0; c < kCells; ++c) n -= sq[c].side == side && sq[c].rank == rank;
    return n;
}

int Board::alive(int side) const
{
    int n = 0;
    for (int c = 0; c < kCells; ++c) n += sq[c].side == side;
    return n;
}

int Board::targets(int from, uint8_t* out) const
{
    if (from < 0 || from >= kCells) return 0;
    const Square& p = sq[from];
    if (p.side < 0 || !mobile(p.rank)) return 0;
    int n = 0;
    for (int d = 0; d < 4; ++d) {
        int c = from;
        while (step_ok(c, d)) {
            c += kStep[d];
            if (lake(c) || sq[c].side == p.side) break;
            out[n++] = uint8_t(c);
            if (sq[c].side >= 0 || p.rank != kScout) break;      // a battle ends it; only Scouts go on
        }
    }
    return n;
}

bool Board::can_play(uint32_t key) const
{
    if (winner >= 0) return false;
    const int s = turn();
    if (setup()) {
        const int cell = int(key & 127), rank = int(key >> 7);
        if (key >> 11 || cell >= kCells || rank >= kRanks) return false;
        return home(s, cell) && sq[cell].side < 0 && left(s, rank) > 0;
    }
    if (key >> 14) return false;
    const int from = key_from(key), to = key_to(key);
    if (from >= kCells || to >= kCells || sq[from].side != s) return false;
    uint8_t t[2 * kN];
    const int n = targets(from, t);
    bool ok = false;
    for (int i = 0; i < n; ++i) ok |= t[i] == to;
    if (!ok) return false;
    // The two-squares rule: not a sixth time between the same two squares
    if (last_to[s] == from && last_from[s] == to && shuttle[s] >= kShuttle) return false;
    return true;
}

int Board::moves_for(int side, uint32_t* out, int cap) const
{
    int n = 0;
    for (int c = 0; c < kCells; ++c) {
        if (sq[c].side != side) continue;
        uint8_t t[2 * kN];
        const int k = targets(c, t);
        for (int i = 0; i < k && n < cap; ++i) {
            const uint32_t key = move_key(c, t[i]);
            if (last_to[side] == c && last_from[side] == t[i] && shuttle[side] >= kShuttle) continue;
            out[n++] = key;
        }
    }
    return n;
}

bool Board::play(uint32_t key)
{
    if (!can_play(key)) return false;
    const int s = turn();
    if (setup()) {
        const int cell = int(key & 127), rank = int(key >> 7);
        sq[cell].side = int8_t(s);
        sq[cell].rank = uint8_t(rank);
        sq[cell].shown = sq[cell].moved = false;
        ++moves;
        return true;
    }
    const int from = key_from(key), to = key_to(key);
    // The two-squares rule's count
    if (last_to[s] == from && last_from[s] == to) ++shuttle[s];
    else shuttle[s] = 1;
    last_from[s] = int8_t(from);
    last_to[s] = int8_t(to);
    last_move_from = int8_t(from);
    last_move_to = int8_t(to);
    Square a = sq[from];
    a.moved = true;
    sq[from] = Square{};
    last = Battle{};
    const int dist = from / kN == to / kN ? (to > from ? to - from : from - to) : (to > from ? to - from : from - to) / kN;
    if (dist > 1) a.shown = true;                         // a long move gives a Scout away
    if (sq[to].side < 0) {
        sq[to] = a;
    } else {
        Square d = sq[to];
        const Outcome o = resolve(a.rank, d.rank);
        last.from = int8_t(from); last.to = int8_t(to); last.side = int8_t(s);
        last.attacker = a.rank; last.defender = d.rank; last.outcome = o;
        a.shown = d.shown = true;
        switch (o) {
            case kAttackerWins: case kFlagTaken: case kBombDefused:
                ++lost[d.side][d.rank]; sq[to] = a; break;
            case kDefenderWins: case kBombHit:
                ++lost[a.side][a.rank]; sq[to] = d; break;
            case kBothLost:
                ++lost[a.side][a.rank]; ++lost[d.side][d.rank]; sq[to] = Square{}; break;
            default: break;
        }
        if (o == kFlagTaken) winner = int8_t(s);
    }
    ++moves;
    if (winner < 0) {
        uint32_t m[4];
        if (moves_for(s ^ 1, m, 1) == 0) winner = int8_t(s);              // they can't move
        else if (moves - kSetupPlies >= kMaxPlies) winner = 2;
    }
    return true;
}

// ---- Setups ------------------------------------------------------------------------------------
namespace {
// Row weights from the front row (0) to the back (3), by rank
const uint8_t kRowWeight[kRanks][4] = {
    {0, 0, 0, 1},   // Flag (placed on the back row on its own)
    {1, 3, 3, 2},   // Spy
    {5, 3, 1, 1},   // Scout
    {1, 2, 3, 3},   // Miner
    {2, 3, 3, 2},   // Sergeant
    {2, 3, 3, 2},   // Lieutenant
    {3, 3, 2, 2},   // Captain
    {3, 3, 2, 2},   // Major
    {3, 3, 2, 2},   // Colonel
    {3, 3, 2, 1},   // General
    {3, 3, 2, 1},   // Marshal
    {1, 3, 3, 1},   // Bomb (those not round the Flag)
};
const uint8_t kOrder[kRanks] = {kFlag, kBomb, kMarshal, kGeneral, kColonel, kMajor, kCaptain,
                                kLieutenant, kSergeant, kMiner, kSpy, kScout};

int front_index(int side, int c) { const int r = c / kN; return side == 0 ? r - 6 : 3 - r; }

// The next setup piece for the side to set up: its rank and square
uint32_t setup_step(const Board& b, uint32_t seed)
{
    const int s = b.turn();
    uint32_t rs = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) next_rand(rs);
    int rank = -1;
    for (int k = 0; k < kRanks && rank < 0; ++k) if (b.left(s, kOrder[k]) > 0) rank = kOrder[k];
    if (rank < 0) return 0;
    int cand[kArmy], w[kArmy], n = 0, total = 0;
    if (rank == kBomb) {
        // Round the Flag first (beside it and in front of it)
        int flag = -1;
        for (int c = 0; c < kCells; ++c) if (b.sq[c].side == s && b.sq[c].rank == kFlag) flag = c;
        if (flag >= 0) {
            for (int d = 0; d < 4; ++d) {
                if (!step_ok(flag, d)) continue;
                const int c = flag + kStep[d];
                if (home(s, c) && b.sq[c].side < 0) { cand[n] = c; w[n] = 1; total += 1; ++n; }
            }
        }
    }
    if (n == 0) {
        for (int c = 0; c < kCells; ++c) {
            if (!home(s, c) || b.sq[c].side >= 0) continue;
            const int wt = kRowWeight[rank][front_index(s, c)];
            if (!wt) continue;
            cand[n] = c; w[n] = wt; total += wt; ++n;
        }
    }
    if (n == 0) {                                            // (cannot happen: room for every rank)
        for (int c = 0; c < kCells; ++c) if (home(s, c) && b.sq[c].side < 0) { cand[n] = c; w[n] = 1; total += 1; ++n; }
    }
    int pick = int(next_rand(rs) % uint32_t(total));
    int i = 0;
    while (i < n - 1 && pick >= w[i]) { pick -= w[i]; ++i; }
    return setup_key(cand[i], rank);
}
} // namespace

void random_army(uint32_t seed, Army& out)
{
    Board b;
    uint32_t s = seed ? seed : 1;
    while (b.placed(0) < kArmy) b.play(setup_step(b, next_rand(s)));
    for (int i = 0; i < kArmy; ++i) out.rank_at[i] = b.sq[army_cell(0, i)].rank;
}

// ---- The computer -------------------------------------------------------------------------------
namespace {

const float kValue[kRanks] = {1000, 60, 10, 30, 15, 25, 40, 60, 100, 160, 250, 15};

// The board as one side sees it: the other side's ranks only where seen
struct Seen { int8_t side; uint8_t rank; bool moved, shown; };   // rank 255 = unknown
constexpr uint8_t kUnknown = 255;

struct Mind {
    int   me;
    int   level;
    Seen  v[kCells];
    float p_moved[kRanks];        // an unknown piece of theirs that has moved: chances by rank
    float p_still[kRanks];        // ... that hasn't
    float ev_moved, ev_still;     // their expected values
};

void make_mind(const Board& b, int me, int level, Mind& m)
{
    m.me = me;
    m.level = level;
    const int them = me ^ 1;
    int pool[kRanks];
    for (int r = 0; r < kRanks; ++r) pool[r] = kCount[r] - b.lost[them][r];
    int moved_unknown = 0, still_unknown = 0;
    for (int c = 0; c < kCells; ++c) {
        const Square& q = b.sq[c];
        m.v[c].side = q.side;
        m.v[c].moved = q.moved;
        m.v[c].shown = q.shown;
        if (q.side == me || q.side < 0 || q.shown) m.v[c].rank = q.rank;
        else {
            m.v[c].rank = kUnknown;
            if (q.moved) ++moved_unknown; else ++still_unknown;
        }
        if (q.side == them && q.shown && pool[q.rank] > 0) --pool[q.rank];
    }
    int movable = 0;
    for (int r = 0; r < kRanks; ++r) if (mobile(r)) movable += pool[r];
    const int bf = pool[kFlag] + pool[kBomb];
    const float still_bf = still_unknown ? float(bf) / float(still_unknown) : 0.0f;
    m.ev_moved = m.ev_still = 0;
    for (int r = 0; r < kRanks; ++r) {
        if (mobile(r)) {
            m.p_moved[r] = movable ? float(pool[r]) / float(movable) : 0;
            m.p_still[r] = movable ? (1.0f - still_bf) * float(pool[r]) / float(movable) : 0;
        } else {
            m.p_moved[r] = 0;
            m.p_still[r] = bf ? still_bf * float(pool[r]) / float(bf) : 0;
        }
        m.ev_moved += m.p_moved[r] * kValue[r];
        m.ev_still += m.p_still[r] * kValue[r];
    }
}

const float* dist(const Mind& m, const Seen& s) { return s.moved ? m.p_moved : m.p_still; }

float piece_value(const Mind& m, const Seen& s)
{
    if (s.rank != kUnknown) return kValue[s.rank];
    return s.moved ? m.ev_moved : m.ev_still;
}

// The expected change in my side's material when `att` (on `from`) strikes `def`.
// Positive is good for me. Also fills the chances of each outcome.
struct Strike { float p[7]; float gain[7]; };

Strike strike(const Mind& m, const Seen& att, const Seen& def)
{
    Strike st{};
    const bool mine = att.side == m.me;
    const float sign = mine ? 1.0f : -1.0f;
    auto add = [&](int a, int d, float pr) {
        if (pr <= 0) return;
        const Outcome o = resolve(a, d);
        float g = 0;
        switch (o) {
            case kAttackerWins: case kBombDefused: g = kValue[d]; break;
            case kFlagTaken: g = 5000; break;
            case kDefenderWins: case kBombHit: g = -kValue[a]; break;
            case kBothLost: g = kValue[d] - kValue[a]; break;
            default: break;
        }
        st.p[o] += pr;
        st.gain[o] += pr * g * sign;
    };
    if (att.rank != kUnknown && def.rank != kUnknown) add(att.rank, def.rank, 1.0f);
    else if (att.rank != kUnknown) { const float* p = dist(m, def); for (int r = 0; r < kRanks; ++r) add(att.rank, r, p[r]); }
    else if (def.rank != kUnknown) { const float* p = dist(m, att); for (int r = 0; r < kRanks; ++r) add(r, def.rank, p[r]); }
    else { const float* p = dist(m, att); for (int r = 0; r < kRanks; ++r) add(r, kSpy, p[r]); }  // (not used: two unknowns)
    return st;
}

// How good a position is for me: material, a little progress, and (level 1+)
// what their pieces next to mine threaten
float evaluate(const Mind& m, bool threats)
{
    float e = 0;
    for (int c = 0; c < kCells; ++c) {
        const Seen& s = m.v[c];
        if (s.side < 0) continue;
        if (s.side == m.me) {
            e += kValue[s.rank];
            if (s.shown && s.rank >= kCaptain) e -= 0.08f * kValue[s.rank];         // they know it
            if (mobile(s.rank)) e += 0.4f * float(m.me == 0 ? 9 - c / kN : c / kN);  // forward
        } else {
            e -= piece_value(m, s);
        }
    }
    if (threats) {
        for (int c = 0; c < kCells; ++c) {
            const Seen& mine = m.v[c];
            if (mine.side != m.me) continue;
            float worst = 0;
            for (int d = 0; d < 4; ++d) {
                if (!step_ok(c, d)) continue;
                const Seen& q = m.v[c + kStep[d]];
                if (q.side != (m.me ^ 1)) continue;
                if (q.rank != kUnknown && !mobile(q.rank)) continue;
                if (q.rank == kUnknown && m.level < 2) continue;       // Medium only fears pieces it has seen
                const Strike st = strike(m, q, mine);
                float g = 0;
                for (int o = 0; o < 7; ++o) g += st.gain[o];
                if (q.rank == kUnknown && !q.moved) g *= 0.6f;                 // it may not move at all
                if (g < worst) worst = g;
            }
            e += 0.6f * worst;
        }
    }
    return e;
}

// My moves in the mind (their pieces' moves too, with unknown ones as one-steppers)
int mind_moves(const Mind& m, int side, const Board& b, uint32_t* out, int cap)
{
    int n = 0;
    for (int c = 0; c < kCells; ++c) {
        const Seen& s = m.v[c];
        if (s.side != side) continue;
        const bool known = s.rank != kUnknown;
        if (known && !mobile(s.rank)) continue;
        const bool scout = known && s.rank == kScout;
        for (int d = 0; d < 4; ++d) {
            int t = c;
            while (step_ok(t, d)) {
                t += kStep[d];
                if (lake(t) || m.v[t].side == side) break;
                if (n < cap) {
                    const bool banned = b.last_to[side] == c && b.last_from[side] == t && b.shuttle[side] >= kShuttle;
                    if (!banned) out[n++] = move_key(c, t);
                }
                if (m.v[t].side >= 0 || !scout) break;
            }
        }
    }
    return n;
}

// Plays a move in the mind for one outcome; returns false if that outcome can't happen
bool apply(Mind& m, uint32_t key, int o)
{
    const int from = key_from(key), to = key_to(key);
    Seen a = m.v[from];
    a.moved = true;
    m.v[from] = Seen{-1, 0, false, false};
    if (m.v[to].side < 0) {
        if (o != kNoBattle) return false;
        m.v[to] = a;
        return true;
    }
    Seen d = m.v[to];
    switch (o) {
        case kAttackerWins: case kFlagTaken: case kBombDefused: a.shown = true; m.v[to] = a; break;
        case kDefenderWins: case kBombHit: d.shown = true; d.moved = d.moved || o == kDefenderWins; m.v[to] = d; break;
        case kBothLost: m.v[to] = Seen{-1, 0, false, false}; break;
        default: return false;
    }
    return true;
}

// The expected value of a move: over its outcomes, of the position after (depth 2:
// after their best reply - tried for Hard, it played no better than one move
// ahead with the threats weighed, so every level looks one move ahead)
float move_value(const Mind& base, const Board& b, uint32_t key, int depth)
{
    const int from = key_from(key), to = key_to(key);
    const Seen att = base.v[from], def = base.v[to];
    float total = 0;
    auto after = [&](Mind& m) -> float {
        // Level 1 weighs the threats left after its move; level 2 looks at
        // their replies instead (and judges those positions on material)
        if (depth <= 1) return evaluate(m, base.level >= 1);
        // Their best reply (the worst for me)
        uint32_t rep[256];
        const int n = mind_moves(m, m.me ^ 1, b, rep, 256);
        float worst = 1e30f;
        for (int i = 0; i < n; ++i) {
            const float v = move_value(m, b, rep[i], 1);
            if (v < worst) worst = v;
        }
        return n ? worst : evaluate(m, false) + 3000;          // they can't move: I win
    };
    if (def.side < 0) {
        Mind m = base;
        apply(m, key, kNoBattle);
        return after(m);
    }
    const Strike st = strike(base, att, def);
    for (int o = 1; o < 7; ++o) {
        if (st.p[o] <= 0.0005f) continue;
        Mind m = base;
        if (!apply(m, key, o)) continue;
        float v = after(m);
        if (o == kFlagTaken) v += att.side == base.me ? 5000 : -5000;
        total += st.p[o] * v;
    }
    (void)from; (void)to;
    return total;
}

} // namespace

uint32_t best_move(const Board& b, int level, uint32_t seed)
{
    if (b.setup()) return setup_step(b, seed);
    const int me = b.turn();
    Mind* m = new Mind;
    make_mind(b, me, level, *m);
    uint32_t keys[256];
    const int n = b.moves_for(me, keys, 256);
    uint32_t best = n ? keys[0] : 0;
    float bv = -1e30f;
    uint32_t rs = seed ? seed : 1;
    for (int i = 0; i < n; ++i) {
        // A flag in reach: take it
        if (b.sq[key_to(keys[i])].side == (me ^ 1) && b.sq[key_to(keys[i])].shown && b.sq[key_to(keys[i])].rank == kFlag) { best = keys[i]; break; }
        const float v = move_value(*m, b, keys[i], 1) + float(next_rand(rs) % 100) * 0.01f;
        if (v > bv) { bv = v; best = keys[i]; }
    }
    delete m;
    return best;
}

// ---- Save ----------------------------------------------------------------------------------
size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "SGO1", 4); n = 4;
    for (int c = 0; c < kCells; ++c) {
        const Square& q = sq[c];
        buf[n++] = q.side < 0 ? 0 : uint8_t((q.side + 1) | (q.rank << 2) | (q.shown ? 0x40 : 0) | (q.moved ? 0x80 : 0));
    }
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    buf[n++] = uint8_t(winner);
    buf[n++] = uint8_t(last_from[0]); buf[n++] = uint8_t(last_from[1]);
    buf[n++] = uint8_t(last_to[0]); buf[n++] = uint8_t(last_to[1]);
    buf[n++] = shuttle[0]; buf[n++] = shuttle[1];
    buf[n++] = uint8_t(last.from); buf[n++] = uint8_t(last.to); buf[n++] = uint8_t(last.side);
    buf[n++] = last.attacker; buf[n++] = last.defender; buf[n++] = uint8_t(last.outcome);
    buf[n++] = uint8_t(last_move_from); buf[n++] = uint8_t(last_move_to);
    return n;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "SGO1", 4) != 0) return false;
    Board* g = new Board();
    size_t n = 4;
    bool ok = true;
    for (int c = 0; c < kCells && ok; ++c) {
        const uint8_t v = buf[n++];
        if (!v) continue;
        const int side = (v & 3) - 1, rank = (v >> 2) & 15;
        if (side < 0 || side > 1 || rank >= kRanks || lake(c)) ok = false;
        g->sq[c].side = int8_t(side);
        g->sq[c].rank = uint8_t(rank);
        g->sq[c].shown = v & 0x40;
        g->sq[c].moved = v & 0x80;
    }
    g->moves = uint16_t(buf[n] | (buf[n + 1] << 8)); n += 2;
    g->winner = int8_t(buf[n++]);
    g->last_from[0] = int8_t(buf[n++]); g->last_from[1] = int8_t(buf[n++]);
    g->last_to[0] = int8_t(buf[n++]); g->last_to[1] = int8_t(buf[n++]);
    g->shuttle[0] = buf[n++]; g->shuttle[1] = buf[n++];
    g->last.from = int8_t(buf[n++]); g->last.to = int8_t(buf[n++]); g->last.side = int8_t(buf[n++]);
    g->last.attacker = buf[n++]; g->last.defender = buf[n++];
    const uint8_t o = buf[n++];
    g->last_move_from = int8_t(buf[n++]); g->last_move_to = int8_t(buf[n++]);
    if (o > kBombHit || g->winner < -1 || g->winner > 2 || g->last.attacker >= kRanks || g->last.defender >= kRanks) ok = false;
    g->last.outcome = Outcome(o);
    for (int s = 0; s < 2 && ok; ++s)
        for (int r = 0; r < kRanks && ok; ++r) {
            int have = 0;
            for (int c = 0; c < kCells; ++c) have += g->sq[c].side == s && g->sq[c].rank == r;
            if (have > kCount[r]) ok = false;
            g->lost[s][r] = g->moves >= kSetupPlies ? uint8_t(kCount[r] - have) : 0;
        }
    // During the setup the pieces down must match the plies played
    if (ok && g->moves < kSetupPlies) ok = g->alive(0) == g->placed(0) && g->alive(1) == g->placed(1);
    if (ok) *this = *g;
    delete g;
    return ok;
}

} // namespace sgo
