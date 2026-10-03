// Checkers (American / English draughts) rules and computer player.
// Plain C++, host-tested.
//
// 8x8, pieces on the dark squares (sq = rank * 8 + file, rank 0 at the
// bottom; a1 is dark). Black (side 0) starts on ranks 0-2 and moves first,
// up the board; White starts on ranks 5-7. Men move one step diagonally
// forward, kings both ways. Jumping is compulsory and a jump continues while
// the same piece can keep jumping; the player may choose which sequence.
// A man reaching the far rank becomes a king and the move ends there.
// A side with no legal move loses. 80 plies (40 moves each) with no
// capture and no man moving is a draw.
#pragma once

#include <cstddef>
#include <cstdint>

namespace checkers {

constexpr int kMaxPath = 10;           // landing squares in one move
constexpr int kMaxMoves = 48;            // more than any real position needs
constexpr int kMaxPlies = 400;

struct Move {
    uint8_t  from = 0;
    uint8_t  n = 0;                    // landing squares in `path`
    uint8_t  path[kMaxPath] = {};
    uint64_t captured = 0;             // squares jumped over
    uint8_t  to() const { return path[n - 1]; }
    bool     jump() const { return captured != 0; }
};

struct MoveList {
    Move m[kMaxMoves];
    int  n = 0;
};

struct Position {
    uint64_t men[2] = {0, 0};
    uint64_t kings[2] = {0, 0};
    uint8_t  side = 0;
    uint8_t  quiet = 0;                // plies since a capture or a man moved

    void     start();
    uint64_t pieces(int s) const { return men[s] | kings[s]; }
    uint64_t occupied() const    { return pieces(0) | pieces(1); }
    // -1 empty, else side; *king tells if it's a king
    int      cell(int sq, bool* king = nullptr) const;
    void     apply(const Move& mv);
};

// Legal moves for side `s` in position p (jumps only, when any exist)
void generate(const Position& p, int s, MoveList& out);

struct Game {
    Position pos;
    uint16_t plies = 0;
    uint8_t  history[kMaxPlies] = {};   // index into generate() at each ply

    Game() { pos.start(); }
    int  turn() const { return pos.side; }
    void legal(MoveList& out) const { generate(pos, pos.side, out); }
    bool play(int index);              // index into legal()
    // -1 game on, 0/1 winner, 2 draw (quiet rule or the game got too long)
    int  result() const;
    int  last_from = -1, last_to = -1;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "CHK1" + plies + history
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 + kMaxPlies;
};

// Computer move (an index into the legal list). Levels search 2, 5, 7 plies
// (jump sequences searched on to the end, up to 24 plies). Ties go by
// `seed`. Needs about 32 KB of stack (move lists live on it).
int best_move(const Game& g, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace checkers
