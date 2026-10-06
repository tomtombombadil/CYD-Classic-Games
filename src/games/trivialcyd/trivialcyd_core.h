// Trivial CYD rules and computer players, written for this project (MIT).
// Plain C++, host-tested. (Tom's name, 2026-10-06, for the classic
// wedge-collecting trivia board game.) Questions come from the shared
// trivia bank (trivia_bank.h, Open Trivia DB, CC BY-SA 4.0), grouped in
// six colours: Geography, Entertainment, History, Arts & Literature,
// Science & Nature, Sports & Leisure.
//
// A track of 36 squares round the board: every sixth square is a
// colour's Headquarters (HQ), halfway between two HQs is a Roll Again
// square, the rest are colours. On your turn roll the die and move that
// many squares either way (tap where to land). Answer a question in the
// colour you land on: right = roll again (and on an HQ you haven't won,
// that colour's wedge); wrong = the next player's turn. With all six
// wedges, your next turn is one question in a colour the others pick: get
// it right to win. Two to four players: you against computers, or people
// passing the board.
#pragma once

#include <cstddef>
#include <cstdint>

namespace tcyd {

constexpr int kSquares = 36, kColors = 6, kMaxPlayers = 4, kBankMax = 6144;
constexpr int kRollAgain = -1;
enum class Phase : uint8_t { Roll, Move, Ask, Reveal, Over };

extern const char* const kColorNames[kColors];
int  color_of(int bank_category);         // the colour a bank category belongs to
int  square_color(int sq);                // 0..5, or kRollAgain
bool is_hq(int sq);

struct Game {
    uint8_t  players = 3;
    uint8_t  people = 1;                  // players 0..people-1 are people
    uint8_t  level = 1;
    uint8_t  pos[kMaxPlayers] = {};
    uint8_t  wedges[kMaxPlayers] = {};    // bit per colour
    uint8_t  turn = 0;
    Phase    phase = Phase::Roll;
    uint8_t  die = 0;
    int8_t   dest[2] = {-1, -1};          // where the roll can land
    int16_t  q = -1;                      // the question
    uint8_t  q_color = 0;
    uint8_t  order[4] = {0, 1, 2, 3};
    uint8_t  answers = 4;
    bool     final_q = false;             // the winning question
    int8_t   answered = -1;               // the slot answered
    bool     right = false;
    bool     won_wedge = false;
    int8_t   winner = -1;
    uint16_t turns = 0;
    uint32_t rng = 1;
    uint8_t  played[kBankMax / 8] = {};

    void start(uint32_t seed, int players, int people, int level);
    uint32_t rand_next();
    bool human(int p) const { return p < people; }
    int  wedge_count(int p) const;
    int  right_slot() const;

    bool roll();                          // Roll -> Move (or Ask: the final question)
    bool move(int sq);                    // Move -> Ask (or Roll for Roll Again)
    bool answer(int slot);                // Ask -> Reveal
    bool next();                          // Reveal -> Roll (same or next player) / Over

    // Computers: where to move (the best of dest[]), and the answer
    int  cpu_move() const;
    int  cpu_answer();

    size_t serialize(uint8_t* buf, size_t cap) const;   // "TCY1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 1 + 1 + 1 + kMaxPlayers * 2 + 1 + 1 + 1 + 2 + 2 + 1 + 4 + 1 + 1 + 1 +
                                         1 + 1 + 1 + 2 + 4 + kBankMax / 8;

private:
    void ask(int color);
};

// ---- Stats: "#,Place,Wedges,Level,Seconds,Time" ----------------------------------------------
struct Record {
    uint8_t  place = 0;
    uint8_t  wedges = 0;
    uint8_t  level = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, wins = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace tcyd
