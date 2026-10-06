// Jeopar-CYD! rules and computer players, written for this project (MIT).
// Plain C++, host-tested. (Tom's name, 2026-10-06, for the classic answer-
// board quiz show.) Questions come from the shared trivia bank
// (trivia_bank.h, Open Trivia DB, CC BY-SA 4.0), multiple choice here.
//
// You against Max and Zoe. Two rounds of six categories x five clues
// ($200-$1,000, then doubled), then one Final clue. Whoever has control
// picks a clue; everyone may buzz in by answering - right adds the value
// and takes control, wrong takes the value off and the others may still
// try. The rows run easy (first two), medium, hard (last two). Hidden
// Daily Doubles (one in round 1, two in round 2) are for the picker alone,
// for a wager of $5 up to their score (or the round's top value if more).
// Final: everyone with money wagers, then answers one hard clue.
//
// The computers never see the right answer: when a clue comes up each one
// is dealt "knows it" by chance (by level and the clue's difficulty) and
// a buzz time; one that doesn't know may still buzz and guess.
#pragma once

#include <cstddef>
#include <cstdint>

namespace jcyd {

constexpr int kCats = 6, kRows = 5, kPlayers = 3, kBankMax = 6144;
enum class Phase : uint8_t { Board, Wager, Clue, Reveal, FinalWager, FinalClue, Over };

struct Cell { int16_t q = -1; uint8_t used = 0, daily = 0; };

struct Plan {                       // a computer's plan for the clue showing
    uint16_t buzz_ms = 0;           // when it buzzes in (0 = it won't)
    int8_t   slot = -1;             // the answer it will give
};

struct Game {
    uint8_t  round = 0;             // 0, 1, then 2 = Final
    uint8_t  level = 1;
    uint8_t  cat[kCats] = {};       // bank categories on the board
    Cell     cell[kCats][kRows];
    int32_t  score[kPlayers] = {};
    uint8_t  chooser = 0;
    Phase    phase = Phase::Board;
    // the clue in play
    int8_t   cur_c = -1, cur_r = -1;
    int16_t  q = -1;
    uint8_t  order[4] = {0, 1, 2, 3};   // display slot k shows bank answer order[k]
    int32_t  wager = 0;                 // a Daily Double's
    uint8_t  tried = 0;                 // players who answered (wrongly) this clue
    uint8_t  wrong_slots = 0;           // answers already given wrongly
    int8_t   answerer = -1;             // who answered last
    int8_t   answer_slot = -1;
    bool     last_right = false;
    Plan     plan[kPlayers];
    // Final
    uint8_t  final_cat = 0;
    int32_t  final_wager[kPlayers] = {};
    int8_t   final_slot[kPlayers] = {-1, -1, -1};
    uint32_t rng = 1;
    uint8_t  played[kBankMax / 8] = {};

    void start(uint32_t seed, int level);
    uint32_t rand_next();
    int32_t value(int r) const { return (r + 1) * 200 * (round == 1 ? 2 : 1); }
    int32_t max_wager(int p) const;
    int  right_slot() const;
    int  clues_left() const;

    bool pick(int c, int r);                // Board -> Clue (or Wager for a Daily Double)
    bool set_wager(int32_t w);              // Wager -> Clue
    bool answer(int p, int slot);           // a buzz-in with an answer
    bool time_up();                         // nobody (else) answers -> Reveal
    bool done_revealing();                  // Reveal -> Board / next round / Final
    bool final_bet(int p, int32_t w);       // FinalWager
    bool final_answer(int p, int slot);     // FinalClue; the last one ends the game
    bool in_final(int p) const { return score[p] > 0; }
    bool final_done() const;
    int  place(int p) const;                // 1 = most money

    // Computers
    void plan_clue();                       // deal each computer its plan for the clue
    void pick_cell(int level, int* c, int* r);
    int32_t cpu_wager(int p) const;         // a Daily Double
    int32_t cpu_final_wager(int p) const;
    int  cpu_final_slot(int p);

    size_t serialize(uint8_t* buf, size_t cap) const;   // "JCY1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 1 + kCats + kCats * kRows * 4 + kPlayers * 4 + 1 + 1 +
                                         1 + 1 + 2 + 4 + 4 + 1 + 1 + 1 + 1 + 1 + kPlayers * 3 + 1 +
                                         kPlayers * 4 + kPlayers + 4 + kBankMax / 8;

private:
    bool deal_round();
    int  take_question(int cat, int difficulty);
    void shuffle_answers();
};

// ---- Stats: "#,Place,Score,Level,Seconds,Time" -----------------------------------------------
struct Record {
    uint8_t  place = 0;
    int32_t  score = 0;
    uint8_t  level = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, wins = 0;
    int32_t  best = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace jcyd
