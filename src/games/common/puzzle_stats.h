// Play history for the solo puzzles (Sliding Tiles, Light Switch, ...).
// Plain C++ (host-tested). Sudoku keeps its own older format.
//
//   #,Level,Result,Moves,Seconds,Time,Par
//   3,4x4,Solved,112,185,3:05,0
// Par = fewest moves possible where the game knows it (else 0).
// Result: Solved, Gave up (left for a new game) or Lost (the game ended
// against the player: a mine hit, out of guesses).
#pragma once

#include <cstddef>
#include <cstdint>

namespace puzzle {

constexpr int kLevels = 3;

struct Record {
    uint8_t  level   = 0;          // 0..2
    bool     solved  = true;       // false = left unsolved for a new game ("Gave up")
    bool     lost    = false;      // with solved = false: the game was lost ("Lost")
    uint16_t moves   = 0;
    uint16_t par     = 0;
    uint32_t seconds = 0;
};

extern const char* const kCsvHeader;
// level_names: the game's three level names ("3x3", "Easy", ...)
size_t format_body(char* buf, size_t cap, const Record& r, const char* const level_names[kLevels]);
bool   parse_line(const char* line, Record& out, const char* const level_names[kLevels]);

constexpr int kRecent = 6;

struct Summary {
    uint32_t solved[kLevels] = {};
    uint32_t gave_up[kLevels] = {};
    uint32_t lost[kLevels] = {};
    uint32_t best_s[kLevels] = {};      // 0 = none
    uint32_t best_moves[kLevels] = {};  // 0 = none
    Record   recent[kRecent];
    int      recent_n = 0, recent_head = 0;
    uint32_t total = 0;
    void add(const Record& r);
    const Record& newest(int i) const;  // 0 = newest
};

} // namespace puzzle
