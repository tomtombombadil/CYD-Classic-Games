// The trivia question bank shared by the trivia games (code MIT; the
// questions in trivia_data.cpp are Open Trivia DB, CC BY-SA 4.0 - see that
// file). Plain C++, host-tested.
//
// Questions are numbered 0 .. count()-1. Each has a category (Open Trivia
// DB's 24, short names), a difficulty, and is multiple choice (4 answers)
// or true / false (2). get() unpacks the question's block of 32 into a
// buffer kept until release() - call that when a trivia game closes.
#pragma once

#include <cstddef>
#include <cstdint>

namespace trivia {

constexpr int kCategories = 24;
enum Difficulty : uint8_t { kEasy, kMedium, kHard };

struct Question {
    char    text[320];
    char    answer[4][128];     // answer[0] is the right one (games shuffle them)
    uint8_t answers = 0;        // 4, or 2 for true / false ("True", "False")
    uint8_t category = 0;
    uint8_t difficulty = 0;
};

int  count();
int  category(int i);
int  difficulty(int i);
bool true_false(int i);
const char* category_name(int c);
const char* category_short(int c);   // for tight spots ("Math")
bool get(int i, Question& q);      // false when out of range / no memory / bad data
void release();                    // frees the unpacked block

// For the tests: unpack every block and check the bank against the
// generator's checksum
bool verify();

// The data (trivia_data.cpp)
extern const int      kQuestions, kBlockSize, kBlocks, kMaxBlockText;
extern const uint32_t kChecksum;
extern const uint32_t kBlockOffset[];
extern const uint8_t  kMeta[];
extern const uint8_t  kData[];

} // namespace trivia
