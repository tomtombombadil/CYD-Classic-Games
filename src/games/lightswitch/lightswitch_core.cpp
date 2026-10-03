#include "lightswitch_core.h"

#include <cstring>

namespace lightswitch {

uint32_t press_mask(int i)
{
    const int r = i / kN, c = i % kN;
    uint32_t m = 1u << i;
    if (r > 0)      m |= 1u << (i - kN);
    if (r < kN - 1) m |= 1u << (i + kN);
    if (c > 0)      m |= 1u << (i - 1);
    if (c < kN - 1) m |= 1u << (i + 1);
    return m;
}

int min_presses(uint32_t lights, uint32_t* solution)
{
    // Equations: for each light j, XOR of presses that flip j = lights_j.
    // Row j of the matrix is press_mask(j) (the matrix is symmetric), with
    // the light's state as the right-hand side in bit kCells.
    uint64_t row[kCells];
    for (int j = 0; j < kCells; ++j)
        row[j] = press_mask(j) | (uint64_t((lights >> j) & 1) << kCells);
    int pivot_col[kCells];
    int rank = 0;
    uint32_t pivot_cols = 0;
    for (int col = 0; col < kCells && rank < kCells; ++col) {
        int p = -1;
        for (int r = rank; r < kCells; ++r) if ((row[r] >> col) & 1) { p = r; break; }
        if (p < 0) continue;
        const uint64_t t = row[p]; row[p] = row[rank]; row[rank] = t;
        for (int r = 0; r < kCells; ++r)
            if (r != rank && ((row[r] >> col) & 1)) row[r] ^= row[rank];
        pivot_col[rank++] = col;
        pivot_cols |= 1u << col;
    }
    for (int r = rank; r < kCells; ++r)
        if ((row[r] >> kCells) & 1) return -1;          // 0 = 1: no solution
    // Free columns: try every setting, keep the fewest presses
    int free_cols[kCells], nf = 0;
    for (int col = 0; col < kCells; ++col) if (!((pivot_cols >> col) & 1)) free_cols[nf++] = col;
    int best = -1;
    uint32_t best_x = 0;
    for (uint32_t f = 0; f < (1u << nf); ++f) {
        uint32_t x = 0;
        for (int k = 0; k < nf; ++k) if ((f >> k) & 1) x |= 1u << free_cols[k];
        for (int r = 0; r < rank; ++r) {
            int v = (row[r] >> kCells) & 1;
            uint32_t rest = static_cast<uint32_t>(row[r]) & ~(1u << pivot_col[r]) & x;
            v ^= __builtin_popcount(rest) & 1;
            if (v) x |= 1u << pivot_col[r];
        }
        const int w = __builtin_popcount(x);
        if (best < 0 || w < best) { best = w; best_x = x; }
    }
    if (solution) *solution = best_x;
    return best;
}

void Puzzle::generate(int level, Rng& rng)
{
    const int presses = level <= 0 ? 4 : level == 1 ? 8 : 14;
    do {
        uint32_t chosen = 0;
        lights = 0;
        for (int k = 0; k < presses; ++k) {
            int i;
            do { i = rng.next() % kCells; } while ((chosen >> i) & 1);
            chosen |= 1u << i;
            lights ^= press_mask(i);
        }
    } while (lights == 0);
    start = lights;
    moves = 0;
    const int p = min_presses(lights);
    par = static_cast<uint16_t>(p < 0 ? 0 : p);
}

int Puzzle::hint() const
{
    uint32_t x = 0;
    if (lights == 0 || min_presses(lights, &x) <= 0) return -1;
    return __builtin_ctz(x);
}

namespace {
void put32(uint8_t* p, uint32_t v) { for (int k = 0; k < 4; ++k) p[k] = uint8_t(v >> (8 * k)); }
uint32_t get32(const uint8_t* p) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(p[k]) << (8 * k); return v; }
}

size_t Puzzle::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "LSW1", 4);
    put32(buf + 4, lights);
    put32(buf + 8, start);
    buf[12] = moves & 0xFF; buf[13] = moves >> 8;
    buf[14] = par & 0xFF;   buf[15] = par >> 8;
    return kSaveBytes;
}

bool Puzzle::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "LSW1", 4) != 0) return false;
    Puzzle t;
    const uint32_t all = (1u << kCells) - 1;
    t.lights = get32(buf + 4) & all;
    t.start = get32(buf + 8) & all;
    t.moves = static_cast<uint16_t>(buf[12] | (buf[13] << 8));
    t.par = static_cast<uint16_t>(buf[14] | (buf[15] << 8));
    if (min_presses(t.lights) < 0 || min_presses(t.start) < 0) return false;
    *this = t;
    return true;
}

} // namespace lightswitch
