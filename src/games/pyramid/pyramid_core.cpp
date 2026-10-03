#include "pyramid_core.h"

#include <cstring>

namespace pyramid {

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
    for (int i = 0; i < kPyr; ++i) pyr[i] = deck[i];
    for (int i = kPyr; i < 52; ++i) stock[stock_n++] = deck[i];
}

bool Game::present(int s) const
{
    if (s == kWasteSlot) return waste_n > 0;
    return s >= 0 && s < kPyr && !(gone >> s & 1);
}

bool Game::free(int s) const
{
    if (!present(s)) return false;
    if (s == kWasteSlot) return true;
    const int r = row_of(s);
    if (r == kRows - 1) return true;
    const int k = s - r * (r + 1) / 2;
    const int below = (r + 1) * (r + 2) / 2 + k;
    return !present(below) && !present(below + 1);
}

uint8_t Game::card(int s) const { return s == kWasteSlot ? waste[waste_n - 1] : pyr[s]; }

bool Game::can_pair(int a, int b) const
{
    if (!free(a)) return false;
    if (b < 0) return rank(card(a)) == 13;
    if (a == b || !free(b)) return false;
    return rank(card(a)) + rank(card(b)) == 13;
}

namespace {
void push(Game& g, Step s)
{
    if (g.log_n == kLog) { memmove(g.log, g.log + 1, sizeof(Step) * (kLog - 1)); --g.log_n; }
    g.log[g.log_n++] = s;
}
}

bool Game::pair(int a, int b)
{
    if (!can_pair(a, b)) return false;
    Step s{0, uint8_t(a), uint8_t(b < 0 ? 0xFF : b), card(a), uint8_t(b < 0 ? 0 : card(b))};
    const int xs[2] = {a, b};
    for (int x : xs) {
        if (x < 0) continue;
        if (x == kWasteSlot) --waste_n;
        else gone |= uint32_t(1) << x;
    }
    push(*this, s);
    ++moves;
    return true;
}

bool Game::can_draw() const { return stock_n > 0 || (waste_n > 0 && passes < kPasses); }

bool Game::draw()
{
    if (stock_n) {
        waste[waste_n++] = stock[--stock_n];
        push(*this, Step{1, 0, 0, 0, 0});
    } else if (waste_n && passes < kPasses) {
        const uint8_t n = waste_n;
        while (waste_n) stock[stock_n++] = waste[--waste_n];
        ++passes;
        push(*this, Step{2, n, 0, 0, 0});
    } else {
        return false;
    }
    ++moves;
    return true;
}

bool Game::undo()
{
    if (!log_n) return false;
    const Step s = log[--log_n];
    if (s.kind == 1) {
        stock[stock_n++] = waste[--waste_n];
    } else if (s.kind == 2) {
        for (int i = 0; i < s.a; ++i) waste[waste_n++] = stock[--stock_n];
        --passes;
    } else {
        // put back in reverse: b first, then a (so a waste card returns on top in order)
        const int xs[2] = {s.b == 0xFF ? -1 : s.b, s.a};
        const uint8_t cs[2] = {s.cb, s.ca};
        for (int k = 0; k < 2; ++k) {
            if (xs[k] < 0) continue;
            if (xs[k] == kWasteSlot) waste[waste_n++] = cs[k];
            else gone &= ~(uint32_t(1) << xs[k]);
        }
    }
    if (moves) --moves;
    return true;
}

int Game::removed() const
{
    int n = 0;
    for (uint32_t g = gone; g; g &= g - 1) ++n;
    return n;
}

bool Game::hint(int* a, int* b) const
{
    for (int x = 0; x <= kWasteSlot; ++x) {
        if (!free(x)) continue;
        if (rank(card(x)) == 13) { *a = x; *b = -1; return true; }
        for (int y = x + 1; y <= kWasteSlot; ++y)
            if (can_pair(x, y)) { *a = x; *b = y; return true; }
    }
    if (can_draw()) { *a = -2; *b = -1; return true; }
    return false;
}

bool Game::stuck() const
{
    int a, b;
    return !won() && !hint(&a, &b);
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "PYR1", 4); n = 4;
    memcpy(buf + n, pyr, kPyr); n += kPyr;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(gone >> (8 * k));
    buf[n++] = stock_n; memcpy(buf + n, stock, 24); n += 24;
    buf[n++] = waste_n; memcpy(buf + n, waste, 24); n += 24;
    buf[n++] = passes;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(seed >> (8 * k));
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "PYR1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    memcpy(g.pyr, buf + n, kPyr); n += kPyr;
    for (int k = 0; k < 4; ++k) g.gone |= uint32_t(buf[n++]) << (8 * k);
    g.stock_n = buf[n++]; memcpy(g.stock, buf + n, 24); n += 24;
    g.waste_n = buf[n++]; memcpy(g.waste, buf + n, 24); n += 24;
    g.passes = buf[n++];
    for (int k = 0; k < 4; ++k) g.seed |= uint32_t(buf[n++]) << (8 * k);
    g.moves = uint16_t(buf[n] | (buf[n + 1] << 8));
    if (g.stock_n > 24 || g.waste_n > 24 || g.stock_n + g.waste_n > 24 || g.passes < 1 || g.passes > kPasses) return false;
    *this = g;
    return true;
}

} // namespace pyramid
