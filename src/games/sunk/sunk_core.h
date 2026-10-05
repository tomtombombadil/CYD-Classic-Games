// You Sunk My CYD! (the classic ships-and-shots game) rules and computer
// player, written for this project (MIT). Plain C++, host-tested.
//
// Each side has a 10x10 sea with five ships: Carrier 5, Battleship 4,
// Cruiser 3, Submarine 3, Destroyer 2. Ships lie across or down and never
// touch, not even at a corner. Sides take turns firing one shot at a cell
// of the other side's sea; a hit is told, and so is a ship sunk (all its
// cells hit). Sink the whole fleet to win.
//
// Moves (also the wireless move keys): the first two plies set the fleets
// - ply 0 is side 0's fleet SEED, ply 1 side 1's - and every later ply is a
// shot, a cell 0..99 (row * 10 + column), side 0 first. A fleet is made
// from its seed by make_fleet(); that function is part of the game's
// wireless version (net_games.h).
//
// Because ships never touch, the cells around a sunk ship are known water:
// they can't be fired at (like a cell fired at already).
#pragma once

#include <cstddef>
#include <cstdint>

namespace sunk {

constexpr int kN = 10;                          // the sea is kN x kN
constexpr int kCells = kN * kN;
constexpr int kShips = 5;
constexpr int kLen[kShips] = {5, 4, 3, 3, 2};
constexpr int kShipCells = 17;
constexpr uint32_t kSeedMax = 0x7FFFFFFFu;      // fleet seeds 0..kSeedMax (fits a move int)
const char* ship_name(int ship);                // "Carrier" ...

struct Ship {
    uint8_t cell = 0;        // top / left cell
    bool    down = false;    // runs down (else across)
    int cell_at(int k, int len) const { return cell + (down ? k * kN : k); }
};

struct Fleet {
    Ship    ship[kShips];
    uint8_t at[kCells] = {};   // ship index + 1 on that cell, 0 = water
};
// The fleet for a seed (deterministic; part of the wireless version)
void make_fleet(uint32_t seed, Fleet& f);

// What a side knows about a cell of the other side's sea
enum Known : uint8_t { kUnknown, kMiss, kHit, kSunk, kClear };

struct Board {
    uint32_t seed[2] = {};
    Fleet    fleet[2];                  // fleet[s] is side s's, valid once its seed is played
    uint8_t  shot[2][kCells] = {};      // shot[s][c]: side s fired at cell c of the other sea
    uint16_t moves = 0;                 // plies: 2 fleets, then shots
    int8_t   last[2] = {-1, -1};        // the last cell each side fired at

    int  turn() const { return moves & 1; }
    bool setup() const { return moves < 2; }
    bool can_play(uint32_t move) const;
    bool play(uint32_t move);
    int  result() const;                // -1 game on, 0/1 winner (no draws)

    bool sunk(int side, int ship) const;           // side's ship is sunk
    int  afloat(int side) const;                   // side's ships still afloat
    // What `shooter` knows about cell c of the other side's sea
    Known known(int shooter, int c) const;
    int  shots(int side) const;

    // Save image: "SNK1" + seeds + moves + last + shot bits (both sides)
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 8 + 2 + 2 + 2 * 13;
};

// Computer move for the side to move: in the setup a fleet seed, then a
// cell. It reads only what that side knows (hits, misses, ships sunk).
// Level 0 hunts at random and tries the cells next to a hit; 1 hunts on a
// spaced pattern and follows a line of hits; 2 counts every way the ships
// still afloat could lie and fires where most of them cross. Ties go by `seed`.
uint32_t best_move(const Board& b, int level, uint32_t seed);

} // namespace sunk
