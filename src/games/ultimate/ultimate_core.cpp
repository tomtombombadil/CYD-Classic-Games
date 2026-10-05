// Ultimate Tic-Tac-Toe rules and computer player. See ultimate_core.h.
#include "ultimate_core.h"

#include <cstring>

namespace ultimate {

namespace {

const uint8_t kLines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

// Small board b's state after a move in it
uint8_t small_state(const uint8_t* cell, int b)
{
    const uint8_t* s = cell + b * 9;
    for (const auto& l : kLines)
        if (s[l[0]] && s[l[0]] == s[l[1]] && s[l[0]] == s[l[2]]) return s[l[0]];
    for (int k = 0; k < 9; ++k) if (!s[k]) return 0;
    return 3;
}

} // namespace

bool Board::can_play(int c) const
{
    if (c < 0 || c >= kCells || cell[c] || result() != -1) return false;
    return board_live(c / 9);
}

bool Board::play(int c)
{
    if (!can_play(c)) return false;
    cell[c] = uint8_t(turn() + 1);
    small[c / 9] = small_state(cell, c / 9);
    const int to = c % 9;
    next = int8_t(board_open(to) ? to : -1);
    last = int8_t(c);
    ++moves;
    return true;
}

int Board::result() const
{
    for (const auto& l : kLines) {
        const uint8_t v = small[l[0]];
        if ((v == 1 || v == 2) && small[l[1]] == v && small[l[2]] == v) return v - 1;
    }
    for (int b = 0; b < 9; ++b) if (!small[b]) return -1;
    return 2;
}

bool Board::winning_line(int* a, int* b) const
{
    for (const auto& l : kLines) {
        const uint8_t v = small[l[0]];
        if ((v == 1 || v == 2) && small[l[1]] == v && small[l[2]] == v) { *a = l[0]; *b = l[2]; return true; }
    }
    return false;
}

int Board::legal(uint8_t* out) const
{
    int n = 0;
    if (result() != -1) return 0;
    for (int b = 0; b < 9; ++b) {
        if (!board_live(b)) continue;
        for (int k = 0; k < 9; ++k) if (!cell[b * 9 + k]) out[n++] = uint8_t(b * 9 + k);
    }
    return n;
}

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "UTT1", 4);
    p += 4;
    // 2 bits a cell, 4 cells a byte
    for (int k = 0; k < 21; ++k) {
        uint8_t v = 0;
        for (int j = 0; j < 4; ++j) {
            const int c = k * 4 + j;
            if (c < kCells) v |= uint8_t(cell[c] << (2 * j));
        }
        *p++ = v;
    }
    *p++ = uint8_t(next);
    *p++ = uint8_t(moves);
    *p++ = uint8_t(moves >> 8);
    *p++ = uint8_t(last);
    return size_t(p - buf);
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "UTT1", 4) != 0) return false;
    Board b;
    const uint8_t* p = buf + 4;
    int xs = 0, os = 0;
    for (int k = 0; k < 21; ++k, ++p)
        for (int j = 0; j < 4; ++j) {
            const int c = k * 4 + j;
            const uint8_t v = uint8_t((*p >> (2 * j)) & 3);
            if (c >= kCells) { if (v) return false; continue; }
            if (v == 3) return false;
            b.cell[c] = v;
            xs += v == 1;
            os += v == 2;
        }
    b.next = int8_t(*p++);
    b.moves = uint16_t(p[0] | p[1] << 8);
    p += 2;
    b.last = int8_t(*p);
    if (b.moves != xs + os || xs - os < 0 || xs - os > 1) return false;
    if (b.next < -1 || b.next > 8 || b.last < -1 || b.last >= kCells) return false;
    if ((b.last < 0) != (b.moves == 0) || (b.last >= 0 && !b.cell[b.last])) return false;
    for (int s = 0; s < 9; ++s) b.small[s] = small_state(b.cell, s);
    if (b.next >= 0 && !b.board_open(b.next)) return false;
    *this = b;
    return true;
}

