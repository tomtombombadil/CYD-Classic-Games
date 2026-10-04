#include "vpoker_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vpoker {

namespace {

const char* const kNames[kRanks] = {"Nothing", "Jacks or Better", "Two Pair", "Three of a Kind", "Straight",
                                    "Flush", "Full House", "Four of a Kind", "Straight Flush", "Royal Flush"};
const int kPay[kRanks] = {0, 1, 2, 3, 4, 6, 9, 25, 50, 250};

inline int rank_of(uint8_t c) { return c % 13 + 1; }      // 1 = Ace .. 13 = King
inline int suit_of(uint8_t c) { return c / 13; }
inline int high_rank(uint8_t c) { const int r = rank_of(c); return r == 1 ? 14 : r; }
inline bool is_high(uint8_t c) { const int r = high_rank(c); return r >= 11; }   // J Q K A

} // namespace

const char* rank_name(int r) { return r >= 0 && r < kRanks ? kNames[r] : "?"; }

int pay(int rank, int bet)
{
    if (rank <= Nothing || rank >= kRanks) return 0;
    if (rank == RoyalFlush && bet >= kMaxBet) return 4000;
    return kPay[rank] * bet;
}

Rank evaluate(const uint8_t c[5])
{
    int count[15] = {};
    bool flush = true;
    for (int i = 0; i < 5; ++i) {
        ++count[high_rank(c[i])];
        if (suit_of(c[i]) != suit_of(c[0])) flush = false;
    }
    int pairs = 0, trips = 0, quads = 0, high_pair = 0;
    for (int r = 2; r <= 14; ++r) {
        if (count[r] == 2) { ++pairs; if (r >= 11) high_pair = 1; }
        if (count[r] == 3) ++trips;
        if (count[r] == 4) ++quads;
    }
    bool straight = false, ace_high = false;
    if (!pairs && !trips && !quads) {
        int lo = 15, hi = 0;
        for (int i = 0; i < 5; ++i) { const int r = high_rank(c[i]); lo = r < lo ? r : lo; hi = r > hi ? r : hi; }
        if (hi - lo == 4) { straight = true; ace_high = hi == 14; }
        else if (count[14] && count[2] && count[3] && count[4] && count[5]) straight = true;   // A 2 3 4 5
    }
    if (straight && flush) return ace_high ? RoyalFlush : StraightFlush;
    if (quads) return FourKind;
    if (trips && pairs) return FullHouse;
    if (flush) return Flush;
    if (straight) return Straight;
    if (trips) return ThreeKind;
    if (pairs == 2) return TwoPair;
    if (high_pair) return JacksOrBetter;
    return Nothing;
}

int Rng::below(int n)
{
    if (n <= 1) return 0;
    const uint32_t limit = 0xFFFFFFFFu - 0xFFFFFFFFu % uint32_t(n);
    uint32_t v;
    do v = next(); while (v >= limit);
    return int(v % uint32_t(n));
}

