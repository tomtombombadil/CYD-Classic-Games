#include "blackjack_core.h"

#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace blackjack {

int Hand::value(bool* soft) const
{
    int v = 0, aces = 0;
    for (int i = 0; i < n; ++i) { v += points(c[i]); aces += rank(c[i]) == 1; }
    bool s = false;
    if (aces && v + 10 <= 21) { v += 10; s = true; }
    if (soft) *soft = s;
    return v;
}

void Game::new_shoe(uint32_t sd)
{
    seed = sd;
    for (int i = 0; i < kShoe; ++i) shoe[i] = uint8_t(i % 52);
    Rng rng(sd);
    for (int i = kShoe - 1; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = shoe[i]; shoe[i] = shoe[j]; shoe[j] = t;
    }
    pos = 0;
}

uint8_t Game::draw()
{
    if (pos >= kShoe) new_shoe(seed * 1664525u + 1013904223u);
    return shoe[pos++];
}

void Game::add_bet(int32_t a)
{
    if (phase == Phase::Playing) return;
    bet += a;
    if (bet > chips) bet = chips;
    if (bet < 0) bet = 0;
}

bool Game::deal()
{
    if (!can_deal()) return false;
    if (shuffle_due()) new_shoe(seed * 1664525u + 1013904223u);
    hand[0] = Hand{};
    hand[1] = Hand{};
    dealer = Hand{};
    hands = 1;
    active = 0;
    result[0] = result[1] = Result::None;
    net = 0;
    hand[0].bet = bet;
    chips -= bet;
    hand[0].c[hand[0].n++] = draw();
    dealer.c[dealer.n++] = draw();
    hand[0].c[hand[0].n++] = draw();
    dealer.c[dealer.n++] = draw();          // the hole card
    phase = Phase::Playing;
    // Naturals end the round at once (the dealer peeks)
    if (hand[0].blackjack() || dealer.blackjack()) { hand[0].done = true; settle(); }
    return true;
}

bool Game::can_hit() const
{
    if (phase != Phase::Playing) return false;
    const Hand& h = hand[active];
    return !h.done && h.value() < 21 && !(h.from_split && rank(h.c[0]) == 1);
}

bool Game::can_double() const
{
    if (phase != Phase::Playing) return false;
    const Hand& h = hand[active];
    return !h.done && h.n == 2 && chips >= h.bet && !(h.from_split && rank(h.c[0]) == 1);
}

bool Game::can_split() const
{
    if (phase != Phase::Playing || hands != 1) return false;
    const Hand& h = hand[0];
    return h.n == 2 && points(h.c[0]) == points(h.c[1]) && chips >= h.bet;
}

bool Game::hit()
{
    if (!can_hit()) return false;
    Hand& h = hand[active];
    h.c[h.n++] = draw();
    if (h.value() >= 21 || h.n == kMaxCards) { h.done = true; next_hand(); }
    return true;
}

bool Game::stand()
{
    if (phase != Phase::Playing || hand[active].done) return false;
    hand[active].done = true;
    next_hand();
    return true;
}

bool Game::double_down()
{
    if (!can_double()) return false;
    Hand& h = hand[active];
    chips -= h.bet;
    h.bet *= 2;
    h.doubled = true;
    h.c[h.n++] = draw();
    h.done = true;
    next_hand();
    return true;
}

bool Game::split()
{
    if (!can_split()) return false;
    Hand& a = hand[0];
    Hand& b = hand[1];
    b = Hand{};
    b.c[0] = a.c[1];
    b.n = 1;
    b.bet = a.bet;
    b.from_split = a.from_split = true;
    chips -= a.bet;
    a.n = 1;
    a.c[a.n++] = draw();
    b.c[b.n++] = draw();
    hands = 2;
    active = 0;
    if (rank(a.c[0]) == 1) { a.done = b.done = true; next_hand(); return true; }   // split Aces: one card each
    if (a.value() == 21) { a.done = true; next_hand(); }
    return true;
}

void Game::next_hand()
{
    while (active < hands && hand[active].done) ++active;
    if (active < hands) {
        if (hand[active].value() == 21) { hand[active].done = true; next_hand(); }
        return;
    }
    active = uint8_t(hands - 1);
    settle();
}

