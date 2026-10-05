// You Sunk My CYD! rules and computer player. See sunk_core.h.
#include "sunk_core.h"

#include <cstring>

namespace sunk {

namespace {

const char* const kNames[kShips] = {"Carrier", "Battleship", "Cruiser", "Submarine", "Destroyer"};

// The fleet generator's own random numbers (xorshift32): never change them,
// they decide where a seed's ships go on both boards
struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed * 2654435761u ^ 0x9E3779B9u) { if (!s) s = 1; }
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int below(int n) { return int((next() >> 8) % uint32_t(n)); }
};

// Version 1's spacing rule (ships never touched): kept only so make_fleet()
// still makes the same fleets for version-1 saves
bool fits_apart(const Fleet& f, int r, int c, int len, bool down)
{
    const int r2 = down ? r + len - 1 : r, c2 = down ? c : c + len - 1;
    if (r2 >= kN || c2 >= kN) return false;
    for (int y = r - 1; y <= r2 + 1; ++y)
        for (int x = c - 1; x <= c2 + 1; ++x)
            if (y >= 0 && y < kN && x >= 0 && x < kN && f.at[y * kN + x]) return false;
    return true;
}

// The rules: in the sea, not on another ship (ships may touch)
bool fits(const Fleet& f, int r, int c, int len, bool down)
{
    const int r2 = down ? r + len - 1 : r, c2 = down ? c : c + len - 1;
    if (r2 >= kN || c2 >= kN) return false;
    for (int k = 0; k < len; ++k)
        if (f.at[(down ? r + k : r) * kN + (down ? c : c + k)]) return false;
    return true;
}

} // namespace

const char* ship_name(int ship) { return ship >= 0 && ship < kShips ? kNames[ship] : "?"; }

bool ship_fits(const Fleet& f, int ship, uint32_t key)
{
    if (ship < 0 || ship >= kShips || key > 0xFF || (key & 0x7F) >= uint32_t(kCells)) return false;
    const int cell = int(key & 0x7F);
    return fits(f, cell / kN, cell % kN, kLen[ship], (key & 0x80) != 0);
}

void place_ship(Fleet& f, int ship, uint32_t key)
{
    f.ship[ship].cell = uint8_t(key & 0x7F);
    f.ship[ship].down = (key & 0x80) != 0;
    for (int k = 0; k < kLen[ship]; ++k) f.at[f.ship[ship].cell_at(k, kLen[ship])] = uint8_t(ship + 1);
}

int ship_places(const Fleet& f, int ship, uint32_t* out)
{
    int n = 0;
    for (int down = 0; down < 2; ++down)
        for (int c = 0; c < kCells; ++c)
            if (ship_fits(f, ship, ship_key(c, down != 0))) out[n++] = ship_key(c, down != 0);
    return n;
}

void random_fleet(uint32_t seed, Fleet& f)
{
    f = Fleet{};
    for (int i = 0; i < kShips; ++i) {
        uint32_t places[2 * kCells];
        const int n = ship_places(f, i, places);
        seed = seed * 1103515245u + 12345u;
        place_ship(f, i, places[(seed >> 8) % uint32_t(n)]);       // largest first: always room
    }
}

void make_fleet(uint32_t seed, Fleet& f)
{
    Rng r(seed);
    for (;;) {
        memset(f.at, 0, sizeof f.at);
        bool ok = true;
        for (int i = 0; i < kShips && ok; ++i) {
            ok = false;
            for (int tries = 0; tries < 200; ++tries) {
                const bool down = r.next() & 0x100;
                const int span = kN - kLen[i] + 1;
                const int row = down ? r.below(span) : r.below(kN);
                const int col = down ? r.below(kN) : r.below(span);
                if (!fits_apart(f, row, col, kLen[i], down)) continue;
                f.ship[i].cell = uint8_t(row * kN + col);
                f.ship[i].down = down;
                for (int k = 0; k < kLen[i]; ++k) f.at[f.ship[i].cell_at(k, kLen[i])] = uint8_t(i + 1);
                ok = true;
                break;
            }
        }
        if (ok) return;                               // else start the fleet over
    }
}

// ---- Board ------------------------------------------------------------------------------------

int Board::placed(int side) const
{
    const int n = int(moves) - side * kShips;
    return n < 0 ? 0 : n > kShips ? kShips : n;
}

bool Board::sunk(int side, int ship) const
{
    if (placed(side) <= ship) return false;           // not placed yet
    const Ship& s = fleet[side].ship[ship];
    for (int k = 0; k < kLen[ship]; ++k)
        if (!shot[side ^ 1][s.cell_at(k, kLen[ship])]) return false;
    return true;
}

