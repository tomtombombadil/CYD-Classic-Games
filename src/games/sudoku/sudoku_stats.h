// Sudoku play history: one record per finished or abandoned puzzle, and the
// summary shown on the Stats screen. Plain C++ (tested on the PC); the
// device code in src/app/stats_store.* decides where the file lives
// (SD /CYD-Classic-Games/sudoku.csv or LittleFS /stats/sudoku.csv).
//
// File format: CSV, one line per record, readable in Excel:
//   #,Difficulty,Result,Seconds,Time,Hints
//   12,Medium,Solved,512,8:32,0
#pragma once

#include <cstddef>
#include <cstdint>

namespace sudoku {
namespace stats {

enum class Result : uint8_t { Solved = 0, GaveUp = 1 };

struct Record {
    uint8_t  difficulty = 0;          // sudoku::Difficulty value, 0..3
    Result   result     = Result::Solved;
    uint8_t  hints      = 0;
    uint32_t seconds    = 0;
};

struct PerDifficulty {
    uint32_t solved   = 0;
    uint32_t gave_up  = 0;
    uint64_t solved_s = 0;            // total time of solved puzzles
    uint32_t best_s   = 0;            // 0 = none yet
};

constexpr int kRecent = 8;

struct Summary {
    PerDifficulty level[4];
    Record   recent[kRecent];         // ring buffer
    int      recent_n    = 0;         // how many are valid (<= kRecent)
    int      recent_head = 0;         // index of the next write
    uint32_t total       = 0;         // records seen

    void add(const Record& r);
    // i = 0 is the newest
    const Record& newest(int i) const;
    uint32_t average_s(int difficulty) const;   // 0 if no solves
};

extern const char* const kCsvHeader;  // first line of the file, with newline

// Write "seq,Difficulty,Result,Seconds,M:SS,Hints\n". Returns length or 0.
// format_body writes the same without "seq," (the stats store numbers lines).
size_t format_line(char* buf, size_t cap, uint32_t seq, const Record& r);
size_t format_body(char* buf, size_t cap, const Record& r);
// Parse one line (header and malformed lines return false).
bool   parse_line(const char* line, Record& out);

// "8:32" / "1:02:03"
void   format_time(char* buf, size_t cap, uint32_t seconds);
const char* result_name(Result r);

} // namespace stats
} // namespace sudoku
