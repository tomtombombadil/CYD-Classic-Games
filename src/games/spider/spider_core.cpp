#include "spider_core.h"

#include <cstring>

namespace spider {

int suits_for(int level) { return level <= 0 ? 1 : level == 1 ? 2 : 4; }

void Game::deal(uint32_t sd, int lv)
{
    // (the log is large: reset the fields, not the whole object)
    level = uint8_t(lv < 0 ? 0 : lv >= kLevels ? kLevels - 1 : lv);
    memset(col, 0, sizeof col);
    memset(n, 0, sizeof n);
    stock_n = 0;
    done = 0;
    score = 500;
    moves = 0;
    seed = sd;
    log_n = 0;
    // 104 cards: 8 suit-sets of Ace..King, the suits cycling over the level's suits
    static const uint8_t kSuitOrder[4] = {0, 1, 3, 2};          // spades, hearts, clubs, diamonds
    const int ns = suits_for(level);
    uint8_t deck[104];
    for (int set = 0; set < 8; ++set)
        for (int r = 0; r < 13; ++r) deck[set * 13 + r] = uint8_t(kSuitOrder[set % ns] * 13 + r);
    Rng rng(sd);
    for (int i = 103; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    int k = 0;
    for (int c = 0; c < kCols; ++c) {
        const int count = c < 4 ? 6 : 5;
        for (int i = 0; i < count; ++i) col[c][n[c]++] = uint8_t(deck[k++] | (i == count - 1 ? 0 : kDown));
    }
    while (k < 104) stock[stock_n++] = deck[k++];
}

int Game::first_up(int c) const
{
    for (int i = 0; i < n[c]; ++i) if (up(col[c][i])) return i;
    return n[c];
}

int Game::run_start(int c) const
{
    if (!n[c]) return 0;
    int i = n[c] - 1;
    while (i > 0 && up(col[c][i - 1]) && suit(col[c][i - 1]) == suit(col[c][i]) && rank(col[c][i - 1]) == rank(col[c][i]) + 1) --i;
    return i;
}

bool Game::can_move(int from, int idx, int to) const
{
    if (from < 0 || from >= kCols || to < 0 || to >= kCols || from == to) return false;
    if (idx < run_start(from) || idx >= n[from]) return false;
    if (n[to] + (n[from] - idx) > kColMax) return false;
    if (!n[to]) return true;
    return rank(col[to][n[to] - 1]) == rank(col[from][idx]) + 1;
}

uint16_t Game::take_runs(uint16_t cols, uint16_t* flips, uint32_t* suits)
{
    uint16_t taken = 0;
    for (int c = 0; c < kCols; ++c) {
        if (!(cols >> c & 1) || n[c] < 13) continue;
        const int s = n[c] - 13;
        bool run = up(col[c][s]) && rank(col[c][s]) == 13;
        for (int i = s + 1; i < n[c] && run; ++i)
            run = up(col[c][i]) && suit(col[c][i]) == suit(col[c][s]) && rank(col[c][i]) + 1 == rank(col[c][i - 1]);
        if (!run) continue;
        if (done < 8) done_suit[done] = uint8_t(suit(col[c][s]));
        ++done;
        *suits |= uint32_t(suit(col[c][s]) & 3) << (2 * c);
        n[c] = uint8_t(s);
        taken |= uint16_t(1 << c);
        score = int16_t(score + 100);
        if (n[c] && !up(col[c][n[c] - 1])) { col[c][n[c] - 1] &= uint8_t(~kDown); *flips |= uint16_t(1 << c); }
    }
    return taken;
}

namespace {
void push(Game& g, const Step& s)
{
    if (g.log_n == kLog) { memmove(g.log, g.log + 1, sizeof(Step) * (kLog - 1)); --g.log_n; }
    g.log[g.log_n++] = s;
}
}

bool Game::move(int from, int idx, int to)
{
    if (!can_move(from, idx, to)) return false;
    Step s{0, uint8_t(from), uint8_t(to), uint8_t(n[from] - idx), 0, 0, 0, 0, score};
    for (int i = idx; i < n[from]; ++i) col[to][n[to]++] = col[from][i];
    n[from] = uint8_t(idx);
    if (n[from] && !up(col[from][n[from] - 1])) { col[from][n[from] - 1] &= uint8_t(~kDown); s.flipped_from = 1; }
    score = int16_t(score - 1);
    s.done_mask = take_runs(uint16_t(1 << to), &s.flip_mask, &s.done_suits);
    push(*this, s);
    ++moves;
    return true;
}

bool Game::can_deal() const
{
    if (stock_n < kCols) return false;
    for (int c = 0; c < kCols; ++c) if (!n[c] || n[c] >= kColMax) return false;
    return true;
}

bool Game::deal_row()
{
    if (!can_deal()) return false;
    Step s{1, 0, 0, 0, 0, 0, 0, 0, score};
    for (int c = 0; c < kCols; ++c) col[c][n[c]++] = stock[--stock_n];
    score = int16_t(score - 1);
    s.done_mask = take_runs(0x3FF, &s.flip_mask, &s.done_suits);
    push(*this, s);
    ++moves;
    return true;
}

bool Game::undo()
{
    if (!log_n) return false;
    const Step s = log[--log_n];
    // put back any runs taken off (in reverse column order), with their flips
    for (int c = kCols - 1; c >= 0; --c) {
        if (!(s.done_mask >> c & 1)) continue;
        if (s.flip_mask >> c & 1) col[c][n[c] - 1] |= kDown;
        const int st = int(s.done_suits >> (2 * c) & 3);
        for (int r = 13; r >= 1; --r) col[c][n[c]++] = uint8_t(st * 13 + r - 1);
        --done;
    }
    if (s.kind == 1) {
        for (int c = kCols - 1; c >= 0; --c) stock[stock_n++] = col[c][--n[c]];
    } else {
        if (s.flipped_from && n[s.from]) col[s.from][n[s.from] - 1] |= kDown;
        const int base = n[s.to] - s.count;
        for (int i = 0; i < s.count; ++i) col[s.from][n[s.from]++] = col[s.to][base + i];
        n[s.to] = uint8_t(base);
    }
    score = s.score;
    if (moves) --moves;
    return true;
}

int Game::best_target(int from, int idx) const
{
    if (from < 0 || idx < 0) return -1;
    int any = -1, empty = -1;
    for (int t = 0; t < kCols; ++t) {
        if (!can_move(from, idx, t)) continue;
        if (!n[t]) { if (empty < 0 && idx > 0) empty = t; continue; }
        if (suit(col[t][n[t] - 1]) == suit(col[from][idx])) return t;
        if (any < 0) any = t;
    }
    return any >= 0 ? any : empty;
}

bool Game::hint(int* from, int* idx, int* to) const
{
    // 1) a run onto its own suit; 2) a run that turns a card up or empties
    // a column onto anything; 3) deal
    int fallback_f = -1, fallback_i = -1, fallback_t = -1;
    for (int f = 0; f < kCols; ++f) {
        if (!n[f]) continue;
        const int i = run_start(f);
        for (int t = 0; t < kCols; ++t) {
            if (!n[t] || !can_move(f, i, t)) continue;
            const bool same = suit(col[t][n[t] - 1]) == suit(col[f][i]);
            const bool helps = i > 0 && !up(col[f][i - 1]);
            const bool same_below = i > 0 && up(col[f][i - 1]) && rank(col[f][i - 1]) == rank(col[f][i]) + 1;
            if (same && !same_below) { *from = f; *idx = i; *to = t; return true; }
            if ((helps || i == 0) && fallback_f < 0 && !same_below) { fallback_f = f; fallback_i = i; fallback_t = t; }
        }
    }
    if (fallback_f >= 0) { *from = fallback_f; *idx = fallback_i; *to = fallback_t; return true; }
    if (can_deal()) { *from = -1; *idx = -1; *to = -1; return true; }
    return false;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t k = 0;
    memcpy(buf, "SPD1", 4); k = 4;
    buf[k++] = level;
    for (int c = 0; c < kCols; ++c) { buf[k++] = n[c]; memcpy(buf + k, col[c], kColMax); k += kColMax; }
    buf[k++] = stock_n; memcpy(buf + k, stock, 50); k += 50;
    buf[k++] = done; memcpy(buf + k, done_suit, 8); k += 8;
    buf[k++] = uint8_t(score); buf[k++] = uint8_t(uint16_t(score) >> 8);
    buf[k++] = uint8_t(moves); buf[k++] = uint8_t(moves >> 8);
    for (int b = 0; b < 4; ++b) buf[k++] = uint8_t(seed >> (8 * b));
    return k;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "SPD1", 4) != 0) return false;
    size_t k = 4;
    const uint8_t lv = buf[k++];
    if (lv >= kLevels) return false;
    uint8_t cn[kCols];
    int total = 0;
    for (int c = 0; c < kCols; ++c) { cn[c] = buf[k]; if (cn[c] > kColMax) return false; total += cn[c]; k += 1 + kColMax; }
    const uint8_t sn = buf[k], dn = buf[k + 51];
    if (sn > 50 || dn > 8 || total + sn + 13 * dn != 104) return false;
    {   // card values in range (face-down bit aside)
        size_t q = 5;
        for (int c = 0; c < kCols; ++c, q += 1 + kColMax)
            for (int i = 0; i < buf[q]; ++i) if ((buf[q + 1 + i] & ~kDown) >= 52) return false;
        for (int i = 0; i < sn; ++i) if ((buf[q + 1 + i] & ~kDown) >= 52) return false;
    }
    k = 4;
    level = buf[k++];
    for (int c = 0; c < kCols; ++c) { n[c] = buf[k++]; memcpy(col[c], buf + k, kColMax); k += kColMax; }
    stock_n = buf[k++]; memcpy(stock, buf + k, 50); k += 50;
    done = buf[k++]; memcpy(done_suit, buf + k, 8); k += 8;
    score = int16_t(buf[k] | (buf[k + 1] << 8)); k += 2;
    moves = uint16_t(buf[k] | (buf[k + 1] << 8)); k += 2;
    seed = 0;
    for (int b = 0; b < 4; ++b) seed |= uint32_t(buf[k++]) << (8 * b);
    log_n = 0;
    return true;
}

} // namespace spider