int Board::afloat(int side) const
{
    int n = 0;
    for (int i = 0; i < kShips; ++i) n += !sunk(side, i);
    return n;
}

Known Board::known(int shooter, int c) const
{
    const int target = shooter ^ 1;
    if (setup()) return kUnknown;
    const Fleet& f = fleet[target];
    if (shot[shooter][c]) {
        if (!f.at[c]) return kMiss;
        return sunk(target, f.at[c] - 1) ? kSunk : kHit;
    }
    return kUnknown;
}

int Board::shots(int side) const
{
    int n = 0;
    for (int c = 0; c < kCells; ++c) n += shot[side][c];
    return n;
}

bool Board::can_play(uint32_t move) const
{
    if (result() != -1) return false;
    if (setup()) return ship_fits(fleet[turn()], placed(turn()), move);
    return move < uint32_t(kCells) && known(turn(), int(move)) == kUnknown;
}

bool Board::play(uint32_t move)
{
    if (!can_play(move)) return false;
    const int side = turn();
    if (setup()) {
        place_ship(fleet[side], placed(side), move);
    } else {
        shot[side][move] = 1;
        last[side] = int8_t(move);
    }
    ++moves;
    return true;
}

int Board::result() const
{
    if (setup()) return -1;
    for (int s = 0; s < 2; ++s)
        if (afloat(s) == 0) return s ^ 1;
    return -1;
}

// ---- Save -------------------------------------------------------------------------------------

size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "SNK2", 4);
    p += 4;
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < kShips; ++i) *p++ = i < placed(s) ? uint8_t(ship_key(fleet[s].ship[i])) : 0;
    *p++ = uint8_t(moves);
    *p++ = uint8_t(moves >> 8);
    *p++ = uint8_t(last[0]);
    *p++ = uint8_t(last[1]);
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 13; ++k) {
            uint8_t v = 0;
            for (int b = 0; b < 8; ++b) {
                const int c = k * 8 + b;
                if (c < kCells && shot[s][c]) v |= uint8_t(1 << b);
            }
            *p++ = v;
        }
    return size_t(p - buf);
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    const bool v1 = len >= kSaveBytesV1 && memcmp(buf, "SNK1", 4) == 0;
    if (!v1 && (len < kSaveBytes || memcmp(buf, "SNK2", 4) != 0)) return false;
    Board b;
    const uint8_t* p = buf + 4;
    uint8_t keys[2][kShips] = {};
    uint32_t seeds[2] = {};
    if (v1) {
        for (int s = 0; s < 2; ++s) {
            seeds[s] = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
            p += 4;
            if (seeds[s] > kSeedMax) return false;
        }
    } else {
        for (int s = 0; s < 2; ++s)
            for (int i = 0; i < kShips; ++i) keys[s][i] = *p++;
    }
    int moves = p[0] | p[1] << 8;
    p += 2;
    if (v1) moves = moves >= 2 ? moves + kSetupPlies - 2 : moves * kShips;   // a seed was a whole fleet
    b.last[0] = int8_t(p[0]);
    b.last[1] = int8_t(p[1]);
    p += 2;
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < 13; ++k, ++p)
            for (int bit = 0; bit < 8; ++bit) {
                const int c = k * 8 + bit;
                if (!(*p & (1 << bit))) continue;
                if (c >= kCells) return false;
                b.shot[s][c] = 1;
            }
    if (moves > kSetupPlies + 2 * kCells) return false;
    // The fleets: every ship where it fits, in order
    for (int s = 0; s < 2; ++s) {
        Fleet whole;
        if (v1) make_fleet(seeds[s], whole);
        const int n = moves - s * kShips < 0 ? 0 : moves - s * kShips > kShips ? kShips : moves - s * kShips;
        for (int i = 0; i < n; ++i) {
            const uint32_t key = v1 ? ship_key(whole.ship[i]) : keys[s][i];
            if (!ship_fits(b.fleet[s], i, key)) return false;
            place_ship(b.fleet[s], i, key);
        }
    }
    b.moves = uint16_t(moves);
    // The shots must match the moves: side 0 fires first, after both fleets
    const int fired = b.moves > kSetupPlies ? b.moves - kSetupPlies : 0;
    if (b.shots(0) != (fired + 1) / 2 || b.shots(1) != fired / 2) return false;
    for (int s = 0; s < 2; ++s) {
        if (b.last[s] < -1 || b.last[s] >= kCells) return false;
        if (b.shots(s) ? (b.last[s] < 0 || !b.shot[s][b.last[s]]) : b.last[s] != -1) return false;
    }
    *this = b;
    return true;
}

