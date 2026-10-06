// Deal or No CYD rules, written for this project (MIT). Plain C++,
// host-tested. (The classic briefcase game show; our own name - Tom,
// 2026-10-06.)
//
// 26 cases hide 26 amounts from 1 cent to $1,000,000. You pick one to keep,
// unopened. Then you open the others round by round - 6, 5, 4, 3, 2, then
// one a round - and after each round the Banker makes an offer for your
// case. Deal: you take the offer and the game ends. No Deal: play on.
// With one other case left you may keep yours or swap, and win what's in
// the case you end with.
//
// The Banker offers a share of the average of the amounts still in play:
// small at first (12 %), growing each round (to 92 % in the last), a
// little up or down by chance, rounded to a tidy figure.
#pragma once

#include <cstddef>
#include <cstdint>

namespace dealcyd {

constexpr int kCases = 26, kRounds = 9;
extern const uint32_t kValues[kCases];       // in cents, smallest first
extern const uint8_t  kOpenPerRound[kRounds];
// "$0.01", "$1,000", "$1,000,000"; short = "$750K", "$1M"
void money(char* buf, size_t cap, uint32_t cents, bool short_form = false);

enum class Phase : uint8_t { Pick, Open, Offer, Swap, Done };

struct Game {
    uint8_t  value_of[kCases] = {};  // case i holds kValues[value_of[i]]
    bool     opened[kCases] = {};
    int8_t   mine = -1;              // your case
    uint8_t  round = 0;              // 0..8
    uint8_t  to_open = 0;            // cases still to open this round
    Phase    phase = Phase::Pick;
    uint32_t offer = 0;              // the Banker's offer on the table (cents)
    uint32_t won = 0;                // what you leave with
    int8_t   dealt = -1;             // the round you took a deal in, -1 = no deal
    bool     swapped = false;
    int8_t   last_opened = -1;
    uint32_t offers[kRounds] = {};   // every offer made
    uint32_t rng = 1;

    void start(uint32_t seed);
    bool pick(int c);                // Pick: your case
    bool open(int c);                // Open: another case
    bool deal();                     // Offer: take it
    bool no_deal();                  // Offer: play on
    bool keep_or_swap(bool swap);    // Swap: the last decision
    bool in_play(int value_index) const;   // that amount is still hidden somewhere
    int  left() const;               // cases unopened, yours too
    uint32_t average() const;        // of the amounts in play (cents)
    int  last_case() const;          // Swap: the other unopened case

    size_t serialize(uint8_t* buf, size_t cap) const;    // "DNC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kCases + 4 + 1 + 1 + 1 + 1 + 4 + 4 + 1 + 1 + 1 + 4 * kRounds + 4;

private:
    void make_offer();
    uint32_t rand_next();
};

// ---- Play history ----------------------------------------------------------------------
//   #,Won,Deal Round,Case Held,Seconds,Time
//   3,43000.00,6,750000.00,312,5:12
struct Record {
    uint32_t won = 0;              // cents
    int8_t   dealt = -1;           // the round of the deal (1-9), -1 none
    uint32_t held = 0;             // what your own case held (cents)
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, deals = 0;
    uint64_t total = 0;
    uint32_t best = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
    uint32_t average() const { return games ? uint32_t(total / games) : 0; }
};

} // namespace dealcyd
