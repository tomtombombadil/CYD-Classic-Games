#include "nonogram_core.h"

#include <cstring>

namespace nonogram {

int level_size(int level)
{
    static const uint8_t s[kLevels] = {5, 8, 10};
    return s[level < 0 ? 0 : level >= kLevels ? kLevels - 1 : level];
}

Clue clue_of(uint16_t line, int n)
{
    Clue c;
    int run = 0;
    for (int i = 0; i <= n; ++i) {
        const bool on = i < n && (line >> i & 1);
        if (on) ++run;
        else if (run) { if (c.n < kMaxClues) c.run[c.n++] = uint8_t(run); run = 0; }
    }
    return c;
}

namespace {

// Line solver: try every placement of the runs consistent with `known`;
// record which cells are filled in all of them and empty in all of them.
struct LineCtx {
    int n;
    const Clue* clue;
    const uint8_t* known;
    uint8_t  place[kMaxN];
    uint16_t can_fill, can_empty;
    int      count;
};

void place_runs(LineCtx& L, int k, int pos)
{
    if (k == L.clue->n) {
        for (int i = pos; i < L.n; ++i) if (L.known[i] == Filled) return;
        for (int i = pos; i < L.n; ++i) L.place[i] = 0;
        for (int i = 0; i < L.n; ++i) {
            if (L.place[i]) L.can_fill |= uint16_t(1 << i);
            else            L.can_empty |= uint16_t(1 << i);
        }
        ++L.count;
        return;
    }
    int rest = 0;
    for (int j = k + 1; j < L.clue->n; ++j) rest += L.clue->run[j] + 1;
    const int len = L.clue->run[k];
    for (int s = pos; s + len + rest <= L.n; ++s) {
        bool ok = true;
        for (int i = pos; i < s && ok; ++i) ok = L.known[i] != Filled;          // gap before
        for (int i = s; i < s + len && ok; ++i) ok = L.known[i] != Marked;      // the run
        if (ok && s + len < L.n) ok = L.known[s + len] != Filled;               // one gap after
        if (!ok) { if (L.known[s] == Filled) break; continue; }
        for (int i = pos; i < s; ++i) L.place[i] = 0;
        for (int i = s; i < s + len; ++i) L.place[i] = 1;
        if (s + len < L.n) L.place[s + len] = 0;
        place_runs(L, k + 1, s + len + 1 > L.n ? L.n : s + len + 1);
        if (L.known[s] == Filled) break;          // the run can't start after a filled cell
    }
}

// Returns false if the line has no valid placement; else updates `line`
bool solve_line(int n, const Clue& clue, uint8_t* line, bool* changed)
{
    LineCtx L{n, &clue, line, {}, 0, 0, 0};
    place_runs(L, 0, 0);
    if (!L.count) return false;
    for (int i = 0; i < n; ++i) {
        if (line[i] != Unknown) continue;
        const bool f = L.can_fill >> i & 1, e = L.can_empty >> i & 1;
        if (f && !e) { line[i] = Filled; *changed = true; }
        if (e && !f) { line[i] = Marked; *changed = true; }
    }
    return true;
}

} // namespace

bool line_solve(int n, const Clue* rows, const Clue* cols, uint8_t* out)
{
    memset(out, Unknown, size_t(n) * n);
    bool changed = true;
    while (changed) {
        changed = false;
        uint8_t line[kMaxN];
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) line[c] = out[r * n + c];
            if (!solve_line(n, rows[r], line, &changed)) return false;
            for (int c = 0; c < n; ++c) out[r * n + c] = line[c];
        }
        for (int c = 0; c < n; ++c) {
            for (int r = 0; r < n; ++r) line[r] = out[r * n + c];
            if (!solve_line(n, cols[c], line, &changed)) return false;
            for (int r = 0; r < n; ++r) out[r * n + c] = line[r];
        }
    }
    for (int i = 0; i < n * n; ++i) if (out[i] == Unknown) return false;
    return true;
}

