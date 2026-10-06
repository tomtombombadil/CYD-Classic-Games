// You Sank My CYD! (the classic ships-and-shots game) rules and computer
// player, written for this project (MIT). Plain C++, host-tested.
//
// Each side has a 10x10 sea with five ships: Carrier 5, Battleship 4,
// Cruiser 3, Submarine 3, Destroyer 2. Ships lie across or down and may
// touch, but not overlap (Tom, 2026-10-05: the classic rules - version 1
// kept them apart and marked the water round a sunk ship). Sides take turns firing one shot at a cell
// of the other side's sea; a hit is told, and so is a ship sunk (all its
// cells hit). Sink the whole fleet to win.
//
// Moves (also the wireless move keys): first the fleets, one ship a move -
// plies 0-4 are side 0's ships (Carrier first, Destroyer last), plies 5-9
// side 1's - a ship's key is ship_key(top-left cell, down). Then every ply
// is a shot, a cell 0..99 (row * 10 + column), side 0 first. (Version 1
// sent a whole fleet as one seed; see net_games.h.)
#pragma once

#include <cstddef>
#include <cstdint>

namespace sunk {

constexpr int kN = 10;                          // the sea is kN x kN
constexpr int kCells = kN * kN;
constexpr int kShips = 5;
constexpr int kLen[kShips] = {5, 4, 3, 3, 2};
constexpr int kShipCells = 17;
constexpr uint32_t kSeedMax = 0x7FFFFFFFu;      // make_fleet() seeds
constexpr int kSetupPlies = 2 * kShips;         // the fleets, a ship a ply
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
// A random whole fleet for a seed, ships anywhere they fit (Random placement)
void random_fleet(uint32_t seed, Fleet& f);
// Version 1's fleet for a seed (ships apart): only to load version-1 saves
void make_fleet(uint32_t seed, Fleet& f);
// A ship's move key: its top / left cell and which way it runs
inline uint32_t ship_key(int cell, bool down) { return uint32_t(cell) | (down ? 0x80u : 0u); }
inline uint32_t ship_key(const Ship& s) { return ship_key(s.cell, s.down); }
// Does `ship` fit at `key`: in the sea, on none of the ships already in f?
bool ship_fits(const Fleet& f, int ship, uint32_t key);
void place_ship(Fleet& f, int ship, uint32_t key);   // (fits already checked)
// Every place `ship` fits in f (keys, at most 2 * kCells)
int  ship_places(const Fleet& f, int ship, uint32_t* out);

// What a side knows about a cell of the other side's sea
enum Known : uint8_t { kUnknown, kMiss, kHit, kSunk };

struct Board {
    Fleet    fleet[2];                  // fleet[s] is side s's, ship by ship as they are placed
    uint8_t  shot[2][kCells] = {};      // shot[s][c]: side s fired at cell c of the other sea
    uint16_t moves = 0;                 // plies: 10 ships, then shots
    int8_t   last[2] = {-1, -1};        // the last cell each side fired at

    int  turn() const { return moves < kShips ? 0 : moves < kSetupPlies ? 1 : (moves - kSetupPlies) & 1; }
    bool setup() const { return moves < kSetupPlies; }
    int  placed(int side) const;        // ships side has placed (0..5)
    bool can_play(uint32_t move) const;
    bool play(uint32_t move);
    int  result() const;                // -1 game on, 0/1 winner (no draws)

    bool sunk(int side, int ship) const;           // side's ship is sunk
    int  afloat(int side) const;                   // side's ships still afloat
    // What `shooter` knows about cell c of the other side's sea
    Known known(int shooter, int c) const;
    int  shots(int side) const;

    // Save image: "SNK2" + ship keys (both sides) + moves + last + shot
    // bits (both sides). "SNK1" (a seed a fleet) still loads.
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 * kShips + 2 + 2 + 2 * 13;
    static constexpr size_t kSaveBytesV1 = 4 + 8 + 2 + 2 + 2 * 13;
};

// Computer move for the side to move: in the setup its next ship (anywhere
// it fits), then a cell. It reads only what that side knows (hits, misses, ships sunk).
// Level 0 hunts at random and tries the cells next to a hit; 1 hunts on a
// spaced pattern and follows a line of hits; 2 counts every way the ships
// still afloat could lie and fires where most of them cross. Ties go by `seed`.
uint32_t best_move(const Board& b, int level, uint32_t seed);

} // namespace sunk
