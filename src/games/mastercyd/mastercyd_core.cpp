#include "mastercyd_core.h"

#include <cstring>

namespace mastercyd {

Feedback score(const uint8_t* code, const uint8_t* guess, int n)
{
    Feedback f;
    int cc[kColors] = {}, gc[kColors] = {};
    for (int i = 0; i < n; ++i) {
        if (code[i] == guess[i]) { ++f.exact; continue; }
        if (code[i] < kColors) ++cc[code[i]];
        if (guess[i] < kColors) ++gc[guess[i]];
    }
    for (int c = 0; c < kColors; ++c) f.near += static_cast<uint8_t>(cc[c] < gc[c] ? cc[c] : gc[c]);
    return f;
}

void Game::start(int lv, Rng& rng)
{
    *this = Game{};
    level = static_cast<uint8_t>(lv < 0 ? 0 : lv >= kLevels ? kLevels - 1 : lv);
    bool used[kColors] = {};
    for (int i = 0; i < pegs(); ++i) {
        uint8_t c;
        do { c = static_cast<uint8_t>(rng.next() % kColors); } while (!repeats() && used[c]);
        used[c] = true;
        secret[i] = c;
    }
}

bool Game::place(uint8_t color)
{
    if (over() || color >= kColors) return false;
    if (!repeats())
        for (int i = 0; i < pegs(); ++i) if (cur[i] == color) return false;
    for (int i = 0; i < pegs(); ++i)
        if (cur[i] == kEmpty) { cur[i] = color; return true; }
    return false;
}

void Game::clear(int slot)
{
    if (slot >= 0 && slot < pegs() && !over()) cur[slot] = kEmpty;
}

bool Game::full() const
{
    for (int i = 0; i < pegs(); ++i) if (cur[i] == kEmpty) return false;
    return true;
}

bool Game::submit()
{
    if (over() || !full()) return false;
    memcpy(guess[rows], cur, kMaxPegs);
    fb[rows] = score(secret, cur, pegs());
    ++rows;
    memset(cur, kEmpty, sizeof cur);
    return true;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "MCD1", 4); n = 4;
    buf[n++] = level;
    memcpy(buf + n, secret, kMaxPegs); n += kMaxPegs;
    memcpy(buf + n, guess, sizeof guess); n += sizeof guess;
    for (int r = 0; r < kRows; ++r) { buf[n++] = fb[r].exact; buf[n++] = fb[r].near; }
    buf[n++] = rows;
    memcpy(buf + n, cur, kMaxPegs); n += kMaxPegs;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "MCD1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.level = buf[n++];
    if (g.level >= kLevels) return false;
    memcpy(g.secret, buf + n, kMaxPegs); n += kMaxPegs;
    memcpy(g.guess, buf + n, sizeof g.guess); n += sizeof g.guess;
    for (int r = 0; r < kRows; ++r) { g.fb[r].exact = buf[n++]; g.fb[r].near = buf[n++]; }
    g.rows = buf[n++];
    memcpy(g.cur, buf + n, kMaxPegs); n += kMaxPegs;
    if (g.rows > kRows) return false;
    for (int i = 0; i < g.pegs(); ++i) {
        if (g.secret[i] >= kColors) return false;
        if (g.cur[i] != kEmpty && g.cur[i] >= kColors) return false;
    }
    *this = g;
    return true;
}

} // namespace mastercyd
