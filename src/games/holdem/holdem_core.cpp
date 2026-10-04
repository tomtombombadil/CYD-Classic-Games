#include "holdem_core.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace holdem {

namespace {

inline int hrank(uint8_t c) { const int r = c % 13 + 1; return r == 1 ? 14 : r; }   // 2..14
inline int suit(uint8_t c) { return c / 13; }

inline uint32_t pack(int cat, int a = 0, int b = 0, int c = 0, int d = 0, int e = 0)
{
    return uint32_t(cat) << 20 | uint32_t(a) << 16 | uint32_t(b) << 12 | uint32_t(c) << 8 | uint32_t(d) << 4 | uint32_t(e);
}

// Highest card of a five-in-a-row in `mask` (bit r = rank r present; bit 1 = ace low), 0 if none
int straight_top(uint32_t mask)
{
    for (int top = 14; top >= 5; --top)
        if (((mask >> (top - 4)) & 0x1F) == 0x1F) return top;
    return 0;
}

// The `n` highest ranks in mask (bits 2..14), skipping `skip1`/`skip2`
void top_ranks(uint32_t mask, int n, int* out, int skip1 = 0, int skip2 = 0)
{
    int k = 0;
    for (int r = 14; r >= 2 && k < n; --r)
        if (((mask >> r) & 1) && r != skip1 && r != skip2) out[k++] = r;
    while (k < n) out[k++] = 0;
}

const char* const kCatNames[] = {"High Card", "Pair", "Two Pair", "Three of a Kind", "Straight",
                                 "Flush", "Full House", "Four of a Kind", "Straight Flush"};
const char* const kActNames[] = {"", "Fold", "Check", "Call", "Bet", "Raise", "All In", "Blind"};

void shuffle(uint8_t* a, int n, Rng& rng)
{
    for (int i = n - 1; i > 0; --i) {
        const int j = rng.below(i + 1);
        const uint8_t t = a[i]; a[i] = a[j]; a[j] = t;
    }
}

} // namespace

const char* category_name(int c) { return c >= 0 && c <= StraightFlush ? kCatNames[c] : "?"; }
const char* act_name(Act a)      { return kActNames[int(a) <= int(Act::Blind) ? int(a) : 0]; }

int Rng::below(int n)
{
    if (n <= 1) return 0;
    const uint32_t limit = 0xFFFFFFFFu - 0xFFFFFFFFu % uint32_t(n);
    uint32_t v;
    do v = next(); while (v >= limit);
    return int(v % uint32_t(n));
}

uint32_t score(const uint8_t* cards, int n)
{
    int rc[15] = {}, sc[4] = {};
    uint32_t smask[4] = {}, rmask = 0;
    for (int i = 0; i < n; ++i) {
        const int r = hrank(cards[i]), s = suit(cards[i]);
        ++rc[r];
        ++sc[s];
        smask[s] |= 1u << r;
        rmask |= 1u << r;
        if (r == 14) { smask[s] |= 2u; rmask |= 2u; }      // ace low for straights
    }
    int fl = -1;
    for (int s = 0; s < 4; ++s) if (sc[s] >= 5) fl = s;
    if (fl >= 0) {
        const int sf = straight_top(smask[fl]);
        if (sf) return pack(StraightFlush, sf);
    }
    int quad = 0, trip1 = 0, trip2 = 0, pair1 = 0, pair2 = 0;
    for (int r = 14; r >= 2; --r) {
        if (rc[r] == 4 && !quad) quad = r;
        else if (rc[r] == 3) { if (!trip1) trip1 = r; else if (!trip2) trip2 = r; }
        else if (rc[r] == 2) { if (!pair1) pair1 = r; else if (!pair2) pair2 = r; }
    }
    const uint32_t ranks = rmask & ~2u;
    if (quad) { int k[1]; top_ranks(ranks, 1, k, quad); return pack(Quads, quad, k[0]); }
    if (trip1 && (trip2 || pair1)) {
        const int p = trip2 > pair1 ? trip2 : pair1;
        return pack(FullHouse, trip1, p);
    }
    if (fl >= 0) { int k[5]; top_ranks(smask[fl] & ~2u, 5, k); return pack(Flush, k[0], k[1], k[2], k[3], k[4]); }
    if (const int st = straight_top(rmask)) return pack(Straight, st);
    if (trip1) { int k[2]; top_ranks(ranks, 2, k, trip1); return pack(Trips, trip1, k[0], k[1]); }
    if (pair2) { int k[1]; top_ranks(ranks, 1, k, pair1, pair2); return pack(TwoPair, pair1, pair2, k[0]); }
    if (pair1) { int k[3]; top_ranks(ranks, 3, k, pair1); return pack(Pair, pair1, k[0], k[1], k[2]); }
    int k[5];
    top_ranks(ranks, 5, k);
    return pack(HighCard, k[0], k[1], k[2], k[3], k[4]);
}

