// Video Poker, "Jacks or Better" (the classic machine game), written for
// this project (MIT). Plain C++, host-tested in tools/host_tests.
//
// Bet 1 to 5 credits, get five cards, tap the ones to hold, draw once to
// replace the rest. The final hand pays by the table below (full-pay 9/6:
// a Full House pays 9, a Flush 6, per credit bet); a Royal Flush pays 800
// per credit at a 5-credit bet (4000) and 250 below that. A fresh 52-card
// deck is shuffled for every hand. Credits: 500 to start, another 500 when
// you can't cover a bet (counted).
// Cards: 0..51 = suit * 13 + rank - 1 (as common/cards.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace vpoker {

enum Rank : uint8_t {
    Nothing = 0, JacksOrBetter, TwoPair, ThreeKind, Straight, Flush, FullHouse,
    FourKind, StraightFlush, RoyalFlush, kRanks
};
const char* rank_name(int r);                  // "Jacks or Better", "Two Pair", ...
int  pay(int rank, int bet);                   // credits won (0 for Nothing)
Rank evaluate(const uint8_t cards[5]);

constexpr int32_t kStartCredits = 500;
constexpr int     kMaxBet = 5;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n);
};

enum class Phase : uint8_t { Ready = 0, Dealt = 1, Done = 2 };

struct Game {
    uint8_t  deck[52] = {};
    uint8_t  pos = 0;                   // next card in the deck
    uint8_t  hand[5] = {};
    uint8_t  held = 0;                  // bit i = card i held
    Phase    phase = Phase::Ready;
    int32_t  credits = kStartCredits;
    uint8_t  bet = kMaxBet;
    uint8_t  last_rank = Nothing;       // the finished hand
    int32_t  last_win = 0;
    uint16_t refills = 0;

    bool can_deal() const { return phase != Phase::Dealt && bet >= 1 && bet <= credits; }
    bool deal(uint32_t seed);           // takes the bet, shuffles, deals five
    void toggle_hold(int i)  { if (phase == Phase::Dealt && i >= 0 && i < 5) held ^= uint8_t(1 << i); }
    bool draw();                        // replaces the unheld cards, pays
    void bet_one() { if (phase != Phase::Dealt) bet = uint8_t(bet % kMaxBet + 1); }
    bool broke() const { return phase != Phase::Dealt && credits < 1; }
    void refill() { credits += kStartCredits; ++refills; }
    Rank current() const { return evaluate(hand); }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "VPK1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 52 + 1 + 5 + 1 + 1 + 4 + 1 + 1 + 4 + 2;
};

// Which cards to hold (bit mask) by the well-known simple strategy for
// full-pay Jacks or Better: pat hands, 4 to a royal, trips/straight/flush,
// two pair, a high pair, 3 to a royal, 4 to a flush, a low pair, 4 to an
// open straight, suited high cards, 3 to a straight flush, high cards...
uint8_t hint(const uint8_t hand[5]);

// Exact expected return (credits per credit bet) of holding `mask`, by
// trying every draw: slow (up to 1.5 million hands) - for tests.
double hold_value(const uint8_t hand[5], uint8_t mask);

// ---- Play history (one line per hand) --------------------------------------------------------
//   #,Bet,Hand,Win,Credits
//   7,5,Two Pair,10,515
struct Record { int32_t bet = 0; uint8_t rank = Nothing; int32_t win = 0, credits = 0; };
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t hands = 0, wins = 0;
    int32_t  net = 0, best_win = 0, best_credits = 0;
    uint8_t  best_rank = Nothing;
    uint32_t count[kRanks] = {};
    void add(const Record& r);
};

} // namespace vpoker
