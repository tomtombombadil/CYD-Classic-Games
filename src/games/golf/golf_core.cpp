#include "golf_core.h"

#include <cstring>

namespace golf {

void Game::deal(uint32_t sd)
{
    *this = Game{};
    seed = sd;
    uint8_t deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = uint8_t(i);
    Rng rng(sd);
    for (int i = 51; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    int k = 0;
    for (int r = 0; r < kDepth; ++r)
        for (int c = 0; c < kCols; ++c) col[c][col_n[c]++] = deck[k++];
    for (int i = 0; i < kStock; ++i) stock[stock_n++] = deck[k++];
    waste[waste_n++] = deck[k++];
}

int Game::left() const
{
    int n = 0;
    for (int c = 0; c < kCols; ++c) n += col_n[c];
    return n;
}

bool Game::can_play(int c) const
{
    if (c < 0 || c >= kCols || !col_n[c] || !waste_n) return false;
    const int w = rank(waste[waste_n - 1]), r = rank(col[c][col_n[c] - 1]);
    if (w == 13) return false;                       // nothing on a King
    return r == w + 1 || r == w - 1;
}

bool Game::play(int c)
{
    if (!can_play(c) || log_n >= sizeof log) return false;
    waste[waste_n++] = col[c][--col_n[c]];
    log[log_n++] = uint8_t(c);
    return true;
}

bool Game::draw()
{
    if (!stock_n || log_n >= sizeof log) return false;
    waste[waste_n++] = stock[--stock_n];
    log[log_n++] = 7;
    return true;
}

bool Game::undo()
{
    if (!log_n) return false;
    const uint8_t s = log[--log_n];
    const uint8_t card = waste[--waste_n];
    if (s == 7) stock[stock_n++] = card;
    else        col[s][col_n[s]++] = card;
    return true;
}

bool Game::stuck() const
{
    if (won() || stock_n) return false;
    for (int c = 0; c < kCols; ++c) if (can_play(c)) return false;
    return true;
}

int Game::hint() const
{
    // the column whose next card could keep a run going is nicer, but any
    // play beats turning the stock
    for (int c = 0; c < kCols; ++c) if (can_play(c)) return c;
    return stock_n ? 7 : -1;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "GLF1", 4); n = 4;
    memcpy(buf + n, col, sizeof col); n += sizeof col;
    memcpy(buf + n, col_n, sizeof col_n); n += sizeof col_n;
    memcpy(buf + n, stock, sizeof stock); n += sizeof stock;
    buf[n++] = stock_n;
    memcpy(buf + n, waste, sizeof waste); n += sizeof waste;
    buf[n++] = waste_n;
    memcpy(buf + n, log, sizeof log); n += sizeof log;
    buf[n++] = log_n;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(seed >> (8 * k));
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "GLF1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    memcpy(g.col, buf + n, sizeof g.col); n += sizeof g.col;
    memcpy(g.col_n, buf + n, sizeof g.col_n); n += sizeof g.col_n;
    memcpy(g.stock, buf + n, sizeof g.stock); n += sizeof g.stock;
    g.stock_n = buf[n++];
    memcpy(g.waste, buf + n, sizeof g.waste); n += sizeof g.waste;
    g.waste_n = buf[n++];
    memcpy(g.log, buf + n, sizeof g.log); n += sizeof g.log;
    g.log_n = buf[n++];
    for (int k = 0; k < 4; ++k) g.seed |= uint32_t(buf[n++]) << (8 * k);
    int total = g.stock_n + g.waste_n;
    for (int c = 0; c < kCols; ++c) { if (g.col_n[c] > kDepth) return false; total += g.col_n[c]; }
    if (g.stock_n > kStock || g.waste_n > 52 || g.log_n > sizeof g.log || total != 52) return false;
    *this = g;
    return true;
}

} // namespace golf