// ---- Betting ------------------------------------------------------------------------------
int32_t Game::pot() const
{
    int32_t p = 0;
    for (const Seat& s : seat) p += s.total;
    return p;
}

int Game::in_hand() const
{
    int n = 0;
    for (const Seat& s : seat) n += !s.folded;
    return n;
}

int Game::can_act() const
{
    int n = 0;
    for (const Seat& s : seat) n += !s.folded && !s.all_in;
    return n;
}

void Game::post(int s, int32_t amount, Act a)
{
    Seat& p = seat[s];
    if (amount > p.stack) amount = p.stack;
    p.stack -= amount;
    p.bet += amount;
    p.total += amount;
    if (p.stack == 0) { p.all_in = true; if (a != Act::Blind) a = Act::AllIn; }
    p.last = a;
    p.last_amount = p.bet;
}

void Game::new_hand(uint32_t seed)
{
    if (seat[0].stack <= 0) return;                         // you need chips first
    for (int s = 1; s < kSeats; ++s)
        if (seat[s].stack <= 0) { seat[s].stack = kStartStack; ++seat[s].rebuys; }   // the computer buys back in
    for (Seat& p : seat) {
        p.bet = p.total = 0;
        p.folded = p.all_in = p.acted = false;
        p.last = Act::None;
        p.last_amount = p.won = p.returned = 0;
        p.value = 0;
    }
    Rng rng(seed);
    for (int i = 0; i < 52; ++i) deck[i] = uint8_t(i);
    shuffle(deck, 52, rng);
    pos = 0;
    dealer = uint8_t((dealer + 1) % kSeats);
    for (int k = 0; k < 2; ++k)
        for (int i = 1; i <= kSeats; ++i) seat[(dealer + i) % kSeats].cards[k] = deck[pos++];
    board_n = 0;
    street = Street::Preflop;
    showdown = false;
    current_bet = 0;
    min_raise = kBigBlind;
    const int sb = (dealer + 1) % kSeats, bb = (dealer + 2) % kSeats;
    post(sb, kSmallBlind, Act::Blind);
    post(bb, kBigBlind, Act::Blind);
    current_bet = kBigBlind;
    ++hand_no;
    to_act = uint8_t(bb);
    next_player();
}

int32_t Game::to_call() const
{
    const Seat& p = seat[to_act];
    const int32_t c = current_bet - p.bet;
    return c < 0 ? 0 : c > p.stack ? p.stack : c;
}

int32_t Game::max_raise_to() const { return seat[to_act].bet + seat[to_act].stack; }

int32_t Game::min_raise_to() const
{
    const int32_t m = current_bet + min_raise;
    return m > max_raise_to() ? max_raise_to() : m;
}

void Game::act(Decision d)
{
    if (street == Street::Over || street == Street::Showdown) return;
    Seat& p = seat[to_act];
    if (p.folded || p.all_in) { next_player(); return; }
    switch (d.act) {
        case Act::Fold:
            if (to_call() == 0) { p.last = Act::Check; break; }   // never fold for free
            p.folded = true;
            p.last = Act::Fold;
            break;
        case Act::Check:
        case Act::Call: {
            const int32_t c = to_call();
            if (c == 0) p.last = Act::Check;
            else post(to_act, c, Act::Call);
            break;
        }
        default: {                                         // Bet, Raise, All In
            int32_t to = d.act == Act::AllIn ? max_raise_to() : d.to;
            if (to > max_raise_to()) to = max_raise_to();
            if (to < min_raise_to()) to = min_raise_to();
            if (to <= current_bet) {                       // can't raise: a call
                const int32_t c = to_call();
                if (c == 0) p.last = Act::Check; else post(to_act, c, Act::Call);
                break;
            }
            const int32_t raise = to - current_bet;
            if (raise >= min_raise) min_raise = raise;
            const bool opening = current_bet == 0;
            current_bet = to;
            post(to_act, to - p.bet, opening ? Act::Bet : Act::Raise);
            for (int s = 0; s < kSeats; ++s) if (s != to_act) seat[s].acted = false;   // the others must answer
            break;
        }
    }
    p.acted = true;
    next_player();
}

