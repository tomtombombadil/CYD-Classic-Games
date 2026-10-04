// Nine Men's Morris rules and computer player, written for this project
// (MIT). Plain C++, host-tested.
//
// 24 points on three nested squares joined at their midpoints:
//    0-----------1-----------2
//    |   3-------4-------5   |
//    |   |   6---7---8   |   |
//    9---10--11      12--13--14
//    |   |   15--16--17  |   |
//    |   18------19------20  |
//    21----------22----------23
// White moves first. Each side has nine men: first they are placed one per
// turn, then moved to a neighbouring empty point. Three in a line (a mill)
// removes one of the other side's men - one not in a mill, unless all of
// them are. A side down to three men may "fly" to any empty point. A side
// left with two men, or with no move, loses. 50 moves each with no man
// removed (after the placing) is a draw.
#pragma once

#include <cstddef>
#include <cstdint>

namespace morris {

constexpr int kPoints = 24;
constexpr int kMen = 9;
constexpr int kMaxMoves = 256;

extern const int8_t kMills[16][3];
extern const uint8_t kX[kPoints], kY[kPoints];       // on a 7 x 7 grid
uint32_t neighbours(int p);                            // bit mask

// A move: from (-1 = placing a man), to, and the man removed (-1 none)
struct Move {
    int8_t from = -1, to = -1, remove = -1;
    int  code() const { return (from + 1) | (to << 5) | ((remove + 1) << 10); }
    static Move decode(int c) { Move m; m.from = int8_t((c & 31) - 1); m.to = int8_t((c >> 5) & 31); m.remove = int8_t(((c >> 10) & 31) - 1); return m; }
};

struct MoveList {
    Move m[kMaxMoves];
    int  n = 0;
};

struct Position {
    uint32_t men[2] = {0, 0};          // bit p: a man on point p
    uint8_t  hand[2] = {kMen, kMen};   // men still to place
    uint8_t  side = 0;                 // to move (0 White)
    uint8_t  quiet = 0;                // plies since a removal, once placing is over

    int  on_board(int s) const { return __builtin_popcount(men[s]); }
    int  left(int s) const { return on_board(s) + hand[s]; }
    int  cell(int p) const { return (men[0] >> p & 1) ? 0 : (men[1] >> p & 1) ? 1 : -1; }
    bool placing() const { return hand[side] > 0; }
    bool flying(int s) const { return hand[s] == 0 && on_board(s) == 3; }
    bool in_mill(int s, int p) const;
    bool makes_mill(int s, int from, int to) const;    // would a man of s arriving at `to` make a mill
    uint32_t removable(int s) const;                   // the men of side s that may be taken
    void apply(const Move& m);
};

void generate(const Position& p, MoveList& out);

struct Game {
    Position pos;
    uint16_t plies = 0;
    Move     last;                     // the last move (to < 0: none)

    Game() { last.to = -1; }
    int  turn() const { return pos.side; }
    void legal(MoveList& out) const { generate(pos, out); }
    bool play(int code);               // a Move::code() from legal()
    int  result() const;               // -1 game on, 0/1 winner, 2 draw

    size_t serialize(uint8_t* buf, size_t cap) const;   // "MOR1" + position
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 8 + 2 + 1 + 1 + 2 + 3;
};

// Computer move (a code) for the side to move. Level 0 looks one move
// ahead, 1 three, 2 deepens within a node budget. Ties go by `seed`.
// Each ply of search keeps a move list on the stack: give it ~20 KB.
int best_move(const Game& g, int level, uint32_t seed, volatile bool* stop = nullptr);

} // namespace morris
