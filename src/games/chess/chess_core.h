// Chess rules and computer player, written for this project (MIT). Plain
// C++, host-tested in tools/host_tests/test_games.cpp.
//
// Squares: sq = rank * 8 + file, a1 = 0, h8 = 63. White (side 0) moves
// first. Full rules: castling, en passant, promotion to any piece, check,
// checkmate, stalemate, the 50-move rule, threefold repetition and
// insufficient material.
//
// The computer: iterative-deepening alpha-beta with a capture search at the
// leaves, material + piece-square evaluation. Levels are limited by depth
// and thinking time (never by deliberate blunders); equal moves are broken
// by a seed.
#pragma once

#include <cstddef>
#include <cstdint>

namespace chess {

enum Piece : uint8_t { None = 0, Pawn = 1, Knight = 2, Bishop = 3, Rook = 4, Queen = 5, King = 6 };
// A square holds piece | (side << 3); 0 = empty
inline int  piece_of(uint8_t c) { return c & 7; }
inline int  side_of(uint8_t c)  { return c >> 3; }

struct Move {
    uint8_t from = 0, to = 0;
    uint8_t promo = None;              // piece a pawn becomes, or None
    bool operator==(const Move& o) const { return from == o.from && to == o.to && promo == o.promo; }
};

constexpr int kMaxMoves = 220;
struct MoveList {
    Move m[kMaxMoves];
    int  n = 0;
};

struct Position {
    uint8_t  sq[64] = {};
    uint8_t  side = 0;
    uint8_t  castle = 0;               // 1 = White O-O, 2 = White O-O-O, 4 = Black O-O, 8 = Black O-O-O
    int8_t   ep = -1;                  // en passant target square
    uint8_t  halfmove = 0;             // plies since a capture or pawn move
    uint8_t  king[2] = {4, 60};
    uint64_t hash = 0;

    void start();
    bool attacked(int square, int by) const;
    bool in_check(int s) const { return attacked(king[s], s ^ 1); }
    void make(const Move& m);          // assumes a legal (or pseudo-legal) move
    uint64_t compute_hash() const;
};

void generate(const Position& p, MoveList& out);           // legal moves, side to move
// Legal moves for whichever side owns the piece on `from` (for showing a
// piece's moves on a long-press, even when it's not that side's turn)
void moves_from(const Position& p, int from, MoveList& out);

enum class End : uint8_t { None, Checkmate, Stalemate, FiftyMoves, Repetition, Material, TooLong };

constexpr int kMaxPlies = 600;

struct Game {
    Position pos;
    uint16_t plies = 0;
    uint8_t  history[kMaxPlies] = {};  // index into generate() at each ply
    uint64_t hashes[kMaxPlies + 1] = {};
    Move     last{};
    bool     has_last = false;

    Game() { pos.start(); hashes[0] = pos.hash; }
    int  turn() const { return pos.side; }
    void legal(MoveList& out) const { generate(pos, out); }
    bool play(int index);
    End  end() const;
    // -1 game on, 0/1 winner (checkmate), 2 draw
    int  result() const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "CHS1" + plies + history
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 + kMaxPlies;
};

// Computer move: an index into the legal list. `clock` gives milliseconds
// (thinking time limit); levels: Easy depth 2, Medium depth 3 / 2 s,
// Hard depth 6 / 6 s. About 24 KB of stack.
int best_move(const Game& g, int level, uint32_t seed, uint32_t (*clock)(), volatile bool* stop = nullptr);

const char* end_text(End e);           // "Checkmate", "Stalemate", ...

} // namespace chess
