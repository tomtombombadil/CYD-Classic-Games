#include "memory_core.h"

#include <cstring>

namespace memory {

void level_size(int level, int* cols, int* rows)
{
    static const uint8_t sizes[kLevels][2] = {{4, 4}, {4, 5}, {5, 6}};
    const int l = level < 0 ? 0 : level >= kLevels ? kLevels - 1 : level;
    *cols = sizes[l][0];
    *rows = sizes[l][1];
}

void Game::start(int lv, Rng& rng)
{
    *this = Game{};
    level = static_cast<uint8_t>(lv < 0 ? 0 : lv >= kLevels ? kLevels - 1 : lv);
    int c, r;
    level_size(level, &c, &r);
    cols = uint8_t(c);
    rows = uint8_t(r);
    // Which pictures: a random choice of `pairs()` out of all of them
    uint8_t all[kPictures];
    for (int i = 0; i < kPictures; ++i) all[i] = uint8_t(i);
    for (int i = kPictures - 1; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = all[i]; all[i] = all[j]; all[j] = t;
    }
    for (int i = 0; i < tiles(); ++i) pic[i] = all[i / 2];
    for (int i = tiles() - 1; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = pic[i]; pic[i] = pic[j]; pic[j] = t;
    }
}

Tap Game::tap(int i)
{
    if (i < 0 || i >= tiles() || solved() || matched[i]) return Tap::Ignored;
    if (up_b >= 0) {                       // a missed pair still showing: turn it back
        if (i == up_a || i == up_b) { up_a = up_b = -1; return Tap::Ignored; }
        up_a = up_b = -1;
    }
    if (up_a < 0) { up_a = int8_t(i); return Tap::First; }
    if (i == up_a) return Tap::Ignored;
    ++turns;
    if (pic[i] == pic[up_a]) {
        matched[i] = matched[up_a] = 1;
        ++found;
        up_a = -1;
        return Tap::Match;
    }
    up_b = int8_t(i);
    return Tap::Miss;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "MEM1", 4); n = 4;
    buf[n++] = level; buf[n++] = cols; buf[n++] = rows;
    memcpy(buf + n, pic, kMaxTiles); n += kMaxTiles;
    memcpy(buf + n, matched, kMaxTiles); n += kMaxTiles;
    buf[n++] = uint8_t(up_a); buf[n++] = uint8_t(up_b);
    buf[n++] = uint8_t(turns); buf[n++] = uint8_t(turns >> 8);
    buf[n++] = found;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "MEM1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.level = buf[n++]; g.cols = buf[n++]; g.rows = buf[n++];
    int c, r;
    if (g.level >= kLevels) return false;
    level_size(g.level, &c, &r);
    if (g.cols != c || g.rows != r) return false;
    memcpy(g.pic, buf + n, kMaxTiles); n += kMaxTiles;
    memcpy(g.matched, buf + n, kMaxTiles); n += kMaxTiles;
    g.up_a = int8_t(buf[n++]); g.up_b = int8_t(buf[n++]);
    g.turns = uint16_t(buf[n] | (buf[n + 1] << 8)); n += 2;
    g.found = buf[n++];
    for (int i = 0; i < g.tiles(); ++i) if (g.pic[i] >= kPictures) return false;
    if (g.up_a < -1 || g.up_a >= g.tiles() || g.up_b < -1 || g.up_b >= g.tiles() || g.found > g.pairs()) return false;
    *this = g;
    return true;
}

} // namespace memory
