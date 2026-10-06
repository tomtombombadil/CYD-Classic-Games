// Who Wants To Be A CYD? rules, written for this project (MIT). Plain C++,
// host-tested. (Tom's name, 2026-10-06, for the classic million-dollar
// quiz show.) Questions come from the shared trivia bank (trivia_bank.h,
// Open Trivia DB, CC BY-SA 4.0).
//
// Fifteen multiple-choice questions up a money ladder from $100 to
// $1,000,000: 1-5 easy, 6-10 medium, 11-15 hard. $1,000 (question 5) and
// $32,000 (question 10) are safe: a wrong answer drops you to the last
// safe amount you passed. You may walk away with what you have instead
// of answering. Three lifelines, once a game each:
//   50:50     - two wrong answers go;
//   Ask the Audience - a poll (the audience knows easy questions better);
//   Phone a Friend   - a friend says which answer they think it is, and
//                      how sure they are (and is sometimes wrong).
// Questions don't repeat until every question of that level was played.
#pragma once

#include <cstddef>
#include <cstdint>

namespace whowants {

constexpr int kSteps = 15;
extern const int32_t kPrize[kSteps];
int32_t safe_amount(int step);          // what you keep if wrong at `step` (0-based)

enum class Phase : uint8_t { Asking, Locked, Right, Over };
enum Lifeline : uint8_t { kFifty = 1, kAudience = 2, kPhone = 4 };

constexpr int kBankMax = 6144;          // played bits kept for up to this many questions

struct Game {
    uint8_t  step = 0;                  // the question being asked (0..14)
    int16_t  q = -1;                    // its number in the trivia bank
    uint8_t  order[4] = {0, 1, 2, 3};   // display slot k shows bank answer order[k] (0 = right)
    uint8_t  hidden = 0;                // display slots taken away by 50:50
    uint8_t  used = 0;                  // lifelines used this game
    uint8_t  shown = 0;                 // lifelines used on this question (their results show)
    uint8_t  poll[4] = {};              // the audience's percent for each slot
    int8_t   friend_pick = -1;          // the friend's slot
    uint8_t  friend_sure = 0;           // 0 guess, 1 think so, 2 sure
    int8_t   chosen = -1;               // the slot you answered
    Phase    phase = Phase::Asking;
    int32_t  won = 0;                   // when Over: what you take home
    bool     walked = false;
    uint32_t rng = 1;
    uint8_t  played[kBankMax / 8] = {};

    // `fits(q)` (may be null) says whether question q fits the screen
    void start(uint32_t seed, bool (*fits)(int) = nullptr);
    uint32_t rand_next();
    bool  next_question(bool (*fits)(int) = nullptr);   // after Right: up a step
    int   right_slot() const;           // the slot with the right answer
    bool  lock(int slot);               // Asking -> Locked (the screen holds a moment)
    bool  reveal();                     // Locked -> Right / Over
    bool  walk_away();
    bool  use(Lifeline l);
    int32_t banked() const { return step == 0 ? 0 : kPrize[step - 1]; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "WWC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 2 + 4 + 1 + 1 + 1 + 4 + 1 + 1 + 1 + 1 + 4 + 1 + 4 + kBankMax / 8;

private:
    bool pick(int level, bool (*fits)(int));
};

// ---- Stats: "#,Won,Reached,Seconds,Time" ------------------------------------------------------
struct Record {
    int32_t  won = 0;
    uint8_t  reached = 0;     // questions answered right
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, millions = 0;
    int32_t  best = 0;
    int64_t  total = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace whowants