// ---- Computer ---------------------------------------------------------------------------------

namespace {

struct Pick {
    uint32_t seed;
    int best = -1;
    long score = -1;
    int ties = 0;
    // Highest score wins; equal scores share the choice evenly (reservoir)
    void offer(int c, long s)
    {
        if (s < score) return;
        if (s > score) { score = s; best = c; ties = 1; return; }
        ++ties;
        seed = seed * 1103515245u + 12345u;
        if ((seed >> 8) % uint32_t(ties) == 0) best = c;
    }
};

bool on_sea(int r, int c) { return r >= 0 && r < kN && c >= 0 && c < kN; }

// Unsunk hits next to cell c (across and down)
int hit_neighbours(const Known* k, int c)
{
    const int r = c / kN, col = c % kN;
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    int n = 0;
    for (int d = 0; d < 4; ++d)
        if (on_sea(r + dr[d], col + dc[d]) && k[(r + dr[d]) * kN + col + dc[d]] == kHit) ++n;
    return n;
}

// Level 1: a line of two or more hits - the cells at its ends
long line_end_score(const Known* k, int c)
{
    const int r = c / kN, col = c % kN;
    long best = 0;
    const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
    for (int d = 0; d < 4; ++d) {
        int run = 0;
        for (int y = r + dr[d], x = col + dc[d]; on_sea(y, x) && k[y * kN + x] == kHit; y += dr[d], x += dc[d]) ++run;
        if (run > best) best = run;
    }
    return best;
}

} // namespace

uint32_t best_move(const Board& b, int level, uint32_t seed)
{
    if (b.setup()) {
        // The next ship anywhere it fits (largest first: there is always room)
        const int side = b.turn(), ship = b.placed(side);
        uint32_t places[2 * kCells];
        const int n = ship_places(b.fleet[side], ship, places);
        if (!n) return 0;
        seed = seed * 2654435761u ^ (seed >> 15);
        return places[(seed >> 8) % uint32_t(n)];
    }
    const int me = b.turn(), them = me ^ 1;
    Known k[kCells];
    bool any_hit = false;
    for (int c = 0; c < kCells; ++c) {
        k[c] = b.known(me, c);
        any_hit |= k[c] == kHit;
    }
    int smallest = 5;
    for (int i = 0; i < kShips; ++i)
        if (!b.sunk(them, i) && kLen[i] < smallest) smallest = kLen[i];
    Pick p{seed};
    if (level <= 1) {
        for (int c = 0; c < kCells; ++c) {
            if (k[c] != kUnknown) continue;
            long s = 1;
            if (any_hit) {
                s = 10 * hit_neighbours(k, c);
                if (level == 1) s += 100 * (line_end_score(k, c) >= 2 ? line_end_score(k, c) : 0);
                if (s == 0) continue;
            } else if (level == 1 && (c / kN + c % kN) % smallest != 0) {
                s = 0;                                    // off the pattern: only if nothing else is left
            }
            p.offer(c, s);
        }
        if (p.best < 0)                                   // hits nobody can follow up (a line ends at both sides)
            for (int c = 0; c < kCells; ++c) if (k[c] == kUnknown) p.offer(c, 1);
        return uint32_t(p.best < 0 ? 0 : p.best);
    }
    // Level 2: every way each ship still afloat could lie; with unsunk hits,
    // only the ways that cross them, weighted by how many they cross
    long score[kCells] = {};
    for (int i = 0; i < kShips; ++i) {
        if (b.sunk(them, i)) continue;
        const int len = kLen[i];
        for (int down = 0; down < 2; ++down)
            for (int r = 0; r < (down ? kN - len + 1 : kN); ++r)
                for (int c = 0; c < (down ? kN : kN - len + 1); ++c) {
                    int hits = 0;
                    bool ok = true;
                    for (int j = 0; j < len && ok; ++j) {
                        const Known v = k[(r + (down ? j : 0)) * kN + c + (down ? 0 : j)];
                        if (v == kHit) ++hits;
                        else if (v != kUnknown) ok = false;
                    }
                    if (!ok || (any_hit && !hits)) continue;
                    const long w = any_hit ? 1L << (3 * hits) : 1;
                    for (int j = 0; j < len; ++j) score[(r + (down ? j : 0)) * kN + c + (down ? 0 : j)] += w;
                }
    }
    for (int c = 0; c < kCells; ++c)
        if (k[c] == kUnknown) p.offer(c, score[c]);
    return uint32_t(p.best < 0 ? 0 : p.best);
}

} // namespace sunk