// ---- Computer ---------------------------------------------------------------------------------

namespace {

struct Search {
    uint32_t nodes = 0, budget = 0;
    volatile bool* stop = nullptr;
    bool out = false;
};

// Lines of three still open for one side in 3 values (a small board, or
// the big board): two of yours and an empty = strong
int line_score(const uint8_t* v, int me, int blocked_value)
{
    int s = 0;
    for (const auto& l : kLines) {
        int mine = 0, theirs = 0, dead = 0;
        for (int k = 0; k < 3; ++k) {
            const uint8_t x = v[l[k]];
            if (x == blocked_value) ++dead;
            else if (x == me) ++mine;
            else if (x) ++theirs;
        }
        if (dead) continue;
        if (!theirs) s += mine == 2 ? 6 : mine == 1 ? 1 : 0;
        if (!mine)   s -= theirs == 2 ? 6 : theirs == 1 ? 1 : 0;
    }
    return s;
}

// From the side to move's view
int evaluate(const Board& b)
{
    const int me = b.turn() + 1;
    int s = 0;
    static const int kWeight[9] = {3, 2, 3, 2, 4, 2, 3, 2, 3};      // centre and corners count more
    for (int k = 0; k < 9; ++k) {
        if (b.small[k] == me) s += 40 * kWeight[k];
        else if (b.small[k] == (me ^ 3)) s -= 40 * kWeight[k];
        else if (!b.small[k]) s += kWeight[k] * line_score(b.cell + k * 9, me, 3);
    }
    s += 30 * line_score(b.small, me, 3);
    // Being sent anywhere is good for the side to move
    if (b.next < 0) s += 20;
    return s;
}

constexpr int kWin = 100000;

int negamax(const Board& b, int depth, int alpha, int beta, Search& S, int ply)
{
    const int r = b.result();
    if (r == 2) return 0;
    if (r >= 0) return -(kWin - ply);                 // the side that just moved won
    if (depth == 0) return evaluate(b);
    if (++S.nodes > S.budget || (S.stop && *S.stop)) { S.out = true; return 0; }
    uint8_t moves[kCells];
    const int n = b.legal(moves);
    int best = -kWin * 2;
    for (int i = 0; i < n; ++i) {
        Board c = b;
        c.play(moves[i]);
        const int v = -negamax(c, depth - 1, -beta, -alpha, S, ply + 1);
        if (S.out) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    return best;
}

} // namespace

int best_move(const Board& b, int level, uint32_t seed, volatile bool* stop)
{
    uint8_t moves[kCells];
    const int n = b.legal(moves);
    if (!n) return -1;
    // Shuffle the root moves by seed so equal moves vary
    for (int i = n - 1; i > 0; --i) {
        seed = seed * 1103515245u + 12345u;
        const int j = int((seed >> 8) % uint32_t(i + 1));
        const uint8_t t = moves[i]; moves[i] = moves[j]; moves[j] = t;
    }
    const int max_depth = level == 0 ? 2 : level == 1 ? 4 : 12;
    Search S;
    S.stop = stop;
    S.budget = level == 2 ? 40000 : 2000000;
    int best = moves[0];
    for (int depth = 1; depth <= max_depth; ++depth) {
        int alpha = -kWin * 2, pick = -1;
        // Search the last best first
        for (int i = 0; i < n; ++i) if (moves[i] == best) { moves[i] = moves[0]; moves[0] = uint8_t(best); break; }
        for (int i = 0; i < n; ++i) {
            Board c = b;
            c.play(moves[i]);
            const int v = -negamax(c, depth - 1, -kWin * 2, -alpha, S, 1);
            if (S.out) break;
            if (v > alpha) { alpha = v; pick = moves[i]; }
        }
        if (S.out) break;
        if (pick >= 0) best = pick;
        if (alpha >= kWin - 100) break;                 // a win found
    }
    return best;
}

} // namespace ultimate
