#include "freecell_core.h"

#include <cstring>

namespace freecell {

int found_for(uint8_t c)
{
    for (int f = 0; f < 4; ++f) if (kFoundSuit[f] == suit(c)) return Found0 + f;
    return Found0;
}

void Game::deal(uint32_t sd)
{
    memset(cell, 0xFF, sizeof cell);
    memset(found, 0, sizeof found);
    memset(n, 0, sizeof n);
    seed = sd;
    moves = 0;
    log_n = 0;
    auto_n = 0;
    uint8_t deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = uint8_t(i);
    Rng rng(sd);
    for (int i = 51; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    for (int i = 0; i < 52; ++i) col[i % 8][n[i % 8]++] = deck[i];
}

uint8_t Game::top(int p) const
{
    if (p < Found0) return cell[p];
    if (p < Col0) { const int f = p - Found0; return found[f] ? uint8_t(kFoundSuit[f] * 13 + found[f] - 1) : 0xFF; }
    const int c = p - Col0;
    return n[c] ? col[c][n[c] - 1] : 0xFF;
}

int Game::free_cells() const { int k = 0; for (uint8_t c : cell) k += c == 0xFF; return k; }
int Game::empty_cols() const { int k = 0; for (int c = 0; c < 8; ++c) k += n[c] == 0; return k; }

int Game::max_run(bool to_empty) const
{
    const int e = empty_cols() - (to_empty ? 1 : 0);
    return (free_cells() + 1) << (e > 0 ? e : 0);
}

int Game::run_start(int c) const
{
    if (!n[c]) return 0;
    int i = n[c] - 1;
    while (i > 0 && red(col[c][i - 1]) != red(col[c][i]) && rank(col[c][i - 1]) == rank(col[c][i]) + 1) --i;
    return i;
}

bool Game::can_move(int from, int idx, int to) const
{
    if (from < 0 || from >= kPiles || to < 0 || to >= kPiles || from == to) return false;
    if (from >= Found0 && from < Col0) return false;            // nothing comes back off a foundation
    uint8_t c;
    int count = 1;
    if (from < Found0) { c = cell[from]; if (c == 0xFF) return false; }
    else {
        const int k = from - Col0;
        if (idx < run_start(k) || idx >= n[k]) return false;
        c = col[k][idx];
        count = n[k] - idx;
    }
    if (to < Found0) return count == 1 && cell[to] == 0xFF;
    if (to < Col0) return count == 1 && to == found_for(c) && found[to - Found0] == rank(c) - 1;
    const int d = to - Col0;
    if (n[d] + count > kColMax) return false;
    if (!n[d]) return count <= max_run(true);
    const uint8_t t = col[d][n[d] - 1];
    return count <= max_run(false) && red(t) != red(c) && rank(t) == rank(c) + 1;
}

// A card goes up by itself when no card could still need it: Aces and
// 2s, or when both opposite-color cards one rank lower are already up
uint8_t Game::auto_up()
{
    uint8_t k = 0;
    for (bool again = true; again;) {
        again = false;
        for (int p = 0; p < kPiles; ++p) {
            if (p >= Found0 && p < Col0) continue;
            const uint8_t c = top(p);
            if (c == 0xFF) continue;
            const int f = found_for(c) - Found0;
            if (found[f] != rank(c) - 1) continue;
            bool safe = rank(c) <= 2;
            if (!safe) {
                safe = true;
                for (int g = 0; g < 4; ++g)
                    if ((kFoundSuit[g] == 1 || kFoundSuit[g] == 2) != red(c) && found[g] < rank(c) - 1) safe = false;
            }
            if (!safe || auto_n >= sizeof auto_from) continue;
            if (p < Found0) cell[p] = 0xFF; else --n[p - Col0];
            ++found[f];
            auto_from[auto_n] = uint8_t(p);
            auto_to[auto_n++] = uint8_t(f);
            ++k;
            again = true;
        }
    }
    return k;
}

bool Game::move(int from, int idx, int to)
{
    if (!can_move(from, idx, to)) return false;
    Step s{uint8_t(from), uint8_t(to), 1, 0};
    uint8_t cards[kColMax];
    int count = 1;
    if (from < Found0) { cards[0] = cell[from]; cell[from] = 0xFF; }
    else {
        const int k = from - Col0;
        count = n[k] - idx;
        memcpy(cards, &col[k][idx], size_t(count));
        n[k] = uint8_t(idx);
    }
    s.count = uint8_t(count);
    if (to < Found0) cell[to] = cards[0];
    else if (to < Col0) ++found[to - Found0];
    else { const int d = to - Col0; memcpy(&col[d][n[d]], cards, size_t(count)); n[d] = uint8_t(n[d] + count); }
    s.autos = auto_up();
    if (log_n == kLog) { memmove(log, log + 1, sizeof(Step) * (kLog - 1)); --log_n; }
    log[log_n++] = s;
    ++moves;
    return true;
}

bool Game::undo()
{
    if (!log_n) return false;
    const Step s = log[--log_n];
    // the automatic moves first, newest first
    for (int k = 0; k < s.autos && auto_n; ++k) {
        --auto_n;
        const int p = auto_from[auto_n], f = auto_to[auto_n];
        const uint8_t c = uint8_t(kFoundSuit[f] * 13 + found[f] - 1);
        --found[f];
        if (p < Found0) cell[p] = c;
        else { const int q = p - Col0; col[q][n[q]++] = c; }
    }
    // the move itself
    uint8_t cards[kColMax];
    int count = s.count;
    if (s.to < Found0) { cards[0] = cell[s.to]; cell[s.to] = 0xFF; }
    else if (s.to < Col0) { const int f = s.to - Found0; cards[0] = uint8_t(kFoundSuit[f] * 13 + found[f] - 1); --found[f]; }
    else { const int d = s.to - Col0; n[d] = uint8_t(n[d] - count); memcpy(cards, &col[d][n[d]], size_t(count)); }
    if (s.from < Found0) cell[s.from] = cards[0];
    else { const int k = s.from - Col0; memcpy(&col[k][n[k]], cards, size_t(count)); n[k] = uint8_t(n[k] + count); }
    if (moves) --moves;
    return true;
}

int Game::best_target(int from, int idx) const
{
    const uint8_t c = from < Found0 ? cell[from] : (from >= Col0 && idx < n[from - Col0] ? col[from - Col0][idx] : 0xFF);
    if (c == 0xFF) return -1;
    if (can_move(from, idx, found_for(c))) return found_for(c);
    int empty = -1;
    for (int d = 0; d < 8; ++d) {
        if (!can_move(from, idx, Col0 + d)) continue;
        if (n[d]) return Col0 + d;
        if (empty < 0 && !(from >= Col0 && idx == 0)) empty = Col0 + d;
    }
    if (empty >= 0) return empty;
    if (from >= Col0 && idx == n[from - Col0] - 1)
        for (int f = 0; f < 4; ++f) if (cell[f] == 0xFF) return Cell0 + f;
    return -1;
}

bool Game::hint(int* from, int* idx, int* to) const
{
    // 1) to a foundation; 2) a column run onto another column (not into an
    // empty one); 3) a free-cell card onto a column
    for (int p = 0; p < kPiles; ++p) {
        if (p >= Found0 && p < Col0) continue;
        const uint8_t c = top(p);
        if (c == 0xFF) continue;
        const int i = p >= Col0 ? n[p - Col0] - 1 : 0;
        if (can_move(p, i, found_for(c))) { *from = p; *idx = i; *to = found_for(c); return true; }
    }
    // Only whole runs: part of a run already sits on a card it fits, so
    // moving it elsewhere just swaps it back and forth (hint ping-pong)
    for (int k = 0; k < 8; ++k) {
        if (!n[k]) continue;
        for (int i = run_start(k); i == run_start(k) && i < n[k]; ++i)
            for (int d = 0; d < 8; ++d)
                if (n[d] && can_move(Col0 + k, i, Col0 + d)) { *from = Col0 + k; *idx = i; *to = Col0 + d; return true; }
    }
    for (int f = 0; f < 4; ++f)
        for (int d = 0; d < 8; ++d)
            if (n[d] && can_move(f, 0, Col0 + d)) { *from = f; *idx = 0; *to = Col0 + d; return true; }
    return false;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t k = 0;
    memcpy(buf, "FRC1", 4); k = 4;
    memcpy(buf + k, cell, 4); k += 4;
    memcpy(buf + k, found, 4); k += 4;
    for (int c = 0; c < 8; ++c) { buf[k++] = n[c]; memcpy(buf + k, col[c], kColMax); k += kColMax; }
    for (int b = 0; b < 4; ++b) buf[k++] = uint8_t(seed >> (8 * b));
    buf[k++] = uint8_t(moves); buf[k++] = uint8_t(moves >> 8);
    return k;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "FRC1", 4) != 0) return false;
    size_t k = 4;
    uint8_t ce[4], fo[4], nn[8], cc[8][kColMax];
    memcpy(ce, buf + k, 4); k += 4;
    memcpy(fo, buf + k, 4); k += 4;
    bool seen[52] = {};
    int total = 0;
    for (int f = 0; f < 4; ++f) {
        if (fo[f] > 13) return false;
        for (int r = 1; r <= fo[f]; ++r) seen[kFoundSuit[f] * 13 + r - 1] = true;
        total += fo[f];
    }
    for (uint8_t c : ce) if (c != 0xFF) { if (c >= 52 || seen[c]) return false; seen[c] = true; ++total; }
    for (int c = 0; c < 8; ++c) {
        nn[c] = buf[k++];
        if (nn[c] > kColMax) return false;
        memcpy(cc[c], buf + k, kColMax); k += kColMax;
        for (int i = 0; i < nn[c]; ++i) { if (cc[c][i] >= 52 || seen[cc[c][i]]) return false; seen[cc[c][i]] = true; ++total; }
    }
    if (total != 52) return false;
    memcpy(cell, ce, 4); memcpy(found, fo, 4); memcpy(n, nn, 8); memcpy(col, cc, sizeof col);
    seed = 0;
    for (int b = 0; b < 4; ++b) seed |= uint32_t(buf[k++]) << (8 * b);
    moves = uint16_t(buf[k] | (buf[k + 1] << 8));
    log_n = 0;
    auto_n = 0;
    return true;
}

} // namespace freecell
