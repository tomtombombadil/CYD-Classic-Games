#include "hollywood_core.h"

#include <cstring>
#include "../common/trivia_bank.h"

namespace hcyd {

const char* const kStars[kSquares] = {
    "Captain Comet", "Professor Puzzle", "Granny Gizmo",
    "DJ Dazzle", "Chef Noodle", "Madame Mystic",
    "Coach Thunder", "Doctor Doodle", "Sir Giggles",
};

namespace {
const int8_t kLines[8][3] = {{0, 1, 2}, {3, 4, 5}, {6, 7, 8}, {0, 3, 6}, {1, 4, 7}, {2, 5, 8}, {0, 4, 8}, {2, 4, 6}};

bool line_for(const int8_t* own, int side)
{
    for (const auto& l : kLines) if (own[l[0]] == side && own[l[1]] == side && own[l[2]] == side) return true;
    return false;
}

int count_of(const int8_t* own, int side)
{
    int n = 0;
    for (int i = 0; i < kSquares; ++i) n += own[i] == side;
    return n;
}
} // namespace

Board::Board() { for (auto& o : owner) o = -1; }

uint32_t Board::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Board::reset(uint32_t seed)
{
    uint8_t keep[sizeof played];
    memcpy(keep, played, sizeof keep);
    *this = Board{};
    memcpy(played, keep, sizeof keep);
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
}

int Board::count(int side) const { return count_of(owner, side); }

bool Board::wins_with(int side, int sq) const
{
    int8_t o[kSquares];
    memcpy(o, owner, sizeof o);
    o[sq] = int8_t(side);
    return line_for(o, side) || count_of(o, side) >= 5;
}

// A fresh question: mostly easy and medium, now and then a hard one
void Board::ask()
{
    const int n = trivia::count() < kBankMax ? trivia::count() : kBankMax;
    const uint32_t r = rand_next() % 10;
    const int want = r < 5 ? trivia::kEasy : r < 9 ? trivia::kMedium : trivia::kHard;
    for (int pass = 0; pass < 2; ++pass) {
        int count = 0;
        for (int i = 0; i < n; ++i) if (trivia::difficulty(i) == want && !(played[i / 8] >> (i % 8) & 1)) ++count;
        if (count) {
            int k = int(rand_next() % uint32_t(count));
            for (int i = 0; i < n; ++i) {
                if (trivia::difficulty(i) != want || (played[i / 8] >> (i % 8) & 1)) continue;
                if (k-- == 0) { q = int16_t(i); played[i / 8] |= uint8_t(1 << (i % 8)); break; }
            }
            break;
        }
        for (int i = 0; i < n; ++i) if (trivia::difficulty(i) == want) played[i / 8] &= uint8_t(~(1 << (i % 8)));
    }
    // the star answers: right 60 % of the time, else a bluff
    const int answers = trivia::true_false(q) ? 2 : 4;
    star_says = rand_next() % 100 < 60 ? 0 : int8_t(1 + rand_next() % uint32_t(answers - 1));
}

bool Board::can_play(int m) const
{
    if (winner >= 0) return false;
    if (phase == Phase::Pick) return m >= 0 && m < kSquares && owner[m] < 0;
    if (phase == Phase::Judge) return m == kAgree || m == kDisagree;
    return false;
}

bool Board::play(int m)
{
    if (!can_play(m)) return false;
    ++moves;
    if (phase == Phase::Pick) {
        square = int8_t(m);
        ask();
        phase = Phase::Judge;
        return true;
    }
    const int me = turn, other = turn ^ 1;
    const bool right = (m == kAgree) == star_right();
    last_square = square;
    last_side = int8_t(me);
    last_right = right ? 1 : 0;
    if (right) { owner[square] = int8_t(me); last_got = int8_t(me); }
    else if (!wins_with(other, square)) { owner[square] = int8_t(other); last_got = int8_t(other); }
    else last_got = -1;                                   // a winning square must be earned
    for (int s = 0; s < 2; ++s) if (line_for(owner, s) || count(s) >= 5) winner = int8_t(s);
    if (winner < 0) {
        // nothing open: the one with more squares wins
        bool open = false;
        for (int8_t o : owner) open = open || o < 0;
        if (!open) winner = int8_t(count(0) > count(1) ? 0 : 1);
    }
    square = -1;
    phase = winner >= 0 ? Phase::Over : Phase::Pick;
    turn = uint8_t(other);
    return true;
}

// ---- Computer -------------------------------------------------------------------------------------
int best_move(const Board& b, int level, uint32_t seed)
{
    uint32_t r = seed ? seed : 1;
    auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; };
    const int me = b.turn, other = me ^ 1;
    if (b.phase == Phase::Judge) {
        static const int kKnows[3] = {40, 55, 70};
        int knows = kKnows[level < 0 ? 0 : level > 2 ? 2 : level];
        if (b.q >= 0 && trivia::difficulty(b.q) == trivia::kHard) knows -= 15;
        if (int(rnd() % 100) < knows) return b.star_right() ? kAgree : kDisagree;
        return kAgree;                                     // not knowing: trust the star (right 60 %)
    }
    int open[kSquares], n = 0;
    for (int i = 0; i < kSquares; ++i) if (b.owner[i] < 0) open[n++] = i;
    if (!n) return 0;
    if (level == 0) return open[rnd() % uint32_t(n)];
    for (int k = 0; k < n; ++k) if (b.wins_with(me, open[k])) return open[k];   // a square that wins
    for (int k = 0; k < n; ++k) if (b.wins_with(other, open[k])) return open[k];   // take theirs away
    if (b.owner[4] < 0) return 4;
    const int corners[4] = {0, 2, 6, 8};
    int c[4], cn = 0;
    for (int k : corners) if (b.owner[k] < 0) c[cn++] = k;
    if (cn) return c[rnd() % uint32_t(cn)];
    return open[rnd() % uint32_t(n)];
}

