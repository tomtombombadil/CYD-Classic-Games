// Pipe Race rules, written for this project (MIT). Plain C++, host-tested in
// tools/host_tests/test_games.cpp. (The classic pipe-laying race; our own
// name - Tom, 2026-10-06.)
//
// An 8x8 grid holds a start tank. Pipe pieces come from a queue of five:
// across, up-down, four bends and a cross. A tap on an empty square lays
// the next piece there; a tap on a pipe the water hasn't reached swaps it
// for the next piece (-50). After a countdown the water leaves the tank and
// runs through the pipes, one square at a time, faster each level. When it
// runs out of pipe - into an empty square, a wall, a rock or a pipe that
// doesn't fit - the level ends: if it went through at least the level's
// goal of pipes, the next level comes; otherwise the game is over.
//
// Score: 100 a pipe filled (200 while flowing fast), +400 for water crossing
// a cross the second way, -50 a swapped pipe, -50 a laid pipe the water
// never reached when the level ends. Go starts the water at once, or makes
// it run fast.
#pragma once

#include <cstddef>
#include <cstdint>

namespace piperace {

constexpr int kCols = 8, kRows = 8, kCells = kCols * kRows, kQueue = 5;

// Sides of a square, as bits
constexpr uint8_t kN = 1, kE = 2, kS = 4, kW = 8;
inline uint8_t opposite(uint8_t side) { return uint8_t(side & 0x3 ? side << 2 : side >> 2); }

enum Piece : uint8_t {
    kEmpty = 0,
    kAcross, kUpDown, kNE, kES, kSW, kWN, kCross,     // pipes (1..7)
    kStartN, kStartE, kStartS, kStartW,                // the tank and its outlet (8..11)
    kRock,                                             // nothing goes here (12)
    kPieces
};
inline bool is_pipe(uint8_t p) { return p >= kAcross && p <= kCross; }
inline bool is_start(uint8_t p) { return p >= kStartN && p <= kStartW; }
uint8_t sides(uint8_t piece);      // the sides a piece opens to

enum class Phase : uint8_t { Waiting, Flowing, Passed, Over };

// What advance() saw happen (bits)
constexpr uint32_t kEvFlow = 1, kEvFilled = 2, kEvCrossBonus = 4, kEvPassed = 8, kEvOver = 16;

struct Game {
    uint8_t  cell[kCells] = {};
    uint8_t  fill[kCells] = {};    // bit 0: filled (a cross: across), bit 1: a cross filled up-down
    uint8_t  queue[kQueue] = {};   // queue[0] is laid next
    uint8_t  level = 1;
    Phase    phase = Phase::Waiting;
    int32_t  score = 0;
    uint16_t pipes = 0;            // filled this level
    uint16_t total = 0;            // filled this game
    uint32_t wait_ms = 0;          // left before the water starts
    int8_t   head = -1;            // the square the water is in
    uint8_t  in = 0;               // the side it came in by (0 = the tank)
    uint16_t head_ms = 0;          // time spent in that square so far
    bool     fast = false;
    uint32_t rng = 1;

    void start(uint32_t seed);     // level 1
    void next_level();             // after Passed
    int      goal() const;         // pipes to fill this level
    uint32_t wait_total() const;   // countdown this level
    uint32_t cell_ms() const;      // time for the water to cross one square
    uint32_t head_total() const;   // time for the square it is in (half for the tank, faster when fast)
    int      start_cell() const;

    // A tap on square c: 1 laid, 2 swapped, 0 nothing (rock, tank, water there, level over)
    int  tap(int c);
    bool can_tap(int c) const;
    void go();                     // start the water now, or make it run fast
    // Time passes: returns kEv* bits
    uint32_t advance(uint32_t ms);
    // How far (0..1) the water is through the head square
    float progress() const { const uint32_t t = head_total(); return t ? float(head_ms) / float(t) : 1.0f; }

    size_t serialize(uint8_t* buf, size_t cap) const;    // "PRC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 * kCells + kQueue + 1 + 1 + 4 + 2 + 2 + 4 + 1 + 1 + 2 + 1 + 4;

private:
    uint8_t random_piece();
    void    end_level();
    uint32_t rand_next();
};

// ---- Play history ----------------------------------------------------------------------
//   #,Score,Level,Pipes,Seconds,Time
//   3,5150,6,58,640,10:40
struct Record {
    int32_t  score = 0;
    uint16_t level = 0;            // the level the game ended on
    uint16_t pipes = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, best_level = 0;
    int32_t  best = 0;
    int64_t  total = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
    int32_t average() const { return games ? int32_t(total / int64_t(games)) : 0; }
};

} // namespace piperace
