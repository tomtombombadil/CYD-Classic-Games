// Mancala (Kalah: six pits a side, four seeds each) rules and computer
// player, written for this project (MIT). Plain C++, host-tested.
//
// pit[0..5]  side 0's pits, in sowing order      pit[6]  side 0's store
// pit[7..12] side 1's pits, in sowing order      pit[13] side 1's store
// A move takes every seed from one of your pits and sows them one by one
// into the following pits and your own store, skipping the other store.
// - The last seed in your store: you move again.
// - The last seed in an empty pit of yours, with seeds opposite: that seed
//   and the opposite pit's seeds go to your store.
// - When either side's pits are all empty the game ends; the other side
//   puts the seeds left on its side into its own store. Most seeds wins.
#pragma once

#include <cstddef>
#include <cstdint>

namespace mancala {

constexpr int kPits = 6;
constexpr int kStore[2] = {6, 13};
constexpr int kStart = 4;                       // seeds per pit at the start

inline int pit_index(int side, int p) { return side * 7 + p; }        // p 0..5
inline int opposite(int i) { return 12 - i; }                           // pits only

// What a move did, for the screen: where each seed went, and any capture
struct Sowing {
    uint8_t from = 0;               // pit index sown from
    uint8_t n = 0;                  // seeds sown
    uint8_t path[48] = {};          // pit index of each seed, in order
    int8_t  captured_from = -1;     // the opposite pit taken (-1 none)
    uint8_t captured = 0;           // seeds moved to the store by the capture (both pits)
    bool    again = false;          // ended in the mover's store: moves again
    bool    ended = false;          // the move ended the game (sides swept)
};

struct Board {
    uint8_t  pit[14] = {kStart, kStart, kStart, kStart, kStart, kStart, 0,
                        kStart, kStart, kStart, kStart, kStart, kStart, 0};
    uint8_t  side = 0;              // to move
    uint16_t moves = 0;             // moves played (an extra turn counts as a move)
    int8_t   last_from = -1;        // pit index of the last move (-1 none)

    bool can_play(int p) const { return p >= 0 && p < kPits && pit[pit_index(side, p)] > 0; }
    bool play(int p, Sowing* how = nullptr);   // p 0..5 of the side to move
    bool over() const;              // a side has no seeds left in its pits
    int  result() const;            // -1 game on, 0/1 winner, 2 draw
    int  seeds_on_side(int s) const;

    // Save image: "MNC1" + pits + side + moves + last
    size_t serialize(uint8_t* buf, size_t cap) const;
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 14 + 1 + 2 + 1;
};

// Computer move (0..5) for the side to move. Level 0 looks one move ahead
// (counting extra turns), 1 searches 5 moves, 2 searches deeper within a
// node budget (about a second on the board). Ties go by `seed`.
int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace mancala