void Game::next_player()
{
    if (in_hand() <= 1) { finish(); return; }
    // Is the betting round complete?
    bool done = true;
    for (const Seat& s : seat)
        if (!s.folded && !s.all_in && (!s.acted || s.bet < current_bet)) { done = false; break; }
    if (done) { end_street(); return; }
    for (int i = 1; i <= kSeats; ++i) {
        const int s = (to_act + i) % kSeats;
        const Seat& p = seat[s];
        if (!p.folded && !p.all_in && (!p.acted || p.bet < current_bet)) { to_act = uint8_t(s); return; }
    }
    end_street();
}

void Game::end_street()
{
    for (;;) {
        for (Seat& s : seat) { s.bet = 0; s.acted = false; }
        current_bet = 0;
        min_raise = kBigBlind;
        switch (street) {
            case Street::Preflop: for (int k = 0; k < 3; ++k) board[board_n++] = deck[pos++]; street = Street::Flop; break;
            case Street::Flop:    board[board_n++] = deck[pos++]; street = Street::Turn; break;
            case Street::Turn:    board[board_n++] = deck[pos++]; street = Street::River; break;
            default:              finish(); return;
        }
        for (Seat& s : seat) if (!s.folded && !s.all_in) s.last = Act::None;
        if (can_act() >= 2) break;                         // betting goes on
        // Everyone (or all but one) is all in: deal the rest out
    }
    for (int i = 1; i <= kSeats; ++i) {
        const int s = (dealer + i) % kSeats;
        if (!seat[s].folded && !seat[s].all_in) { to_act = uint8_t(s); return; }
    }
}

void Game::finish()
{
    street = Street::Over;
    for (Seat& s : seat) s.won = s.returned = 0;
    if (in_hand() == 1) {                                  // everyone else folded
        for (Seat& s : seat) if (!s.folded) { s.won = pot(); s.stack += s.won; }
        showdown = false;
        return;
    }
    showdown = true;
    // Deal out any missing board cards (an all-in before the river)
    while (board_n < 5) board[board_n++] = deck[pos++];
    for (Seat& s : seat) {
        if (s.folded) continue;
        uint8_t c[7] = {s.cards[0], s.cards[1], board[0], board[1], board[2], board[3], board[4]};
        s.value = score(c, 7);
    }
    // Side pots, smallest stake first
    int32_t prev = 0;
    for (;;) {
        int32_t level = 0;
        for (const Seat& s : seat)
            if (!s.folded && s.total > prev && (level == 0 || s.total < level)) level = s.total;
        if (level == 0) break;
        int32_t amount = 0;
        for (const Seat& s : seat) {
            const int32_t a = s.total < level ? s.total : level, b = s.total < prev ? s.total : prev;
            amount += a - b;
        }
        // Only one player still in reached this level: their own chips in it
        // go back to them (an uncalled bet), the folded players' are won
        int eligible = 0;
        for (const Seat& s : seat) eligible += !s.folded && s.total >= level;
        if (eligible == 1) {
            for (Seat& s : seat)
                if (!s.folded && s.total >= level) {
                    const int32_t own = level - (s.total < prev ? s.total : prev);
                    s.returned += own;
                    s.won += amount - own;
                }
            prev = level;
            continue;
        }
        uint32_t best = 0;
        for (const Seat& s : seat) if (!s.folded && s.total >= level && s.value > best) best = s.value;
        int winners = 0;
        for (const Seat& s : seat) winners += !s.folded && s.total >= level && s.value == best;
        const int32_t share = amount / winners;
        int32_t odd = amount - share * winners;
        for (int i = 1; i <= kSeats; ++i) {                // odd chips: first winner left of the button
            Seat& s = seat[(dealer + i) % kSeats];
            if (!s.folded && s.total >= level && s.value == best) { s.won += share + odd; odd = 0; }
        }
        prev = level;
    }
    for (Seat& s : seat) s.stack += s.won + s.returned;
}