void Game::settle()
{
    // The dealer draws only when some hand is still standing (not bust)
    bool any = false;
    for (int i = 0; i < hands; ++i) any |= !hand[i].bust() && !hand[i].blackjack();
    if (any && !dealer.blackjack())
        while (dealer.value() < 17 && dealer.n < kMaxCards) dealer.c[dealer.n++] = draw();
    const int d = dealer.value();
    net = 0;
    for (int i = 0; i < hands; ++i) {
        const Hand& h = hand[i];
        const int v = h.value();
        int32_t pay = 0;                     // returned to the chips, stake included
        if (h.blackjack() && !dealer.blackjack()) { result[i] = Result::Blackjack; pay = h.bet + h.bet * 3 / 2; }
        else if (h.blackjack() && dealer.blackjack()) { result[i] = Result::Push; pay = h.bet; }
        else if (dealer.blackjack())         { result[i] = Result::Lose; }
        else if (v > 21)                     { result[i] = Result::Lose; }
        else if (d > 21 || v > d)            { result[i] = Result::Win; pay = 2 * h.bet; }
        else if (v == d)                     { result[i] = Result::Push; pay = h.bet; }
        else                                 { result[i] = Result::Lose; }
        chips += pay;
        net += pay - h.bet;
    }
    phase = Phase::Done;
    if (bet > chips) bet = chips >= kMinBet ? chips : bet;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t k = 0;
    auto put32 = [&](int32_t v) { for (int b = 0; b < 4; ++b) buf[k++] = uint8_t(uint32_t(v) >> (8 * b)); };
    memcpy(buf, "BJK1", 4); k = 4;
    memcpy(buf + k, shoe, kShoe); k += kShoe;
    buf[k++] = uint8_t(pos); buf[k++] = uint8_t(pos >> 8);
    buf[k++] = uint8_t(phase);
    put32(chips); put32(bet);
    const Hand* hs[3] = {&hand[0], &hand[1], &dealer};
    for (const Hand* h : hs) {
        memcpy(buf + k, h->c, kMaxCards); k += kMaxCards;
        buf[k++] = h->n;
        put32(h->bet);
        buf[k++] = h->doubled; buf[k++] = h->done; buf[k++] = h->from_split;
    }
    buf[k++] = hands; buf[k++] = active;
    buf[k++] = uint8_t(result[0]); buf[k++] = uint8_t(result[1]);
    put32(net);
    buf[k++] = uint8_t(refills); buf[k++] = uint8_t(refills >> 8);
    put32(int32_t(seed));
    return k;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "BJK1", 4) != 0) return false;
    size_t k = 4;
    auto get32 = [&]() { uint32_t v = 0; for (int b = 0; b < 4; ++b) v |= uint32_t(buf[k++]) << (8 * b); return int32_t(v); };
    Game g;
    memcpy(g.shoe, buf + k, kShoe); k += kShoe;
    for (uint8_t c : g.shoe) if (c >= 52) return false;
    g.pos = uint16_t(buf[k] | (buf[k + 1] << 8)); k += 2;
    const uint8_t ph = buf[k++];
    if (ph > 2 || g.pos > kShoe) return false;
    g.phase = Phase(ph);
    g.chips = get32(); g.bet = get32();
    Hand* hs[3] = {&g.hand[0], &g.hand[1], &g.dealer};
    for (Hand* h : hs) {
        memcpy(h->c, buf + k, kMaxCards); k += kMaxCards;
        h->n = buf[k++];
        if (h->n > kMaxCards) return false;
        h->bet = get32();
        h->doubled = buf[k++]; h->done = buf[k++]; h->from_split = buf[k++];
    }
    g.hands = buf[k++]; g.active = buf[k++];
    if (g.hands < 1 || g.hands > 2 || g.active >= g.hands) return false;
    g.result[0] = Result(buf[k++] % 5); g.result[1] = Result(buf[k++] % 5);
    g.net = get32();
    g.refills = uint16_t(buf[k] | (buf[k + 1] << 8)); k += 2;
    g.seed = uint32_t(get32());
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Bet,Result,Net,Chips";

const char* result_name(Result r)
{
    switch (r) {
        case Result::Win:       return "Win";
        case Result::Lose:      return "Lose";
        case Result::Push:      return "Push";
        case Result::Blackjack: return "Blackjack";
        default:                return "";
    }
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    const int n = snprintf(buf, cap, "%ld,%s,%ld,%ld\n", long(r.bet), result_name(r.result), long(r.net), long(r.chips));
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0;
    long bet = 0, net = 0, chips = 0;
    char res[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%ld,%15[^,],%ld,%ld", &seq, &bet, res, &net, &chips) != 5) return false;
    Result r = Result::None;
    for (Result c : {Result::Win, Result::Lose, Result::Push, Result::Blackjack})
        if (strcmp(res, result_name(c)) == 0) r = c;
    if (r == Result::None) return false;
    out = Record{int32_t(bet), r, int32_t(net), int32_t(chips)};
    return true;
}

void Summary::add(const Record& r)
{
    ++hands;
    wins += r.result == Result::Win || r.result == Result::Blackjack;
    losses += r.result == Result::Lose;
    pushes += r.result == Result::Push;
    blackjacks += r.result == Result::Blackjack;
    if (r.chips > best_chips) best_chips = r.chips;
    net += r.net;
}

} // namespace blackjack
