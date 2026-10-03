// RPG Dice: a dice roller for tabletop role-playing games (Tom, 2026-10-03).
// Plain C++, host-tested.
//
// A roll is a Pool: how many of each die (Coin = d2, d4 ... d20, d100) plus
// one modifier, e.g. 2d6 + 1d8 + 3. Every die's result is kept so the
// screen can show each one, and the total. A Preset is a saved, named set
// of up to four labelled pools rolled together with one tap - e.g. a
// fighter's two attacks: Hit 1d20+7, Damage 1d8+5, Hit 1d20+4,
// Damage 1d6+5. Rolls go into a history (newest first) as short text.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rpgdice {

enum Die : uint8_t { Coin = 0, D4, D6, D8, D10, D12, D20, D100, kDieTypes };
constexpr int kSides[kDieTypes] = {2, 4, 6, 8, 10, 12, 20, 100};
const char* die_name(int die);              // "Coin", "d4", ... "d100"

constexpr int kMaxDice = 20;                // dice in one pool
constexpr int kMaxMod = 99;

// Labels a preset line can have (tap to cycle in the editor)
constexpr int kLabels = 7;
const char* label_name(int label);          // "Roll", "Hit", "Damage", ...

struct Pool {
    uint8_t count[kDieTypes] = {};
    int8_t  mod = 0;
    uint8_t label = 0;

    int  dice() const;
    bool empty() const { return dice() == 0; }
    bool add(int die);                      // false when the pool is full
    void bump_mod(int d);                   // +/- 1, kept within +-kMaxMod
    void clear() { *this = Pool{}; }
    // "2d6 + 1d8 + 3", "Coin", "d20 - 1"; "No dice" when empty
    size_t format(char* out, size_t cap) const;
};

// One pool's result: the dice in a fixed order (by die type), each value
struct Rolled {
    uint8_t  n = 0;
    uint8_t  die[kMaxDice] = {};
    uint8_t  value[kMaxDice] = {};          // 1..sides (d100: 1..100; Coin: 1 tails, 2 heads)
    int8_t   mod = 0;
    uint8_t  label = 0;
    int16_t  total = 0;
};

// Small, good-enough generator (xorshift32); seeded per roll from the board
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n);                       // uniform 0..n-1, no bias
};

void roll(const Pool& p, Rng& rng, Rolled& out);

constexpr int kPresets = 8;
constexpr int kPresetLines = 4;
constexpr int kNameLen = 16;                // incl. the terminating 0

struct Preset {
    char    name[kNameLen] = "";
    uint8_t lines = 0;                      // 0 = empty slot
    Pool    pool[kPresetLines];
};

constexpr int kHistory = 40;
constexpr int kHistoryText = 112;

// The roller's saved state
struct State {
    Pool    pool;                           // being built / last rolled
    int8_t  preset = -1;                    // >= 0: the last roll was this preset
    uint8_t shown = 0;                      // lines in `last`
    Rolled  last[kPresetLines];             // what the screen shows
    Preset  presets[kPresets];
    uint8_t hist_n = 0, hist_head = 0;      // ring of history texts
    char    hist[kHistory][kHistoryText] = {};

    // Roll the pool / a preset; fills `last` and adds a history line
    void roll_pool(Rng& rng);
    void roll_preset(int index, Rng& rng);
    const char* history(int k) const;       // k = 0 newest; nullptr past the end
    void clear_history() { hist_n = hist_head = 0; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "RPD1" ...
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4096 + kHistory * kHistoryText;
};

// One result as history text: "2d6+3: 4 5 +3 = 12", "Hit 1d20+7: 14 +7 = 21"
size_t format_rolled(const Rolled& r, char* out, size_t cap, bool with_label);

} // namespace rpgdice
