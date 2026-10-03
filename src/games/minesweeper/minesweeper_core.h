// Minesweeper rules and board generator, written for this project (MIT).
// Plain C++, host-tested in tools/host_tests/test_games.cpp.
//
// Board sizes are fixed per level (the same on every CYD, so stats compare)
// and kept small enough for stylus-sized cells on a 240x320 screen:
//   Easy 8x10, 10 mines   Medium 9x11, 15 mines   Hard 10x11, 20 mines
// (cells at least 22 px on 240x320). There is no restart: replaying a board
// whose mines you've seen isn't a game.
//
// Mines are placed on the first tap, never on or next to that cell, so the
// first tap always opens an area. Every board can be cleared by logic alone
// (never a guess): the generator plays it with a solver that uses the
// single-number rules, the subset rule between two numbers and the mine
// count, and retries until the solver clears it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mines {

constexpr int kMaxW = 10, kMaxH = 11, kMaxCells = kMaxW * kMaxH;
constexpr int kLevels = 3;

struct LevelSize { uint8_t w, h, mines; };
extern const LevelSize kSizes[kLevels];

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

enum class Cell : uint8_t { Hidden = 0, Open = 1, Flag = 2 };
enum class Status : uint8_t { Playing = 0, Won = 1, Lost = 2 };

struct Board {
    uint8_t  level = 0, w = 8, h = 10, mines = 10;
    bool     placed = false;               // mines placed (after the first tap)
    Status   status = Status::Playing;
    int16_t  boom = -1;                    // the mine that was hit
    uint16_t moves = 0;                    // taps that opened something
    uint8_t  mine[kMaxCells] = {};         // 1 = mine
    uint8_t  near[kMaxCells] = {};         // mines next to the cell
    Cell     cell[kMaxCells] = {};

    void start(int level);                 // empty board, mines not yet placed
    int  cells() const { return w * h; }
    int  flags() const;
    int  mines_left() const { return mines - flags(); }   // may go negative

    // A tap on a hidden cell opens it (placing the mines first if needed);
    // a tap on an open number whose flags are all set opens its other
    // neighbours ("chord"). Returns true if anything opened.
    bool open(int i, Rng& rng);
    void toggle_flag(int i);               // hidden <-> flag; open cells ignore it

    size_t serialize(uint8_t* buf, size_t cap) const;   // "MIN1" ...
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 8 + kMaxCells * 2;

    // Testing / generator helpers
    void place(int first, Rng& rng);       // no-guess board, `first` opens an area
    int  neighbors(int i, int out[8]) const;
private:
    bool open_one(int i);                  // flood-opens zeros; false = hit a mine
    void finish_if_won();
    void set_counts();
};

// True if a player using logic only can clear `b` (mines placed) starting
// with a tap on `first`.
bool solvable(const Board& b, int first);

} // namespace mines