// ---- Save ---------------------------------------------------------------------------------------
size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "HCY1", 4); n = 4;
    for (int8_t o : owner) buf[n++] = uint8_t(o);
    buf[n++] = turn;
    buf[n++] = uint8_t(phase);
    buf[n++] = uint8_t(square);
    buf[n++] = uint8_t(q); buf[n++] = uint8_t(uint16_t(q) >> 8);
    buf[n++] = uint8_t(star_says);
    buf[n++] = uint8_t(winner);
    buf[n++] = uint8_t(last_square);
    buf[n++] = uint8_t(last_side);
    buf[n++] = uint8_t(last_got);
    buf[n++] = last_right;
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    memcpy(buf + n, played, sizeof played); n += sizeof played;
    return n;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "HCY1", 4) != 0) return false;
    Board g;
    size_t n = 4;
    for (int8_t& o : g.owner) { o = int8_t(buf[n++]); if (o < -1 || o > 1) return false; }
    g.turn = buf[n++];
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.square = int8_t(buf[n++]);
    g.q = int16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.star_says = int8_t(buf[n++]);
    g.winner = int8_t(buf[n++]);
    g.last_square = int8_t(buf[n++]);
    g.last_side = int8_t(buf[n++]);
    g.last_got = int8_t(buf[n++]);
    g.last_right = buf[n++];
    g.moves = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (!g.rng) g.rng = 1;
    memcpy(g.played, buf + n, sizeof g.played);
    if (g.turn > 1 || g.square < -1 || g.square >= kSquares || g.q < -1 || g.q >= trivia::count()) return false;
    if (g.star_says < 0 || g.star_says > 3 || g.winner < -1 || g.winner > 1) return false;
    if (g.last_square < -1 || g.last_square >= kSquares || g.last_side < -1 || g.last_side > 1 || g.last_got < -1 || g.last_got > 1) return false;
    if (g.phase == Phase::Judge && (g.q < 0 || g.square < 0)) return false;
    if (g.q >= 0 && g.star_says >= (trivia::true_false(g.q) ? 2 : 4)) return false;
    *this = g;
    return true;
}

} // namespace hcyd
