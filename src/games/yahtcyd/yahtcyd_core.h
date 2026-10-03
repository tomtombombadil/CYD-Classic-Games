// Yaht-CYD (a Yahtzee-style dice game) rules and play history. Plain C++,
// host-tested.
//
// 13 turns. Each turn: up to three rolls of five dice, holding any dice
// between rolls, then the roll is scored in one empty box. Upper boxes
// (Ones..Sixes) score the matching dice; 63 or more there earns a 35 bonus.
// Lower boxes: 3 / 4 of a Kind (sum of dice), Full House 25, Run of 4
// (small straight) 30, Run of 5 (large straight) 40, Yaht-CYD (five alike)
// 50, Chance (sum). "Run of 4/5" keep the names short enough for the card.
// Each extra Yaht-CYD after a scored 50 earns 100 more and is a joker: it
// must go in its number's upper box if that is empty; otherwise it may go
// anywhere, Full House and the straights counting in full.
#pragma once

#include <cstddef>
#include <cstdint>

namespace yahtcyd {

enum Box : uint8_t {
    Ones, Twos, Threes, Fours, Fives, Sixes,
    ThreeKind, FourKind, FullHouse, SmallStraight, LargeStraight, YahtCyd, Chance,
};
constexpr int kBoxes = 13;
const char* box_name(int box);

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Game {
    uint8_t dice[5] = {1, 2, 3, 4, 5};
    uint8_t held = 0;                  // bit i = die i kept
    uint8_t rolls = 0;                 // rolls this turn (0..3)
    int16_t score[kBoxes];             // -1 = empty
    uint8_t extra = 0;                 // extra Yaht-CYDs (100 each)

    Game() { for (auto& s : score) s = -1; }
    bool over() const;
    int  turn() const;                 // 1..13
    bool can_roll() const { return !over() && rolls < 3; }
    void roll(Rng& rng);
    void toggle_hold(int die);         // only between rolls
    bool can_score(int box) const;
    int  potential(int box) const;     // what scoring this roll there would give
    bool score_box(int box);           // false if not allowed
    int  upper() const;
    int  bonus() const { return upper() >= 63 ? 35 : 0; }
    int  total() const;
    bool is_yaht() const;              // the dice are five alike

    size_t serialize(uint8_t* buf, size_t cap) const;   // "YAH1" + fields
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 5 + 1 + 1 + 2 * kBoxes + 1;
};

// ---- Play history ----------------------------------------------------------------------
//   #,Score,Upper,Bonus,YahtCYDs,Seconds,Time
//   7,245,68,35,1,742,12:22
struct Record {
    uint16_t score = 0, upper = 0, bonus = 0;
    uint8_t  yahts = 0;                // Yaht-CYDs rolled (50 box + extras)
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, best = 0, total = 0, bonuses = 0, yahts = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
    uint32_t average() const { return games ? (total + games / 2) / games : 0; }
};

} // namespace yahtcyd
