// Farkle: a push-your-luck dice game for two (you and the computer, or
// pass-and-play), written for this project (MIT). Plain C++, host-tested.
//
// Roll six dice. Set aside at least one scoring die (or set), then either
// roll the rest or bank your turn's points. A roll that scores nothing is a
// "Farkle": the turn's points are lost. Score with all six set aside ("hot
// dice") and you may roll all six again. First to 10,000 - the other player
// then gets one last turn to beat it.
// Scoring (a common set): a 1 = 100, a 5 = 50; three of a kind = 100 x the
// face (three 1s = 1000); four of a kind 1000, five 2000, six 3000; a 1-6
// straight 1500, three pairs 1500, four of a kind with a pair 1500, two
// triplets 2500. Dice only count in the roll they were thrown in.
#pragma once

#include <cstddef>
#include <cstdint>

namespace farkle {

constexpr int kDice = 6;
constexpr int32_t kTarget = 10000;

// Score of exactly these dice, every one of them scoring; -1 if any die
// doesn't count (0 for no dice)
int score_exact(const uint8_t* values, int n);
// The best-scoring set among `values` (mask of dice to keep); 0 = Farkle
int best_set(const uint8_t* values, int n, uint8_t* mask = nullptr);

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int die() { uint32_t v; do v = next(); while (v >= 0xFFFFFFFCu); return int(v % 6) + 1; }
};

enum class Phase : uint8_t { Start = 0, Rolled, Farkle, Over };

struct Game {
    int32_t score[2] = {0, 0};
    uint8_t turn = 0;                   // whose turn: 0 first player, 1 second
    int32_t turn_score = 0;             // banked-able points from earlier rolls this turn
    uint8_t dice[kDice] = {1, 2, 3, 4, 5, 6};
    uint8_t live = 0;                   // dice thrown in the last roll (bit i)
    uint8_t picked = 0;                 // dice the player has set aside from that roll
    uint8_t kept = 0;                   // dice set aside earlier this turn
    Phase   phase = Phase::Start;
    int8_t  last_turn_for = -1;         // >= 0: someone reached the target; this player has one last turn
    uint8_t winner = 0;
    uint16_t turns = 0;

    int  picked_score() const;          // -1 when the picked dice don't all score
    bool can_roll() const;              // a roll now: start of turn, or picked dice score
    bool can_bank() const;              // picked dice score (and the turn has points)
    int  dice_to_roll() const;          // how many a roll would throw
    void toggle(int i);                 // pick / unpick a die from the last roll
    bool roll(Rng& rng);
    bool bank();
    void next_turn();                   // after a Farkle has been shown
    bool over() const { return phase == Phase::Over; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "FRK1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 8 + 1 + 4 + 6 + 1 + 1 + 1 + 1 + 1 + 1 + 2;
};

// The computer's turn step: which dice to pick from the roll, and then
// roll on (true) or bank. Easy banks at 300 with simple keeps; Medium
// banks by dice left; Hard weighs the chance of a Farkle against what a
// roll is worth, and the scores.
struct Plan { uint8_t pick; bool roll_on; };
Plan plan(const Game& g, int level);

} // namespace farkle
