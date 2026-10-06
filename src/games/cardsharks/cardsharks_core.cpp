#include "cardsharks_core.h"

#include <cstring>

namespace csh {

namespace {
uint32_t step(uint32_t& s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
}

// The deck: everything not on the table, shuffled
void Board::shuffle_in()
{
    bool used[kDeck] = {};
    for (const Row& r : row) for (uint8_t c : r.card) if (c != kNoCard) used[c] = true;
    deck_n = 0;
    for (int c = 0; c < kDeck; ++c) if (!used[c]) deck[deck_n++] = uint8_t(c);
    for (int i = deck_n - 1; i > 0; --i) {
        const int j = int(step(rng) % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
}

uint8_t Board::draw()
{
    if (deck_n == 0) shuffle_in();
    return deck[--deck_n];
}

void Board::new_round()
{
    for (Row& r : row) r = Row{};
    shuffle_in();
    for (Row& r : row) r.card[0] = draw();
    turn_side = uint8_t(round & 1);
    changed = called = false;
}

void Board::reset(uint32_t seed)
{
    *this = Board{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) step(rng);
    new_round();
}

bool Board::can_play(int m) const
{
    if (winner >= 0) return false;
    const Row& r = row[turn_side];
    switch (m) {
        case kHigher: case kLower: return r.pos < kRow - 1;
        case kFreeze: return r.pos > r.frozen;
        case kChange: return !changed && !called && r.pos == r.frozen;
    }
    return false;
}

bool Board::play(int m)
{
    if (!can_play(m)) return false;
    Row& r = row[turn_side];
    last_side = int8_t(turn_side);
    ++moves;
    const auto pass_turn = [&]() { turn_side ^= 1; changed = called = false; };
    if (m == kChange) {
        r.card[r.pos] = draw();
        changed = true;
        last = Last::Changed;
        return true;
    }
    if (m == kFreeze) {
        r.frozen = r.pos;
        last = Last::Froze;
        pass_turn();
        return true;
    }
    called = true;
    const uint8_t next = draw();
    const int a = value(r.card[r.pos]), b = value(next);
    const bool right = m == kHigher ? b > a : b < a;
    if (right) {
        r.card[++r.pos] = next;
        last = Last::Right;
        if (r.pos == kRow - 1) {
            last = Last::RoundWon;
            ++wins[turn_side];
            if (wins[turn_side] >= kWinRounds) { winner = int8_t(turn_side); return true; }
            ++round;
            new_round();
        }
        return true;
    }
    last = Last::Wrong;
    last_card = next;
    missed_at = uint8_t(r.pos + 1);
    for (int i = r.frozen + 1; i < kRow; ++i) r.card[i] = kNoCard;
    r.pos = r.frozen;
    pass_turn();
    return true;
}

float Board::chance(uint8_t card, bool higher) const
{
    bool seen[kDeck] = {};
    for (const Row& r : row) for (uint8_t c : r.card) if (c != kNoCard) seen[c] = true;
    const int v = value(card);
    int n = 0, good = 0;
    for (int c = 0; c < kDeck; ++c) {
        if (seen[c]) continue;
        ++n;
        good += higher ? value(uint8_t(c)) > v : value(uint8_t(c)) < v;
    }
    return n ? float(good) / float(n) : 0.0f;
}

int best_move(const Board& b, int level, uint32_t seed)
{
    const Row& r = b.row[b.turn_side];
    const Row& o = b.row[b.turn_side ^ 1];
    const uint8_t base = r.card[r.pos];
    const int v = value(base);
    float ph, pl;
    if (level >= 2) { ph = b.chance(base, true); pl = b.chance(base, false); }
    else { ph = float(14 - v) / 12.0f; pl = float(v - 2) / 12.0f; }   // by the card alone
    const float best = ph > pl ? ph : pl;
    const int call = ph > pl ? kHigher : ph < pl ? kLower : ((seed >> 5) & 1 ? kHigher : kLower);
    // Change a middling base before the first call
    if (b.can_play(kChange)) {
        if (level == 1 && v >= 7 && v <= 9) return kChange;
        if (level >= 2 && best < 0.66f) return kChange;
    }
    if (b.can_play(kFreeze)) {
        const int gained = r.pos - r.frozen;
        if (level == 0) { if (gained >= 2) return kFreeze; }
        else {
            const bool they_close = o.pos >= kRow - 2;            // one or two from winning
            const float need = level >= 2 ? (they_close ? 0.40f : 0.62f) : 0.55f;
            if (best < need) return kFreeze;
            if (level >= 2 && gained >= 3 && best < 0.75f && !they_close) return kFreeze;
        }
    }
    return call;
}

// ---- Save ----------------------------------------------------------------------------------
size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "CSC1", 4); n = 4;
    for (const Row& r : row) {
        memcpy(buf + n, r.card, kRow); n += kRow;
        buf[n++] = uint8_t(r.pos); buf[n++] = uint8_t(r.frozen);
    }
    memcpy(buf + n, deck, kDeck); n += kDeck;
    buf[n++] = deck_n;
    buf[n++] = turn_side;
    buf[n++] = wins[0]; buf[n++] = wins[1];
    buf[n++] = round;
    buf[n++] = changed ? 1 : 0;
    buf[n++] = called ? 1 : 0;
    buf[n++] = uint8_t(winner);
    buf[n++] = uint8_t(last);
    buf[n++] = uint8_t(last_side);
    buf[n++] = last_card;
    buf[n++] = missed_at;
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    return n;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "CSC1", 4) != 0) return false;
    Board g;
    size_t n = 4;
    for (Row& r : g.row) {
        memcpy(r.card, buf + n, kRow); n += kRow;
        r.pos = int8_t(buf[n++]); r.frozen = int8_t(buf[n++]);
        if (r.pos < 0 || r.pos >= kRow || r.frozen < 0 || r.frozen > r.pos) return false;
        for (int i = 0; i < kRow; ++i) {
            if (i <= r.pos && r.card[i] >= kDeck) return false;
            if (i > r.pos && r.card[i] != kNoCard) return false;
        }
    }
    memcpy(g.deck, buf + n, kDeck); n += kDeck;
    g.deck_n = buf[n++];
    if (g.deck_n > kDeck) return false;
    for (int i = 0; i < g.deck_n; ++i) if (g.deck[i] >= kDeck) return false;
    g.turn_side = buf[n++];
    g.wins[0] = buf[n++]; g.wins[1] = buf[n++];
    g.round = buf[n++];
    g.changed = buf[n++] != 0;
    g.called = buf[n++] != 0;
    g.winner = int8_t(buf[n++]);
    if (buf[n] > uint8_t(Last::RoundWon)) return false;
    g.last = Last(buf[n++]);
    g.last_side = int8_t(buf[n++]);
    g.last_card = buf[n++];
    g.missed_at = buf[n++];
    g.moves = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (g.turn_side > 1 || g.wins[0] > kWinRounds || g.wins[1] > kWinRounds || g.winner < -1 || g.winner > 1) return false;
    if (g.last_card != kNoCard && g.last_card >= kDeck) return false;
    *this = g;
    return true;
}

} // namespace csh