// ---- The computer --------------------------------------------------------------------------
double Game::equity(int s, int samples, Rng& rng) const
{
    // Cards it can't see: everything but its own two and the board
    uint8_t unseen[52];
    int n = 0;
    for (int c = 0; c < 52; ++c) {
        bool known = c == seat[s].cards[0] || c == seat[s].cards[1];
        for (int b = 0; b < board_n; ++b) known |= c == board[b];
        if (!known) unseen[n++] = uint8_t(c);
    }
    int opp[kSeats], no = 0;
    for (int i = 0; i < kSeats; ++i) if (i != s && !seat[i].folded) opp[no++] = i;
    const int need = 5 - board_n + 2 * no;
    double wins = 0;
    for (int k = 0; k < samples; ++k) {
        for (int i = 0; i < need; ++i) {                   // partial shuffle
            const int j = i + rng.below(n - i);
            const uint8_t t = unseen[i]; unseen[i] = unseen[j]; unseen[j] = t;
        }
        uint8_t full[5];
        for (int b = 0; b < 5; ++b) full[b] = b < board_n ? board[b] : unseen[b - board_n];
        int at = 5 - board_n;
        uint8_t c[7] = {seat[s].cards[0], seat[s].cards[1], full[0], full[1], full[2], full[3], full[4]};
        const uint32_t mine = score(c, 7);
        bool lost = false;
        int ties = 0;
        for (int o = 0; o < no && !lost; ++o) {
            c[0] = unseen[at++];
            c[1] = unseen[at++];
            const uint32_t v = score(c, 7);
            if (v > mine) lost = true;
            else if (v == mine) ++ties;
        }
        if (!lost) wins += 1.0 / (1 + ties);
    }
    (void)opp;
    return wins / samples;
}

Decision Game::decide(Rng& rng) const
{
    const int s = to_act;
    const Seat& p = seat[s];
    static const int kSamples[3] = {60, 200, 500};
    // Styles: seat 1 steady, seat 2 tight and patient, seat 3 loose and pushy
    static const double kTight[kSeats] = {0, 0.0, 0.06, -0.05};
    static const double kPush[kSeats]  = {0, 1.0, 0.8, 1.4};
    const int lv = level > 2 ? 2 : level;
    const double eq = equity(s, kSamples[lv], rng);
    const int opponents = in_hand() - 1;
    const double fair = 1.0 / (opponents + 1);
    const int32_t call = to_call(), pot_now = pot();
    const double odds = call > 0 ? double(call) / double(pot_now + call) : 0;
    // How strong against this many opponents: 1.0 = an average hand
    const double rel = eq / fair;
    const double r = (rng.next() % 1000) / 1000.0;
    const bool late = s == dealer;                         // acts last after the flop
    // Someone who bets into you usually holds more than a random hand:
    // count the price of calling against the estimate
    double adj = eq;
    if (call > 0) {
        const double size = double(call) / double(pot_now > 0 ? pot_now : 1);
        adj = eq * (1.0 - 0.45 * (size > 1 ? 1 : size));
    }
    const double rel_adj = adj / fair;
    double raise_at = 1.5 + kTight[s] * 4 - (lv == 2 && late ? 0.1 : 0);
    if (street == Street::Preflop) raise_at += 0.15;
    // Raise with strength (about half to a whole pot), sometimes just call to vary
    if (rel_adj >= raise_at && max_raise_to() > current_bet && r < 0.7 + 0.15 * kPush[s]) {
        double frac = 0.5 + (rel_adj - raise_at) * 0.4;
        if (frac > 1.0) frac = 1.0;
        int32_t to = current_bet + int32_t((pot_now + call) * frac);
        if (to < min_raise_to()) to = min_raise_to();
        // Very strong late in the hand: all in when the stack is small next to the pot
        if (adj > 0.8 && street >= Street::Turn && p.stack < pot_now) return {Act::AllIn, max_raise_to()};
        if (to >= max_raise_to()) return {Act::AllIn, max_raise_to()};
        return {current_bet ? Act::Raise : Act::Bet, to};
    }
    if (call == 0) {
        // Free to check; a pushy player sometimes bets a fair hand on a later street
        if (street != Street::Preflop && rel > 1.0 && r < 0.2 * kPush[s] && opponents <= 2) {
            int32_t to = pot_now / 2;
            if (to < min_raise_to()) to = min_raise_to();
            return {Act::Bet, to};
        }
        return {Act::Check, 0};
    }
    // Before the flop: play hands better than average (by style), cheap looks a bit looser
    if (street == Street::Preflop) {
        const double need = 1.05 + kTight[s] * 3 - (call <= kBigBlind ? 0.12 : 0) - (lv >= 1 && late ? 0.05 : 0);
        return {rel_adj >= need ? Act::Call : Act::Fold, 0};
    }
    // After: call when the chance of winning beats the price (a margin by style)
    const double margin = 0.03 + kTight[s] - (lv >= 1 && late ? 0.02 : 0);
    if (adj >= odds + margin) return {Act::Call, 0};
    return {Act::Fold, 0};
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
    memcpy(p, "HLD1", 4); p += 4;
    for (const Seat& s : seat) {
        put32(p, s.stack); put32(p, s.bet); put32(p, s.total); put32(p, s.last_amount);
        put32(p, s.won); put32(p, int32_t(s.value));
        *p++ = uint8_t(s.rebuys); *p++ = uint8_t(s.rebuys >> 8);
        *p++ = s.cards[0]; *p++ = s.cards[1];
        *p++ = uint8_t(s.folded | s.all_in << 1 | s.acted << 2);
        *p++ = uint8_t(s.last);
        *p++ = 0; *p++ = 0;
    }
    memcpy(p, deck, 52); p += 52;
    *p++ = pos;
    memcpy(p, board, 5); p += 5;
    *p++ = board_n;
    *p++ = uint8_t(street);
    *p++ = dealer;
    *p++ = to_act;
    put32(p, current_bet);
    put32(p, min_raise);
    put32(p, int32_t(hand_no));
    *p++ = level;
    *p++ = showdown;
    return size_t(p - buf);
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "HLD1", 4) != 0) return false;
    Game g;
    const uint8_t* p = buf + 4;
    for (Seat& s : g.seat) {
        s.stack = get32(p); s.bet = get32(p); s.total = get32(p); s.last_amount = get32(p);
        s.won = get32(p); s.value = uint32_t(get32(p));
        s.rebuys = uint16_t(p[0] | p[1] << 8); p += 2;
        s.cards[0] = *p++; s.cards[1] = *p++;
        const uint8_t f = *p++;
        s.folded = f & 1; s.all_in = (f >> 1) & 1; s.acted = (f >> 2) & 1;
        const uint8_t a = *p++;
        s.last = a <= uint8_t(Act::Blind) ? Act(a) : Act::None;
        p += 2;
        if (s.cards[0] > 51 || s.cards[1] > 51 || s.stack < 0) return false;
    }
    memcpy(g.deck, p, 52); p += 52;
    uint64_t bits = 0;
    for (int i = 0; i < 52; ++i) { if (g.deck[i] > 51 || ((bits >> g.deck[i]) & 1)) return false; bits |= 1ull << g.deck[i]; }
    g.pos = *p++;
    memcpy(g.board, p, 5); p += 5;
    g.board_n = *p++;
    const uint8_t st = *p++;
    g.dealer = *p++;
    g.to_act = *p++;
    g.current_bet = get32(p);
    g.min_raise = get32(p);
    g.hand_no = uint32_t(get32(p));
    g.level = *p++;
    g.showdown = *p++ != 0;
    if (st > uint8_t(Street::Over) || g.board_n > 5 || g.pos > 52 || g.dealer >= kSeats || g.to_act >= kSeats || g.level > 2)
        return false;
    g.street = Street(st);
    *this = g;
    return true;
}

