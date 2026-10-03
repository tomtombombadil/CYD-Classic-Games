// What every two-player game shares, rules side: how it is being played
// (vs computer, pass-and-play, wireless), computer levels, and the play
// history format. Plain C++ (host-tested in tools/host_tests/test_common.cpp).
//
// Stats file, one line per finished game:
//   #,Mode,Level,Result,Moves,Seconds,Time
//   4,Computer,Hard,Lost,21,95,1:35
//   5,Pass and play,-,Red won,30,240,4:00
#pragma once

#include <cstddef>
#include <cstdint>

namespace twoplayer {

enum class Mode : uint8_t { Computer = 0, PassAndPlay = 1, Wireless = 2 };
enum class Level : uint8_t { Easy = 0, Medium = 1, Hard = 2 };
constexpr int kLevels = 3;

const char* mode_name(Mode m);       // "Computer", "Pass and play", "Wireless"
const char* level_name(Level l);     // "Easy", "Medium", "Hard"

// Who won, from side 1's view. vs Computer, side 1 is the player.
enum class Result : uint8_t { Side1 = 0, Side2 = 1, Draw = 2 };

struct Record {
    Mode     mode    = Mode::Computer;
    Level    level   = Level::Easy;      // vs Computer only
    Result   result  = Result::Draw;
    uint16_t moves   = 0;
    uint32_t seconds = 0;
};

// Names the game gives its two sides, e.g. {"Red", "Yellow"} or {"X", "O"}
struct Sides {
    const char* side1;
    const char* side2;
};

extern const char* const kCsvHeader;
// Without the "seq," prefix (the stats store adds it). Returns length or 0.
size_t format_body(char* buf, size_t cap, const Record& r, const Sides& s);
// Whole line "seq,...". False for the header and anything malformed.
bool   parse_line(const char* line, Record& out, const Sides& s);

struct Summary {
    // vs computer, per level: player won / lost / drew
    uint32_t won[kLevels] = {}, lost[kLevels] = {}, drawn[kLevels] = {};
    // pass-and-play (and wireless): side 1 won / side 2 won / draws
    uint32_t side1 = 0, side2 = 0, draws = 0;
    uint32_t total = 0;
    void add(const Record& r);
};

void format_time(char* buf, size_t cap, uint32_t seconds);   // "8:32", "1:02:03"

} // namespace twoplayer