bool Game::deal(uint32_t seed)
{
    if (!can_deal()) return false;
    Rng rng(seed);
    for (int i = 0; i < 52; ++i) deck[i] = uint8_t(i);
    for (int i = 51; i > 0; --i) {
        const int j = rng.below(i + 1);
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    pos = 0;
    for (int i = 0; i < 5; ++i) hand[i] = deck[pos++];
    credits -= bet;
    held = 0;
    last_win = 0;
    last_rank = Nothing;
    phase = Phase::Dealt;
    return true;
}

bool Game::draw()
{
    if (phase != Phase::Dealt) return false;
    for (int i = 0; i < 5; ++i)
        if (!((held >> i) & 1)) hand[i] = deck[pos++];
    last_rank = evaluate(hand);
    last_win = pay(last_rank, bet);
    credits += last_win;
    phase = Phase::Done;
    return true;
}

// ---- Hint: simple strategy for 9/6 Jacks or Better ------------------------------------------
namespace {

int popcount(unsigned m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

// Every subset of the 5 cards with `k` cards satisfying `ok`; returns the
// first found (in mask order), 0 if none
template <class F>
uint8_t find_subset(int k, F ok)
{
    for (unsigned m = 1; m < 32; ++m)
        if (popcount(m) == k && ok(uint8_t(m))) return uint8_t(m);
    return 0;
}

bool same_suit(const uint8_t h[5], uint8_t m)
{
    int s = -1;
    for (int i = 0; i < 5; ++i)
        if ((m >> i) & 1) { if (s < 0) s = suit_of(h[i]); else if (suit_of(h[i]) != s) return false; }
    return true;
}

bool all_royal(const uint8_t h[5], uint8_t m)          // 10 J Q K A only
{
    for (int i = 0; i < 5; ++i) if (((m >> i) & 1) && high_rank(h[i]) < 10) return false;
    return true;
}

bool distinct(const uint8_t h[5], uint8_t m)
{
    int seen = 0;
    for (int i = 0; i < 5; ++i)
        if ((m >> i) & 1) { const int b = 1 << high_rank(h[i]); if (seen & b) return false; seen |= b; }
    return true;
}

// The held ranks fit in one straight window (ace high or low)
bool straight_window(const uint8_t h[5], uint8_t m)
{
    if (!distinct(h, m)) return false;
    for (int low = 1; low <= 10; ++low) {               // window low..low+4 (1 = ace low)
        bool fits = true;
        for (int i = 0; i < 5 && fits; ++i) {
            if (!((m >> i) & 1)) continue;
            const int r = high_rank(h[i]);
            const bool in = (r >= low && r <= low + 4) || (r == 14 && low == 1);
            fits = in;
        }
        if (fits) return true;
    }
    return false;
}

} // namespace

uint8_t hint(const uint8_t h[5])
{
    const Rank now = evaluate(h);
    int count[15] = {};
    for (int i = 0; i < 5; ++i) ++count[high_rank(h[i])];
    auto rank_mask = [&](int pred) {                    // cards whose rank count == pred
        uint8_t m = 0;
        for (int i = 0; i < 5; ++i) if (count[high_rank(h[i])] == pred) m |= uint8_t(1 << i);
        return m;
    };
    // 1. Pat royal / straight flush; four of a kind (the four)
    if (now == RoyalFlush || now == StraightFlush) return 0x1F;
    if (now == FourKind) return rank_mask(4);
    // 2. 4 to a royal flush
    if (uint8_t m = find_subset(4, [&](uint8_t s) { return same_suit(h, s) && all_royal(h, s); })) return m;
    // 3. Full house, flush, straight (pat); three of a kind (the three)
    if (now == FullHouse || now == Flush || now == Straight) return 0x1F;
    if (now == ThreeKind) return rank_mask(3);
    // 3b. 4 to a straight flush (worth more than two pair or a high pair)
    if (uint8_t m = find_subset(4, [&](uint8_t s) { return same_suit(h, s) && straight_window(h, s); })) return m;
    // 4. Two pair
    if (now == TwoPair) return rank_mask(2);
    // 5. High pair
    if (now == JacksOrBetter) return rank_mask(2);
    // 6. 3 to a royal flush
    if (uint8_t m = find_subset(3, [&](uint8_t s) { return same_suit(h, s) && all_royal(h, s); })) return m;
    // 7. 4 to a flush
    if (uint8_t m = find_subset(4, [&](uint8_t s) { return same_suit(h, s); })) return m;
    // 8. Low pair
    if (uint8_t m = rank_mask(2)) return m;
    // 9. 4 to an outside (open-ended) straight: four in a row, no ace
    if (uint8_t m = find_subset(4, [&](uint8_t s) {
            if (!distinct(h, s)) return false;
            int lo = 15, hi = 0;
            for (int i = 0; i < 5; ++i) if ((s >> i) & 1) { const int r = high_rank(h[i]); lo = r < lo ? r : lo; hi = r > hi ? r : hi; }
            return hi - lo == 3 && hi < 14; }))
        return m;
    // 10. 2 suited high cards
    if (uint8_t m = find_subset(2, [&](uint8_t s) {
            for (int i = 0; i < 5; ++i) if (((s >> i) & 1) && !is_high(h[i])) return false;
            return same_suit(h, s); }))
        return m;
    // 11. 3 to a straight flush
    if (uint8_t m = find_subset(3, [&](uint8_t s) { return same_suit(h, s) && straight_window(h, s); })) return m;
    // 12. 2 unsuited high cards (with three or more, the lowest two)
    uint8_t highs = 0;
    for (int i = 0; i < 5; ++i) if (is_high(h[i])) highs |= uint8_t(1 << i);
    if (popcount(highs) >= 2) {
        while (popcount(highs) > 2) {                    // drop the highest
            int top = -1;
            for (int i = 0; i < 5; ++i) if (((highs >> i) & 1) && (top < 0 || high_rank(h[i]) > high_rank(h[top]))) top = i;
            highs &= uint8_t(~(1 << top));
        }
        return highs;
    }
    // 13. Suited 10 with a J, Q or K
    if (uint8_t m = find_subset(2, [&](uint8_t s) {
            int ten = 0, face = 0;
            for (int i = 0; i < 5; ++i) if ((s >> i) & 1) { const int r = high_rank(h[i]); ten += r == 10; face += r >= 11 && r <= 13; }
            return ten == 1 && face == 1 && same_suit(h, s); }))
        return m;
    // 14. One high card
    if (highs) return highs;
    // 15. Draw five new cards
    return 0;
}

double hold_value(const uint8_t h[5], uint8_t mask)
{
    uint8_t rest[47];
    int nr = 0;
    for (int c = 0; c < 52; ++c) {
        bool in = false;
        for (int i = 0; i < 5; ++i) in |= h[i] == c;
        if (!in) rest[nr++] = uint8_t(c);
    }
    uint8_t cur[5];
    int fixed = 0;
    for (int i = 0; i < 5; ++i) if ((mask >> i) & 1) cur[fixed++] = h[i];
    const int need = 5 - fixed;
    double total = 0;
    long hands = 0;
    int idx[5];
    // Every combination of `need` cards from rest[]
    for (int k = 0; k < need; ++k) idx[k] = k;
    for (;;) {
        for (int k = 0; k < need; ++k) cur[fixed + k] = rest[idx[k]];
        total += pay(evaluate(cur), kMaxBet) / double(kMaxBet);
        ++hands;
        int k = need - 1;
        while (k >= 0 && idx[k] == nr - need + k) --k;
        if (k < 0) break;
        ++idx[k];
        for (int j = k + 1; j < need; ++j) idx[j] = idx[j - 1] + 1;
    }
    return total / double(hands);
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
    memcpy(p, "VPK1", 4); p += 4;
    memcpy(p, deck, 52); p += 52;
    *p++ = pos;
    memcpy(p, hand, 5); p += 5;
    *p++ = held;
    *p++ = uint8_t(phase);
    put32(p, credits);
    *p++ = bet;
    *p++ = last_rank;
    put32(p, last_win);
    *p++ = uint8_t(refills); *p++ = uint8_t(refills >> 8);
    return size_t(p - buf);
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "VPK1", 4) != 0) return false;
    Game g;
    const uint8_t* p = buf + 4;
    memcpy(g.deck, p, 52); p += 52;
    g.pos = *p++;
    memcpy(g.hand, p, 5); p += 5;
    g.held = uint8_t(*p++ & 0x1F);
    const uint8_t ph = *p++;
    g.credits = get32(p);
    g.bet = *p++;
    g.last_rank = *p++;
    g.last_win = get32(p);
    g.refills = uint16_t(p[0] | (p[1] << 8));
    // Sanity: a real deck, cards in range
    int seen = 0;
    uint64_t bits = 0;
    for (int i = 0; i < 52; ++i) { if (g.deck[i] > 51 || ((bits >> g.deck[i]) & 1)) return false; bits |= 1ull << g.deck[i]; ++seen; }
    for (int i = 0; i < 5; ++i) if (g.hand[i] > 51) return false;
    if (ph > 2 || g.pos > 52 || g.bet < 1 || g.bet > kMaxBet || g.last_rank >= kRanks) return false;
    if (ph == uint8_t(Phase::Dealt) && g.pos > 47) return false;     // the draw needs 5 more cards
    g.phase = Phase(ph);
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Bet,Hand,Win,Credits\n";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    const int n = snprintf(buf, cap, "%ld,%s,%ld,%ld\n", long(r.bet), rank_name(r.rank), long(r.win), long(r.credits));
    return n > 0 && size_t(n) < cap ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    // "seq,bet,hand,win,credits"
    const char* p = strchr(line, ',');
    if (!p || line[0] < '0' || line[0] > '9') return false;
    Record r;
    char* end;
    r.bet = int32_t(strtol(p + 1, &end, 10));
    if (*end != ',') return false;
    const char* name = end + 1;
    const char* comma = strchr(name, ',');
    if (!comma) return false;
    r.rank = Nothing;
    bool found = false;
    for (int k = 0; k < kRanks; ++k)
        if (size_t(comma - name) == strlen(kNames[k]) && strncmp(name, kNames[k], comma - name) == 0) { r.rank = uint8_t(k); found = true; }
    if (!found) return false;
    r.win = int32_t(strtol(comma + 1, &end, 10));
    if (*end != ',') return false;
    r.credits = int32_t(strtol(end + 1, &end, 10));
    out = r;
    return true;
}

void Summary::add(const Record& r)
{
    ++hands;
    if (r.win > 0) ++wins;
    net += r.win - r.bet;
    if (r.win > best_win) best_win = r.win;
    if (r.credits > best_credits) best_credits = r.credits;
    if (r.rank > best_rank) best_rank = r.rank;
    if (r.rank < kRanks) ++count[r.rank];
}

} // namespace vpoker
