#include "tictactoe_core.h"

#include <cstring>

namespace tictactoe {

namespace {

const int kLines[8][3] = {
    {0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6},
};

// Score for the side to move: +10 - ply for a win, 0 draw/unknown
int negamax(Board& b, int depth, int ply, int alpha, int beta)
{
    const int r = b.result();
    if (r == 2) return 0;
    if (r >= 0) return -(10 - ply);      // the side that just moved won
    if (depth == 0) return 0;
    int best = -100;
    for (int i = 0; i < 9; ++i) {
        if (b.cell[i] >= 0) continue;
        b.play(i);
        const int v = -negamax(b, depth - 1, ply + 1, -beta, -alpha);
        b.undo();
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

} // namespace

bool Board::play(int i)
{
    if (!can_play(i)) return false;
    cell[i] = static_cast<int8_t>(turn());
    history[moves++] = static_cast<uint8_t>(i);
    return true;
}

bool Board::undo()
{
    if (!moves) return false;
    cell[history[--moves]] = -1;
    return true;
}

bool Board::winning_line(int a[3]) const
{
    for (const auto& l : kLines)
        if (cell[l[0]] >= 0 && cell[l[0]] == cell[l[1]] && cell[l[1]] == cell[l[2]]) {
            a[0] = l[0]; a[1] = l[1]; a[2] = l[2];
            return true;
        }
    return false;
}

int Board::result() const
{
    int a[3];
    if (winning_line(a)) return cell[a[0]];
    return moves == 9 ? 2 : -1;
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "TTT1", 4);
    buf[4] = moves;
    memcpy(buf + 5, history, 9);
    return kSaveBytes;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "TTT1", 4) != 0 || buf[4] > 9) return false;
    Board t;
    for (int k = 0; k < buf[4]; ++k) if (!t.play(buf[5 + k])) return false;
    *this = t;
    return true;
}

int depth_for_level(int level)
{
    static const int d[3] = {1, 2, 9};
    return d[level < 0 ? 0 : level > 2 ? 2 : level];
}

int best_move(const Board& start, int depth, uint32_t seed)
{
    Board b = start;
    int best = -1000, pick = -1, ties = 0;
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    for (int i = 0; i < 9; ++i) {
        if (b.cell[i] >= 0) continue;
        b.play(i);
        const int v = -negamax(b, depth - 1, 1, -100, 100);   // full window: exact ties
        b.undo();
        if (v > best) { best = v; pick = i; ties = 1; }
        else if (v == best) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            if (rng % ++ties == 0) pick = i;
        }
    }
    return pick;
}

} // namespace tictactoe
