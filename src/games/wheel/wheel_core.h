// Wheel of CYD: spin the wheel, call letters, solve the phrase. Rules and
// the computer players, written for this project (MIT). Plain C++,
// host-tested. The puzzles are wheel_phrases.cpp (tools/make_phrases.py).
//
// Three rounds, a new puzzle each. On a turn the player may:
// - Spin: the wheel lands on a money wedge, BUST or SKIP. Money: call a
//   consonant; each time it is in the puzzle pays the wedge's value to the
//   player's round money and the turn goes on; not in it, the turn passes.
//   BUST: the round money goes, and the turn. SKIP: the turn passes.
// - Buy a Vowel ($250 of round money): in the puzzle, the turn goes on.
// - Solve: right wins the round - its round money (at least $500) goes to
//   the bank; wrong passes the turn.
// A call that shows the last hidden letter solves it for the caller. Round
// money not banked by a solve is lost at the end of the round. Most money
// banked after three rounds wins. Round r starts with player r.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wheel {

constexpr int kCols = 12, kRows = 4;           // the puzzle board
constexpr int kMaxPlayers = 3;
constexpr int kRounds = 3;
constexpr int kVowelCost = 250;
constexpr int kSolveMin = 500;
constexpr int kWedges = 16;
constexpr int16_t kBust = -1, kSkip = -2;
extern const int16_t kWheel[kWedges];           // wedge values, clockwise from the pointer at rest
constexpr int kMaxPhrases = 1024;               // the played bits in the save

struct Phrase { uint8_t cat; const char* text; };
extern const Phrase kPhrases[];
extern const int kPhraseCount;
extern const char* const kCategories[];
extern const int kCategoryCount;

inline bool is_letter(char c) { return c >= 'A' && c <= 'Z'; }
bool is_vowel(char c);
// Tiles for `text`: pos[i] = row * kCols + col of character i (-1 for a
// space). Greedy wrap at spaces, each row centred, the rows centred in
// kRows. Returns the rows used (0 = doesn't fit).
int wrap(const char* text, int8_t* pos, int cap);

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n) { return int((next() >> 8) % uint32_t(n)); }
};

enum class Phase : uint8_t { Choose = 0, Consonant, RoundOver, Over };

struct Game {
    uint8_t  players = 3;
    uint8_t  round = 0;                 // 0..kRounds-1
    uint8_t  turn = 0;
    Phase    phase = Phase::Choose;
    int16_t  puzzle = 0;
    uint32_t called = 0;                // letters called this round (bit 0 = A)
    int32_t  money[kMaxPlayers] = {};   // this round's
    int32_t  bank[kMaxPlayers] = {};    // banked by solving
    int16_t  value = 0;                 // Consonant: the spin's value per letter
    int8_t   wedge = 0;                 // where the wheel stopped last
    int8_t   round_winner = -1;
    uint16_t turns = 0;                 // turns taken in the game
    uint8_t  played[kMaxPhrases / 8] = {};

    void start(int n_players, Rng& rng);           // a new game
    const char* text() const { return kPhrases[puzzle].text; }
    const char* category() const { return kCategories[kPhrases[puzzle].cat]; }
    bool called_letter(char c) const { return is_letter(c) && ((called >> (c - 'A')) & 1); }
    bool shown(char c) const { return !is_letter(c) || called_letter(c) || phase == Phase::RoundOver || phase == Phase::Over; }
    int  count(char c) const;                      // times c is in the puzzle
    int  hidden() const;                           // letter tiles not shown yet
    int  letters() const;                          // letter tiles
    bool consonants_left() const;                  // an uncalled consonant is in the puzzle
    bool vowels_left() const;
    bool can_spin() const { return phase == Phase::Choose && consonants_left(); }
    bool can_buy() const  { return phase == Phase::Choose && money[turn] >= kVowelCost && vowels_left(); }
    bool can_solve() const { return phase == Phase::Choose; }
    bool can_call(char c) const;                   // Consonant phase: an uncalled consonant
    bool can_buy_letter(char c) const;             // an uncalled vowel, and the money

    int  spin(Rng& rng);                           // the wedge it landed on
    int  call(char c);                             // after a money spin: times found, -1 not allowed
    int  buy(char c);                              // a vowel: times found, -1 not allowed
    // The letters for the hidden tiles, in order; right = round won
    bool solve(const char* letters);
    void next_round(Rng& rng);                     // after RoundOver
    bool over() const { return phase == Phase::Over; }
    int  leader() const;                           // most banked, -1 = a tie at the top

    size_t serialize(uint8_t* buf, size_t cap) const;   // "WHL1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 4 + 2 + 4 + 12 + 12 + 2 + 1 + 1 + 2 + kMaxPhrases / 8;

  private:
    void pass();
    void win_round();
    void pick_puzzle(Rng& rng);
};

// The computer players. A computer never looks at the answer: it sees the
// board, the category and the letters called, like a player. Easy calls
// common letters at random and solves when nearly everything shows;
// Medium calls the most common letters first and solves sooner; Hard
// thinks of every puzzle it knows that fits the board, calls the letter
// most of them have, and solves once only one fits.
enum class Act : uint8_t { Spin, Buy, Solve };
Act  decide(const Game& g, int level, uint32_t seed);
char pick_consonant(const Game& g, int level, uint32_t seed);
char pick_vowel(const Game& g, int level, uint32_t seed);
// Puzzles in the list that fit what the board shows (same category,
// letters and gaps); `pick` (if set) gets one of them chosen by `seed`
int  fitting(const Game& g, uint32_t seed = 0, int* pick = nullptr);
// A computer's solve: the letters for the hidden tiles of its best guess
// (a puzzle that fits), into `out` (kCols * kRows + 1)
void guess_letters(const Game& g, uint32_t seed, char* out, size_t cap);

} // namespace wheel
