#include "twenty48_core.h"

#include <cstdio>
#include <cstring>

namespace twenty48 {

namespace {

// Cell index of step k (0 = nearest the wall) along line `line` for a
// slide in direction d.
int at(Dir d, int line, int k)
{
    switch (d) {
        case Dir::Left:  return line * kSize + k;
        case Dir::Right: return line * kSize + (kSize - 1 - k);
        case Dir::Up:    return k * kSize + line;
        case Dir::Down:  return (kSize - 1 - k) * kSize + line;
    }
    return 0;
}

void add_tile(Game& g, Rng& rng)
{
    int empty[kCells], n = 0;
    for (int i = 0; i < kCells; ++i) if (!g.cell[i]) empty[n++] = i;
    if (!n) { g.spawned = -1; return; }
    const int i = empty[rng.next() % n];
    g.cell[i] = (rng.next() % 10 == 0) ? 2 : 1;
    g.spawned = static_cast<int8_t>(i);
}

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

} // namespace

void Game::start(Rng& rng)
{
    *this = Game{};
    add_tile(*this, rng);
    add_tile(*this, rng);
    spawned = -1;
}

bool Game::can_slide(Dir d) const
{
    for (int line = 0; line < kSize; ++line)
        for (int k = 1; k < kSize; ++k) {
            const uint8_t here = cell[at(d, line, k)], ahead = cell[at(d, line, k - 1)];
            if (here && (!ahead || ahead == here)) return true;
        }
    return false;
}

bool Game::slide(Dir d, Rng& rng, uint8_t* merged_to)
{
    if (merged_to) *merged_to = 0;
    if (!can_slide(d)) return false;
    for (int line = 0; line < kSize; ++line) {
        uint8_t in[kSize], out[kSize] = {};
        int n = 0;
        for (int k = 0; k < kSize; ++k) {
            const uint8_t v = cell[at(d, line, k)];
            if (v) in[n++] = v;
        }
        int o = 0;
        for (int k = 0; k < n; ++k) {
            if (k + 1 < n && in[k] == in[k + 1]) {
                const uint8_t e = static_cast<uint8_t>(in[k] + 1);
                out[o++] = e;
                score += value(e);
                if (merged_to && e > *merged_to) *merged_to = e;
                ++k;
            } else {
                out[o++] = in[k];
            }
        }
        for (int k = 0; k < kSize; ++k) cell[at(d, line, k)] = out[k];
    }
    ++moves;
    if (max_exp() >= kWinExp) won = true;
    add_tile(*this, rng);
    return true;
}

bool Game::over() const
{
    for (int d = 0; d < 4; ++d)
        if (can_slide(static_cast<Dir>(d))) return false;
    return true;
}

uint8_t Game::max_exp() const
{
    uint8_t m = 0;
    for (int i = 0; i < kCells; ++i) if (cell[i] > m) m = cell[i];
    return m;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "T48A", 4); n = 4;
    memcpy(buf + n, cell, kCells); n += kCells;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(score >> (8 * k));
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    buf[n++] = uint8_t(spawned);
    buf[n++] = won ? 1 : 0;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "T48A", 4) != 0) return false;
    Game g;
    size_t n = 4;
    for (int i = 0; i < kCells; ++i) {
        if (buf[n + i] > 17) return false;               // 2^17 is the most a 4x4 can hold
        g.cell[i] = buf[n + i];
    }
    n += kCells;
    for (int k = 0; k < 4; ++k) g.score |= uint32_t(buf[n++]) << (8 * k);
    g.moves = uint16_t(buf[n] | (buf[n + 1] << 8)); n += 2;
    g.spawned = static_cast<int8_t>(buf[n++]);
    if (g.spawned < -1 || g.spawned >= kCells) g.spawned = -1;
    g.won = buf[n++] != 0;
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Score,Best Tile,Moves,Seconds,Time";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%lu,%lu,%u,%lu,%s\n", (unsigned long)r.score, (unsigned long)r.tile,
                           (unsigned)r.moves, (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, score = 0, tile = 0, secs = 0;
    unsigned moves = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%lu,%lu,%u,%lu,%15[^,\r\n]", &seq, &score, &tile, &moves, &secs, tm) != 6)
        return false;
    out.score = static_cast<uint32_t>(score);
    out.tile = static_cast<uint32_t>(tile);
    out.moves = static_cast<uint16_t>(moves);
    out.seconds = static_cast<uint32_t>(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    total += r.score;
    if (r.score > best) best = r.score;
    if (r.tile > best_tile) best_tile = r.tile;
    wins += r.tile >= 2048;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace twenty48
