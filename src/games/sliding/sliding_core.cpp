#include "sliding_core.h"

#include <cstring>

namespace sliding {

void Puzzle::reset(int size)
{
    n = static_cast<uint8_t>(size < 3 ? 3 : size > kMaxN ? kMaxN : size);
    memset(tile, 0, sizeof tile);
    for (int i = 0; i < cells() - 1; ++i) tile[i] = static_cast<uint8_t>(i + 1);
    gap = static_cast<uint8_t>(cells() - 1);
    moves = 0;
}

void Puzzle::shuffle(Rng& rng)
{
    const int steps = n == 3 ? 80 : n == 4 ? 240 : 500;
    int prev = -1;
    for (int k = 0; k < steps || solved(); ++k) {
        // Slide one neighbour of the gap into it (never straight back)
        int opts[4], m = 0;
        const int r = gap / n, c = gap % n;
        const int nb[4][2] = {{r - 1, c}, {r + 1, c}, {r, c - 1}, {r, c + 1}};
        for (auto& p : nb)
            if (p[0] >= 0 && p[0] < n && p[1] >= 0 && p[1] < n && p[0] * n + p[1] != prev)
                opts[m++] = p[0] * n + p[1];
        const int pick = opts[rng.next() % m];
        tile[gap] = tile[pick];
        tile[pick] = 0;
        prev = gap;
        gap = static_cast<uint8_t>(pick);
    }
    moves = 0;
}

int Puzzle::tap(int i)
{
    if (i < 0 || i >= cells() || i == gap) return 0;
    const int r = i / n, c = i % n, gr = gap / n, gc = gap % n;
    int step;
    if (r == gr)      step = (c < gc) ? -1 : 1;     // walk from the gap toward i
    else if (c == gc) step = (r < gr) ? -n : n;
    else return 0;
    int moved = 0;
    while (gap != i) {
        const int next = gap + step;
        tile[gap] = tile[next];
        tile[next] = 0;
        gap = static_cast<uint8_t>(next);
        ++moved;
    }
    moves = static_cast<uint16_t>(moves + moved);
    return moved;
}

bool Puzzle::solved() const
{
    for (int i = 0; i < cells() - 1; ++i) if (tile[i] != i + 1) return false;
    return true;
}

size_t Puzzle::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "SLD1", 4);
    buf[4] = n;
    buf[5] = moves & 0xFF;
    buf[6] = moves >> 8;
    memcpy(buf + 7, tile, sizeof tile);
    return kSaveBytes;
}

bool Puzzle::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "SLD1", 4) != 0) return false;
    Puzzle t;
    t.n = buf[4];
    if (t.n < 3 || t.n > kMaxN) return false;
    t.moves = static_cast<uint16_t>(buf[5] | (buf[6] << 8));
    memcpy(t.tile, buf + 7, sizeof t.tile);
    // Every tile exactly once
    bool seen[kMaxN * kMaxN] = {};
    for (int i = 0; i < t.cells(); ++i) {
        const int v = t.tile[i];
        if (v >= t.cells() || seen[v]) return false;
        seen[v] = true;
        if (v == 0) t.gap = static_cast<uint8_t>(i);
    }
    *this = t;
    return true;
}

int size_for_level(int level) { return level <= 0 ? 3 : level == 1 ? 4 : 5; }

} // namespace sliding
