// Texas Hold'em (no limit) against three computer players, written for
// this project (MIT). Plain C++, host-tested in tools/host_tests.
//
// Four seats: 0 is you, 1-3 the computer. Blinds 5 / 10, 1000 chips each.
// The button moves one seat each hand. Each player gets two cards; five
// community cards come in three streets (flop 3, turn 1, river 1) with a
// betting round before the flop and after each street. Best five of your
// seven cards wins; all-ins make side pots. A computer player who runs out
// buys back in (1000); when you run out, New Chips gives you 1000 more.
//
// Computer players estimate how often their hand wins by dealing out the
// rest at random many times (Monte Carlo) and weigh that against the price
// of calling; each has a style (tight / loose, calm / aggressive). Levels
// change how carefully they estimate (more deals) and how well they use
// position and pot odds - never deliberate blunders.
// Cards: 0..51 = suit * 13 + rank - 1 (as common/cards.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace holdem {

constexpr int     kSeats = 4;
constexpr int32_t kStartStack = 1000, kSmallBlind = 5, kBigBlind = 10;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n);
};

// Hand value: bigger is better. Category in bits 20+, then up to five
// ranks (2..14) four bits each.
enum Category : uint8_t { HighCard = 0, Pair, TwoPair, Trips, Straight, Flush, FullHouse, Quads, StraightFlush };
uint32_t score(const uint8_t* cards, int n);          // best five of n (5..7)
inline int category(uint32_t s) { return int(s >> 20); }
const char* category_name(int c);                     // "Two Pair", ... "Straight Flush"

enum class Street : uint8_t { Preflop = 0, Flop, Turn, River, Showdown, Over };
enum class Act : uint8_t { None = 0, Fold, Check, Call, Bet, Raise, AllIn, Blind };
const char* act_name(Act a);                          // "Fold", "Check", ...

struct Seat {
    int32_t stack = kStartStack;
    int32_t bet = 0;                  // this street
    int32_t total = 0;                // this hand
    uint8_t cards[2] = {};
    bool    folded = false, all_in = false, acted = false;
    Act     last = Act::None;
    int32_t last_amount = 0;
    int32_t won = 0;                  // chips won this hand (after it's over)
    int32_t returned = 0;             // own uncalled chips handed back (not winnings; not saved)
    uint32_t value = 0;               // showdown hand value
    uint16_t rebuys = 0;
};

struct Decision { Act act; int32_t to; };             // to = the bet total for Bet/Raise

struct Game {
    Seat     seat[kSeats];
    uint8_t  deck[52] = {};
    uint8_t  pos = 0;
    uint8_t  board[5] = {};
    uint8_t  board_n = 0;
    Street   street = Street::Over;
    uint8_t  dealer = 0;
    uint8_t  to_act = 0;
    int32_t  current_bet = 0;         // the bet to match this street
    int32_t  min_raise = kBigBlind;   // the last raise's size
    uint32_t hand_no = 0;
    uint8_t  level = 1;               // computer skill 0..2
    bool     showdown = false;        // the last hand ended in a showdown (cards shown)

    int32_t pot() const;
    bool    hand_over() const { return street == Street::Over; }
    int     in_hand() const;          // players not folded
    int     can_act() const;          // not folded and not all-in

    // A new hand: button moves, blinds, two cards each
    void new_hand(uint32_t seed);

    // The player to act (to_act)
    int32_t to_call() const;          // chips needed to call
    bool    can_check() const { return to_call() == 0; }
    int32_t min_raise_to() const;     // smallest legal bet / raise total (all-in may be less)
    int32_t max_raise_to() const;     // all in
    void    act(Decision d);          // fold / check / call / bet-raise to d.to / all-in

    Decision decide(Rng& rng) const;  // the computer's choice for to_act
    double   equity(int s, int samples, Rng& rng) const;   // chance seat s wins (ties shared)

    size_t serialize(uint8_t* buf, size_t cap) const;   // "HLD1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kSeats * 32 + 52 + 1 + 5 + 1 + 1 + 1 + 1 + 4 + 4 + 4 + 1 + 1;

    void finish();                    // award the pot(s): the hand is over (public for tests)

private:
    void next_player();               // the next to act, or the next street
    void end_street();
    void post(int s, int32_t amount, Act a);
};

// ---- Play history (one line per hand you were dealt) ---------------------------------------
//   #,Result,Net,Chips
//   12,Won,140,1250
enum class Result : uint8_t { Won = 0, Lost, Folded, Split };
struct Record { Result result = Result::Lost; int32_t net = 0, chips = 0; };
extern const char* const kCsvHeader;
const char* result_name(Result r);
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t hands = 0, won = 0, folded = 0;
    int32_t  net = 0, best_chips = 0, biggest = 0;
    void add(const Record& r);
};

} // namespace holdem
