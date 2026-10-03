#include "pegs_core.h"

#include <cstring>

namespace pegs {

namespace {

const int8_t kOrtho[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
const int8_t kTri[6][2]   = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}, {-1, -1}, {1, 1}};

int sq_of(int r, int c) { return (r < 0 || c < 0 || r >= kN || c >= kN) ? -1 : r * kN + c; }

} // namespace

uint64_t level_holes(int level)
{
    uint64_t h = 0;
    for (int r = 0; r < kN; ++r)
        for (int c = 0; c < kN; ++c) {
            bool on = false;
            const bool mid_r = r >= 2 && r <= 4, mid_c = c >= 2 && c <= 4;
            switch (level) {
                case Triangle: on = r < 5 && c <= r; break;
                case English:  on = mid_r || mid_c; break;
                default:       on = mid_r || mid_c || ((r == 1 || r == 5) && (c == 1 || c == 5)); break;
            }
            if (on) h |= uint64_t(1) << (r * kN + c);
        }
    return h;
}

int level_start_hole(int level)
{
    switch (level) {
        case Triangle: return 0;                 // the top
        case English:  return 3 * kN + 3;        // centre
        default:       return 1 * kN + 3;        // two above the centre (solvable; the centre isn't)
    }
}

int level_dirs(int level, const int8_t (**dirs)[2])
{
    if (level == Triangle) { *dirs = kTri; return 6; }
    *dirs = kOrtho;
    return 4;
}

const char* level_name(int level)
{
    static const char* const n[kLevels] = {"Triangle", "English", "European"};
    return n[level >= 0 && level < kLevels ? level : 1];
}

void Game::start(int lv)
{
    *this = Game{};
    level = static_cast<uint8_t>(lv < 0 || lv >= kLevels ? English : lv);
    holes = level_holes(level);
    pegs = holes & ~(uint64_t(1) << level_start_hole(level));
}

int Game::peg_count() const
{
    int n = 0;
    for (uint64_t p = pegs; p; p &= p - 1) ++n;
    return n;
}

bool Game::find(int from, int to, Move* m) const
{
    if (!peg(from) || !hole(to) || peg(to)) return false;
    const int8_t (*d)[2];
    const int nd = level_dirs(level, &d);
    const int r = from / kN, c = from % kN;
    for (int k = 0; k < nd; ++k) {
        const int over = sq_of(r + d[k][0], c + d[k][1]);
        const int dest = sq_of(r + 2 * d[k][0], c + 2 * d[k][1]);
        if (dest == to && over >= 0 && peg(over)) {
            if (m) *m = Move{uint8_t(from), uint8_t(over), uint8_t(to)};
            return true;
        }
    }
    return false;
}

int Game::legal(Move* out) const
{
    int n = 0;
    const int8_t (*d)[2];
    const int nd = level_dirs(level, &d);
    for (int s = 0; s < kSquares; ++s) {
        if (!peg(s)) continue;
        const int r = s / kN, c = s % kN;
        for (int k = 0; k < nd; ++k) {
            const int over = sq_of(r + d[k][0], c + d[k][1]);
            const int to = sq_of(r + 2 * d[k][0], c + 2 * d[k][1]);
            if (over >= 0 && to >= 0 && peg(over) && hole(to) && !peg(to)) {
                if (out) out[n] = Move{uint8_t(s), uint8_t(over), uint8_t(to)};
                ++n;
            }
        }
    }
    return n;
}

bool Game::can_move_from(int sq) const
{
    if (!peg(sq)) return false;
    for (int t = 0; t < kSquares; ++t) if (find(sq, t)) return true;
    return false;
}

bool Game::stuck() const { return legal(nullptr) == 0; }

bool Game::play(int from, int to)
{
    Move m;
    if (moves >= kMaxMoves || !find(from, to, &m)) return false;
    pegs &= ~(uint64_t(1) << m.from);
    pegs &= ~(uint64_t(1) << m.over);
    pegs |= uint64_t(1) << m.to;
    history[moves][0] = m.from; history[moves][1] = m.over; history[moves][2] = m.to;
    ++moves;
    return true;
}

bool Game::undo()
{
    if (!moves) return false;
    --moves;
    const uint8_t* h = history[moves];
    pegs |= uint64_t(1) << h[0];
    pegs |= uint64_t(1) << h[1];
    pegs &= ~(uint64_t(1) << h[2]);
    return true;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "PEG1", 4); n = 4;
    buf[n++] = level;
    for (int k = 0; k < 8; ++k) buf[n++] = uint8_t(pegs >> (8 * k));
    buf[n++] = moves;
    memcpy(buf + n, history, sizeof history); n += sizeof history;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "PEG1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.level = buf[n++];
    if (g.level >= kLevels) return false;
    g.holes = level_holes(g.level);
    for (int k = 0; k < 8; ++k) g.pegs |= uint64_t(buf[n++]) << (8 * k);
    g.moves = buf[n++];
    memcpy(g.history, buf + n, sizeof g.history);
    if (g.moves > kMaxMoves || (g.pegs & ~g.holes)) return false;
    *this = g;
    return true;
}

} // namespace pegs