// ---- Stats --------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Result,Net,Chips\n";
namespace { const char* const kResults[] = {"Won", "Lost", "Folded", "Split"}; }

const char* result_name(Result r) { return kResults[int(r) < 4 ? int(r) : 1]; }

size_t format_body(char* buf, size_t cap, const Record& r)
{
    const int n = snprintf(buf, cap, "%s,%ld,%ld\n", result_name(r.result), long(r.net), long(r.chips));
    return n > 0 && size_t(n) < cap ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    if (line[0] < '0' || line[0] > '9') return false;
    const char* p = strchr(line, ',');
    if (!p) return false;
    ++p;
    const char* comma = strchr(p, ',');
    if (!comma) return false;
    Record r;
    bool found = false;
    for (int k = 0; k < 4; ++k)
        if (size_t(comma - p) == strlen(kResults[k]) && strncmp(p, kResults[k], comma - p) == 0) { r.result = Result(k); found = true; }
    if (!found) return false;
    char* end;
    r.net = int32_t(strtol(comma + 1, &end, 10));
    if (*end != ',') return false;
    r.chips = int32_t(strtol(end + 1, &end, 10));
    out = r;
    return true;
}

void Summary::add(const Record& r)
{
    ++hands;
    if (r.result == Result::Won || r.result == Result::Split) ++won;
    if (r.result == Result::Folded) ++folded;
    net += r.net;
    if (r.chips > best_chips) best_chips = r.chips;
    if (r.net > biggest) biggest = r.net;
}

} // namespace holdem
