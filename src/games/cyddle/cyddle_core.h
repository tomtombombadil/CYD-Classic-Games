// CYD-dle (a Wordle-style word game) rules. Plain C++, host-tested.
// Guess a five-letter word; each guess marks letters right place (green),
// in the word elsewhere (gold) or not in it (grey). Repeated letters are
// marked the way the word allows (a letter is gold only as many times as
// it is still unaccounted for in the answer).
//
// Levels: Easy 7 guesses, Normal 6, Hard 6 and every hint must be used
// (green letters stay put, gold letters are used again).
#pragma once

#include <cstddef>
#include <cstdint>

namespace cyddle {

constexpr int kLen = 5;
constexpr int kMaxRows = 7;

enum Mark : uint8_t { Unknown = 0, Absent = 1, Present = 2, Correct = 3 };

int  answer_count();
void answer_word(int index, char out[kLen]);        // lowercase, no terminator
bool is_word(const char w[kLen]);                   // accepted as a guess
void score(const char guess[kLen], const char answer[kLen], Mark out[kLen]);

enum class Submit : uint8_t { Ok, TooShort, NotWord, MustUseHints };

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Game {
    uint16_t answer = 0;               // index into the answer list
    uint8_t  level = 1;                // 0 Easy, 1 Normal, 2 Hard
    uint8_t  rows = 0;                 // guesses made
    char     guess[kMaxRows][kLen] = {};
    char     typing[kLen] = {};
    uint8_t  typed = 0;
    uint8_t  played[(2048 + 7) / 8] = {};   // answers already used (so words don't repeat)

    int  max_rows() const { return level == 0 ? 7 : 6; }
    bool solved() const;
    bool over() const { return solved() || rows >= max_rows(); }
    void marks(int row, Mark out[kLen]) const;
    Mark key_mark(char letter) const;  // best mark a letter has had so far
    void type(char letter);            // a-z
    void back();
    Submit submit();
    // A new word for `level`, never one played before until all have been
    void start(int level, Rng& rng);
    void restart() { rows = 0; typed = 0; }

    size_t serialize(uint8_t* buf, size_t cap) const;   // "CYD1" + fields
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 + 1 + 1 + kMaxRows * kLen + kLen + 1 + sizeof played;
};

} // namespace cyddle
