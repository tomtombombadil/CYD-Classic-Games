// Blackjack rules, written for this project (MIT). Plain C++, host-tested
// in tools/host_tests/test_games.cpp.
//
// You against the house (the dealer). A six-deck shoe, reshuffled once
// about three quarters of it is used. Rules (common casino ones):
//   - blackjack (Ace + ten-card on the first two) pays 3 to 2
//   - the dealer checks for blackjack first, then draws to 17 and stands
//     on every 17, soft or hard
//   - double down on any first two cards (one more card, bet doubled)
//   - split a pair once (two hands); split Aces get one card each, and 21
//     on a split hand isn't a blackjack
//   - no insurance, no surrender
// Chips: 500 to start; when you can't cover the smallest bet you get a
// fresh 500 (counted).
// Cards: 0..51 = suit * 13 + rank - 1 (as common/cards.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace blackjack {

constexpr int kDecks = 6, kShoe = 52 * kDecks, kMaxCards = 12;
constexpr int32_t kStartChips = 500, kMinBet = 5;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

inline int rank(uint8_t c) { return c % 13 + 1; }
inline int points(uint8_t c) { const int r = rank(c); return r > 10 ? 10 : r; }

struct Hand {
    uint8_t c[kMaxCards] = {};
    uint8_t n = 0;
    int32_t bet = 0;
    bool    doubled = false, done = false, from_split = false;
    int  value(bool* soft = nullptr) const;     // best total (Aces 11 when it fits)
    bool blackjack() const { return n == 2 && !from_split && value() == 21; }
    bool bust() const { return value() > 21; }
};

enum class Phase : uint8_t { Betting = 0, Playing = 1, Done = 2 };
enum class Result : uint8_t { None = 0, Win, Lose, Push, Blackjack };

struct Game {
    uint8_t  shoe[kShoe] = {};
    uint16_t pos = 0;                   // next card in the shoe
    Phase    phase = Phase::Betting;
    int32_t  chips = kStartChips;
    int32_t  bet = 10;                  // the next round's bet
    Hand     hand[2];                   // the player's hands (2 after a split)
    uint8_t  hands = 1, active = 0;
    Hand     dealer;
    Result   result[2] = {};
    int32_t  net = 0;                   // the last round's win or loss
    uint16_t refills = 0;
    uint32_t seed = 1;

    void new_shoe(uint32_t seed);
    bool shuffle_due() const { return pos > kShoe * 3 / 4; }
    uint8_t draw();

    // Betting
    void add_bet(int32_t amount);       // clamped to the chips
    void clear_bet() { bet = 0; }
    bool can_deal() const { return phase != Phase::Playing && bet >= kMinBet && bet <= chips; }
    bool deal();                        // a new round (may end at once on a blackjack)
    bool broke() const { return phase != Phase::Playing && chips < kMinBet; }
    void refill() { chips += kStartChips; ++refills; if (bet > chips) bet = chips; }

    // Playing the active hand
    bool can_hit() const;
    bool can_double() const;
    bool can_split() const;
    bool hit();
    bool stand();
    bool double_down();
    bool split();

    size_t serialize(uint8_t* buf, size_t cap) const;   // "BJK1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kShoe + 2 + 1 + 4 + 4 + 3 * (kMaxCards + 1 + 4 + 3) + 2 + 2 + 4 + 2 + 4;

private:
    void next_hand();                   // move on, or let the dealer play and settle
    void settle();
};

// ---- Play history (one line per hand) --------------------------------------------------------
//   #,Bet,Result,Net,Chips
//   12,20,Blackjack,30,560
struct Record { int32_t bet = 0; Result result = Result::None; int32_t net = 0, chips = 0; };
extern const char* const kCsvHeader;
const char* result_name(Result r);       // "Win", "Lose", "Push", "Blackjack"
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t hands = 0, wins = 0, losses = 0, pushes = 0, blackjacks = 0;
    int32_t  best_chips = 0, net = 0;
    void add(const Record& r);
};

} // namespace blackjack
