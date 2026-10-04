#include "farkle_core.h"

#include <cstring>

namespace farkle {

namespace {

int kind(int face, int k)
{
    if (k == 3) return face == 1 ? 1000 : face * 100;
    return k == 4 ? 1000 : k == 5 ? 2000 : 3000;
}

// Score of all the dice counted in c[0..5] (faces 1..6), -1 if impossible
int exact(const int c[6])
{
    int total = 0;
    for (int f = 0; f < 6; ++f) total += c[f];
    if (!total) return 0;
    int best = -1;
    if (total == 6) {
        int ones = 0, twos = 0, threes = 0, fours = 0;
        for (int f = 0; f < 6; ++f) { ones += c[f] == 1; twos += c[f] == 2; threes += c[f] == 3; fours += c[f] == 4; }
        if (ones == 6) best = 1500;                       // 1 2 3 4 5 6
        if (twos == 3) best = best > 1500 ? best : 1500;  // three pairs
        if (threes == 2) best = best > 2500 ? best : 2500;
        if (fours == 1 && twos == 1) best = best > 1500 ? best : 1500;
    }
    // Otherwise each face's dice score as one unit: three or more of a kind
    // as a set (four 1s = 1000, not a triple plus a single), else only 1s
    // and 5s, singly
    int sum = 0;
    for (int f = 0; f < 6; ++f) {
        if (!c[f]) continue;
        if (c[f] >= 3) sum += kind(f + 1, c[f]);
        else if (f == 0) sum += 100 * c[f];
        else if (f == 4) sum += 50 * c[f];
        else { sum = -1; break; }
    }
    if (sum > best) best = sum;
    return best;
}

int popcount(unsigned m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

// One roll of d dice: the chance of a Farkle, and the average best score when not
// (worked out exactly offline: tools in the commit that added this)
const double kFarkleP[7] = {0, 0.6667, 0.4444, 0.2778, 0.1574, 0.0772, 0.0231};
const double kGain[7]    = {0, 75, 90, 120, 170, 245, 435};

} // namespace

int score_exact(const uint8_t* v, int n)
{
    int c[6] = {};
    for (int i = 0; i < n; ++i) { if (v[i] < 1 || v[i] > 6) return -1; ++c[v[i] - 1]; }
    return exact(c);
}

int best_set(const uint8_t* v, int n, uint8_t* mask)
{
    int best = 0;
    uint8_t bm = 0;
    for (unsigned m = 1; m < (1u << n); ++m) {
        uint8_t sel[kDice];
        int k = 0;
        for (int i = 0; i < n; ++i) if ((m >> i) & 1) sel[k++] = v[i];
        const int s = score_exact(sel, k);
        if (s > best || (s == best && s > 0 && popcount(m) > popcount(bm))) { best = s; bm = uint8_t(m); }
    }
    if (mask) *mask = bm;
    return best;
}

// ---- A turn -------------------------------------------------------------------------------
int Game::picked_score() const
{
    if (!picked) return 0;
    uint8_t sel[kDice];
    int k = 0;
    for (int i = 0; i < kDice; ++i) if ((picked >> i) & 1) sel[k++] = dice[i];
    return score_exact(sel, k);
}

bool Game::can_roll() const
{
    if (phase == Phase::Start) return true;
    return phase == Phase::Rolled && picked && picked_score() > 0;
}

bool Game::can_bank() const { return can_roll() && phase == Phase::Rolled; }

int Game::dice_to_roll() const
{
    if (phase == Phase::Start) return kDice;
    const int left = kDice - popcount(kept | picked);
    return left ? left : kDice;                          // hot dice: all six again
}

void Game::toggle(int i)
{
    if (phase != Phase::Rolled || i < 0 || i >= kDice || !((live >> i) & 1)) return;
    picked ^= uint8_t(1 << i);
}

bool Game::roll(Rng& rng)
{
    if (!can_roll()) return false;
    if (phase == Phase::Rolled) {
        turn_score += picked_score();
        kept |= picked;
        picked = 0;
        if (popcount(kept) == kDice) kept = 0;          // hot dice
    } else {
        turn_score = 0;
        kept = picked = 0;
    }
    live = 0;
    for (int i = 0; i < kDice; ++i)
        if (!((kept >> i) & 1)) { dice[i] = uint8_t(rng.die()); live |= uint8_t(1 << i); }
    uint8_t thrown[kDice];
    int n = 0;
    for (int i = 0; i < kDice; ++i) if ((live >> i) & 1) thrown[n++] = dice[i];
    phase = best_set(thrown, n) > 0 ? Phase::Rolled : Phase::Farkle;
    return true;
}

bool Game::bank()
{
    if (!can_bank()) return false;
    turn_score += picked_score();
    score[turn] += turn_score;
    ++turns;
    if (last_turn_for >= 0) {                            // the last turn is over
        winner = score[1] > score[0] ? 1 : score[0] > score[1] ? 0 : uint8_t(turn ^ 1);
        phase = Phase::Over;
        return true;
    }
    if (score[turn] >= kTarget) last_turn_for = int8_t(turn ^ 1);
    turn ^= 1;
    turn_score = 0;
    kept = picked = live = 0;
    phase = Phase::Start;
    return true;
}

void Game::next_turn()
{
    if (phase != Phase::Farkle) return;
    ++turns;
    if (last_turn_for >= 0) {                            // farkled the last turn: the leader wins
        winner = uint8_t(turn ^ 1);
        phase = Phase::Over;
        return;
    }
    turn ^= 1;
    turn_score = 0;
    kept = picked = live = 0;
    phase = Phase::Start;
}

// ---- The computer ----------------------------------------------------------------------------
Plan plan(const Game& g, int level)
{
    Plan p{0, false};
    if (g.phase != Phase::Rolled) return p;
    uint8_t idx[kDice];
    int n = 0;
    for (int i = 0; i < kDice; ++i) if ((g.live >> i) & 1) idx[n++] = uint8_t(i);
    const int me = g.turn, them = me ^ 1;
    const int32_t need = g.last_turn_for >= 0 ? g.score[them] - g.score[me] + 1 : 0;   // on the last turn: to win
    double best_v = -1;
    for (unsigned m = 1; m < (1u << n); ++m) {
        uint8_t sel[kDice];
        int k = 0;
        uint8_t mask = 0;
        for (int i = 0; i < n; ++i) if ((m >> i) & 1) { sel[k++] = g.dice[idx[i]]; mask |= uint8_t(1 << idx[i]); }
        const int s = score_exact(sel, k);
        if (s <= 0) continue;
        int left = n - k;
        if (left == 0) left = kDice;
        const int32_t t = g.turn_score + s;
        double v;
        if (level == 0) v = s * 10.0 + k;               // Easy: the most points
        else {
            // Points now plus what rolling on is worth with the dice left
            const double on = (1 - kFarkleP[left]) * kGain[left] - kFarkleP[left] * t;
            v = t + (on > 0 ? on : 0);
        }
        if (v > best_v) { best_v = v; p.pick = mask; }
    }
    // Then: roll on or bank?
    uint8_t sel[kDice];
    int k = 0;
    for (int i = 0; i < kDice; ++i) if ((p.pick >> i) & 1) sel[k++] = g.dice[i];
    const int32_t t = g.turn_score + score_exact(sel, k);
    int left = n - k;
    const bool hot = left == 0;
    if (hot) left = kDice;
    if (need > 0) { p.roll_on = t < need; return p; }   // the last turn: go until ahead
    if (g.score[me] + t >= kTarget) { p.roll_on = false; return p; }
    if (hot) { p.roll_on = true; return p; }
    switch (level) {
        case 0: p.roll_on = t < 300; break;
        case 1: {
            static const int32_t kBankAt[7] = {0, 300, 300, 400, 600, 1500, 1 << 30};
            p.roll_on = t < kBankAt[left];
            break;
        }
        default: {
            // Worth rolling when the expected gain beats the expected loss;
            // bolder when far behind, steadier when well ahead
            const int32_t gap = g.score[them] - g.score[me];
            const double nerve = gap > 3000 ? 1.25 : gap < -3000 ? 0.85 : 1.0;
            p.roll_on = (1 - kFarkleP[left]) * kGain[left] * nerve > kFarkleP[left] * t;
            break;
        }
    }
    return p;
}

// ---- Save -------------------------------------------------------------------------------------
namespace {
void put32(uint8_t*& p, int32_t v) { for (int k = 0; k < 4; ++k) *p++ = uint8_t(uint32_t(v) >> (8 * k)); }
int32_t get32(const uint8_t*& p) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(*p++) << (8 * k); return int32_t(v); }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "FRK1", 4); p += 4;
    put32(p, score[0]); put32(p, score[1]);
    *p++ = turn;
    put32(p, turn_score);
    memcpy(p, dice, kDice); p += kDice;
    *p++ = live; *p++ = picked; *p++ = kept;
    *p++ = uint8_t(phase);
    *p++ = uint8_t(last_turn_for);
    *p++ = winner;
    *p++ = uint8_t(turns); *p++ = uint8_t(turns >> 8);
    return size_t(p - buf);
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "FRK1", 4) != 0) return false;
    Game g;
    const uint8_t* p = buf + 4;
    g.score[0] = get32(p); g.score[1] = get32(p);
    g.turn = *p++ & 1;
    g.turn_score = get32(p);
    memcpy(g.dice, p, kDice); p += kDice;
    g.live = *p++ & 0x3F; g.picked = *p++ & 0x3F; g.kept = *p++ & 0x3F;
    const uint8_t ph = *p++;
    g.last_turn_for = int8_t(*p++);
    g.winner = *p++ & 1;
    g.turns = uint16_t(p[0] | p[1] << 8);
    for (int i = 0; i < kDice; ++i) if (g.dice[i] < 1 || g.dice[i] > 6) return false;
    if (ph > uint8_t(Phase::Over) || g.last_turn_for < -1 || g.last_turn_for > 1) return false;
    g.phase = Phase(ph);
    *this = g;
    return true;
}

} // namespace farkle