void Game::set_picture(int size, const uint16_t* bits)
{
    n = uint8_t(size);
    for (int r = 0; r < kMaxN; ++r) picture[r] = r < n ? bits[r] : 0;
    for (int r = 0; r < n; ++r) rows[r] = clue_of(picture[r], n);
    for (int c = 0; c < n; ++c) {
        uint16_t col = 0;
        for (int r = 0; r < n; ++r) if (picture[r] >> c & 1) col |= uint16_t(1 << r);
        cols[c] = clue_of(col, n);
    }
    memset(cell, Unknown, sizeof cell);
    taps = 0;
}

void Game::start(int lv, Rng& rng)
{
    *this = Game{};
    level = uint8_t(lv < 0 ? 0 : lv >= kLevels ? kLevels - 1 : lv);
    const int size = level_size(level);
    uint8_t work[kMaxN * kMaxN];
    for (int tries = 0; tries < 5000; ++tries) {
        uint16_t bits[kMaxN] = {};
        const int half = (size + 1) / 2;
        int filled = 0;
        for (int r = 0; r < size; ++r)
            for (int c = 0; c < half; ++c)
                if (rng.next() % 100 < 58) {           // ~58 % filled: dense enough to solve
                    bits[r] |= uint16_t(1 << c) | uint16_t(1 << (size - 1 - c));
                    ++filled;
                }
        if (filled < size) continue;
        set_picture(size, bits);
        Clue* bad = nullptr;
        for (int r = 0; r < size; ++r) if (rows[r].n > kMaxClues) bad = &rows[r];
        if (bad) continue;
        if (line_solve(size, rows, cols, work)) return;
    }
    // Fallback (never seen in tests): a filled frame, always line-solvable
    uint16_t frame[kMaxN] = {};
    for (int r = 0; r < size; ++r) frame[r] = uint16_t((r == 0 || r == size - 1) ? (1 << size) - 1 : 1 | (1 << (size - 1)));
    set_picture(size, frame);
}

void Game::tap(int r, int c, Cell mode)
{
    if (r < 0 || c < 0 || r >= n || c >= n || solved()) return;
    uint8_t& x = cell[r * n + c];
    x = x == mode ? Unknown : mode;
    ++taps;
}

bool Game::row_done(int r) const
{
    uint16_t bits = 0;
    for (int c = 0; c < n; ++c) if (cell[r * n + c] == Filled) bits |= uint16_t(1 << c);
    const Clue k = clue_of(bits, n);
    return k.n == rows[r].n && memcmp(k.run, rows[r].run, k.n) == 0;
}

bool Game::col_done(int c) const
{
    uint16_t bits = 0;
    for (int r = 0; r < n; ++r) if (cell[r * n + c] == Filled) bits |= uint16_t(1 << r);
    const Clue k = clue_of(bits, n);
    return k.n == cols[c].n && memcmp(k.run, cols[c].run, k.n) == 0;
}

bool Game::solved() const
{
    for (int i = 0; i < n; ++i) if (!row_done(i) || !col_done(i)) return false;
    return true;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t k = 0;
    memcpy(buf, "NON1", 4); k = 4;
    buf[k++] = level; buf[k++] = n;
    for (int r = 0; r < kMaxN; ++r) { buf[k++] = uint8_t(picture[r]); buf[k++] = uint8_t(picture[r] >> 8); }
    memcpy(buf + k, cell, sizeof cell); k += sizeof cell;
    buf[k++] = uint8_t(taps); buf[k++] = uint8_t(taps >> 8);
    return k;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "NON1", 4) != 0) return false;
    Game g;
    size_t k = 4;
    g.level = buf[k++];
    const int size = buf[k++];
    if (g.level >= kLevels || size != level_size(g.level)) return false;
    uint16_t bits[kMaxN];
    for (int r = 0; r < kMaxN; ++r) { bits[r] = uint16_t(buf[k] | (buf[k + 1] << 8)); k += 2; }
    g.set_picture(size, bits);
    memcpy(g.cell, buf + k, sizeof g.cell); k += sizeof g.cell;
    g.taps = uint16_t(buf[k] | (buf[k + 1] << 8));
    for (int i = 0; i < kMaxN * kMaxN; ++i) if (g.cell[i] > Marked) return false;
    *this = g;
    return true;
}

} // namespace nonogram
