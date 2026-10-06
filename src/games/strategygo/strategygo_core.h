// Strategy Go! rules and computer player, written for this project (MIT).
// Plain C++, host-tested. (The classic hidden-army game; our own name -
// Tom, 2026-10-06.)
//
// A 10x10 board with two 2x2 lakes in the middle. Each side has 40 pieces,
// set up hidden in its own four rows: Marshal (10), General (9), 2 Colonels
// (8), 3 Majors (7), 4 Captains (6), 4 Lieutenants (5), 4 Sergeants (4),
// 5 Miners (3), 8 Scouts (2), the Spy (S), 6 Bombs (B) and the Flag (F).
// Red moves first. A move: one piece one square across or up/down (not into
// a lake or onto its own side); a Scout goes any distance in a straight
// line over empty squares and may strike at the end. Bombs and the Flag
// never move. Moving onto an enemy is a battle: both pieces are shown and
// the higher rank wins (equal: both go). The Spy beats the Marshal when the
// Spy strikes; a Miner defuses a Bomb; anything else that hits a Bomb is
// lost. Capture the Flag - or leave the other side with no move - to win.
// A piece may not go back and forth between the same two squares more
// than five times in a row.
//
// Moves (also the wireless keys): plies 0-39 are Red's setup, 40-79
// Blue's, a piece a ply - setup_key(cell, rank). Then a move is
// move_key(from, to) = from | to << 7. A wireless board receives the other
// army's setup; the screen never shows it before the end.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sgo {

constexpr int kN = 10, kCells = kN * kN, kArmy = 40, kSetupPlies = 2 * kArmy;
constexpr int kMaxPlies = 2000;             // a game this long is a draw
constexpr int kShuttle = 5;                 // the two-squares rule

enum Rank : uint8_t {
    kFlag = 0, kSpy = 1, kScout = 2, kMiner = 3, kSergeant = 4, kLieutenant = 5, kCaptain = 6,
    kMajor = 7, kColonel = 8, kGeneral = 9, kMarshal = 10, kBomb = 11, kRanks = 12
};
extern const uint8_t kCount[kRanks];        // pieces of each rank in an army
const char* rank_name(int r);               // "Marshal", ...
const char* rank_short(int r);              // "10", ..., "S", "B", "F"

inline bool lake(int c) { const int r = c / kN, k = c % kN; return (r == 4 || r == 5) && (k == 2 || k == 3 || k == 6 || k == 7); }
inline bool home(int side, int c) { return side == 0 ? c >= 60 : c < 40; }   // Red: rows 6-9, Blue: rows 0-3
inline bool mobile(int r) { return r != kFlag && r != kBomb; }

inline uint32_t setup_key(int cell, int rank) { return uint32_t(cell) | uint32_t(rank) << 7; }
inline uint32_t move_key(int from, int to) { return uint32_t(from) | uint32_t(to) << 7; }
inline int key_from(uint32_t k) { return int(k & 127); }
inline int key_to(uint32_t k) { return int((k >> 7) & 127); }

struct Square {
    int8_t  side = -1;           // -1 empty
    uint8_t rank = 0;
    bool    shown = false;       // its rank is known to the other side (it fought)
    bool    moved = false;       // it has moved (so it's no Bomb or Flag)
};

enum Outcome : uint8_t { kNoBattle, kAttackerWins, kDefenderWins, kBothLost, kFlagTaken, kBombDefused, kBombHit };

struct Battle {
    int8_t  from = -1, to = -1, side = -1;   // the attacker's squares and side
    uint8_t attacker = 0, defender = 0;      // ranks
    Outcome outcome = kNoBattle;
};

struct Army { uint8_t rank_at[kArmy]; };     // a setup: ranks in the side's 40 home squares, nearest the back first

struct Board {
    Square   sq[kCells];
    uint16_t moves = 0;
    int8_t   winner = -1;                    // 0 / 1, 2 = draw
    int8_t   last_from[2] = {-1, -1}, last_to[2] = {-1, -1};
    uint8_t  shuttle[2] = {};
    Battle   last;                           // the last move's battle (outcome kNoBattle if none)
    int8_t   last_move_from = -1, last_move_to = -1;
    uint8_t  lost[2][kRanks] = {};           // pieces each side has lost, by rank (every lost piece was seen)

    int  turn() const { return moves < kArmy ? 0 : moves < kSetupPlies ? 1 : (moves - kSetupPlies) & 1; }
    bool setup() const { return moves < kSetupPlies; }
    int  placed(int side) const;             // setup pieces in
    int  left(int side, int rank) const;     // setup: pieces of that rank still to place
    bool can_play(uint32_t key) const;
    bool play(uint32_t key);
    int  result() const { return winner; }
    int  alive(int side) const;
    int  moves_for(int side, uint32_t* out, int cap) const;   // legal moves (keys) for `side`
    // Squares a piece on `from` may go to (its side to move or not)
    int  targets(int from, uint8_t* out) const;

    // Save image: "SGO1" + squares (a byte each) + moves + winner + rules
    // state + last battle + last move (lost pieces are worked out again)
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kCells + 2 + 1 + 2 + 2 + 2 + 6 + 2;
};

// A sensible random setup for a side: the Flag on the back row behind
// Bombs, Scouts toward the front, Miners toward the back, the rest spread.
void random_army(uint32_t seed, Army& out);
// Where piece i of a side's setup goes (i = 0..39, the back row first)
int  army_cell(int side, int i);

// Computer move for the side to move: in the setup the next piece of its
// army (from `seed`), then a move. It reads only what that side may know:
// its own pieces, the other side's pieces it has seen in battle, and which
// of theirs have moved. All look one move ahead, weighing battles by the
// chances of what an unseen piece may be. Level 0 weighs material only;
// 1 also the threats from pieces of theirs it has seen; 2 also from unseen
// ones, by their chances.
uint32_t best_move(const Board& b, int level, uint32_t seed);

} // namespace sgo
