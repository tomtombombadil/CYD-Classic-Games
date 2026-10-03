#include "solitaire_core.h"

#include <cstring>

namespace solitaire {

int  rank(uint8_t c) { return (c & 0x3F) % 13 + 1; }
int  suit(uint8_t c) { return (c & 0x3F) / 13; }
bool red(uint8_t c)  { const int s = suit(c); return s == 1 || s == 2; }   // same order as cards::Suit

int found_for(uint8_t c)
{
    for (int f = 0; f < 4; ++f) if (kFoundSuit[f] == suit(c)) return Found0 + f;
    return Found0;
}

namespace {
bool is_found(int p) { return p >= Found0 && p < Tab0; }
bool is_tab(int p)   { return p >= Tab0 && p < kPiles; }
}

void Game::deal(uint32_t sd, int dr, Scoring sc)
{
    // Keep the log out of the copy: just reset fields
    for (Stack& s : pile) s = Stack{};
    draw = uint8_t(dr == 1 ? 1 : 3);
    scoring = sc;
    seed = sd;
    score = sc == Scoring::Vegas ? -52 : 0;
    passes = 0;
    moves = 0;
    undo_n = 0;
    uint8_t deck[52];
    for (int i = 0; i < 52; ++i) deck[i] = uint8_t(i);
    Rng rng(sd);
    for (int i = 51; i > 0; --i) {
        const int j = int(rng.next() % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
    int k = 0;
    for (int col = 0; col < 7; ++col)
        for (int r = 0; r <= col; ++r) {
            Stack& s = pile[Tab0 + col];
            s.c[s.n++] = uint8_t(deck[k++] | (r == col ? 0 : kDown));
        }
    while (k < 52) { Stack& s = pile[Stock]; s.c[s.n++] = uint8_t(deck[k++] | kDown); }
}

int Game::first_up(int p) const
{
    const Stack& s = pile[p];
    for (int i = 0; i < s.n; ++i) if (face_up(s.c[i])) return i;
    return s.n;
}

bool Game::can_draw() const
{
    if (pile[Stock].n) return true;
    if (!pile[Waste].n) return false;
    if (scoring == Scoring::Vegas) return passes < (draw == 1 ? 0 : 2);
    return true;
}

namespace {
void log_step(Game& g, uint8_t from, uint8_t to, uint8_t count, uint8_t kind, uint8_t flipped, int32_t score_before, uint8_t passes_before)
{
    if (g.undo_n == kUndo) {                       // drop the oldest
        memmove(g.undo_log, g.undo_log + 1, sizeof(Step) * (kUndo - 1));
        --g.undo_n;
    }
    g.undo_log[g.undo_n++] = Step{from, to, count, kind, flipped, passes_before, score_before};
}
}

bool Game::draw_stock()
{
    if (!can_draw()) return false;
    const int32_t before = score;
    const uint8_t pb = passes;
    Stack& st = pile[Stock];
    Stack& w = pile[Waste];
    if (st.n) {
        const int k = st.n < draw ? st.n : draw;
        for (int i = 0; i < k; ++i) w.c[w.n++] = uint8_t(st.c[--st.n] & ~kDown);
        log_step(*this, Stock, Waste, uint8_t(k), 1, 0, before, pb);
    } else {
        const int k = w.n;
        for (int i = 0; i < k; ++i) st.c[st.n++] = uint8_t(w.c[--w.n] | kDown);
        ++passes;
        if (scoring == Scoring::Standard) { score -= draw == 1 ? 100 : 20; if (score < 0) score = 0; }
        log_step(*this, Waste, Stock, uint8_t(k), 2, 0, before, pb);
    }
    ++moves;
    return true;
}

bool Game::can_move(int from, int idx, int to) const
{
    if (from < 0 || from >= kPiles || to < 0 || to >= kPiles || from == to) return false;
    if (from == Stock || to == Stock || to == Waste) return false;
    const Stack& f = pile[from];
    if (idx < 0 || idx >= f.n || !face_up(f.c[idx])) return false;
    const int count = f.n - idx;
    if ((from == Waste || is_found(from)) && count != 1) return false;
    const uint8_t c = f.c[idx];
    const Stack& t = pile[to];
    if (is_found(to)) {
        if (count != 1 || suit(c) != kFoundSuit[to - Found0]) return false;
        if (!t.n) return rank(c) == 1;
        return suit(t.top()) == suit(c) && rank(c) == rank(t.top()) + 1;
    }
    // tableau
    if (!t.n) return rank(c) == 13;
    const uint8_t top = t.top();
    return face_up(top) && red(top) != red(c) && rank(c) + 1 == rank(top);
}

bool Game::move(int from, int idx, int to)
{
    if (!can_move(from, idx, to)) return false;
    const int32_t before = score;
    Stack& f = pile[from];
    Stack& t = pile[to];
    const int count = f.n - idx;
    for (int i = 0; i < count; ++i) t.c[t.n++] = f.c[idx + i];
    f.n = uint8_t(idx);
    uint8_t flipped = 0;
    if (is_tab(from) && f.n && !face_up(f.top())) { f.c[f.n - 1] &= uint8_t(~kDown); flipped = 1; }
    if (scoring == Scoring::Standard) {
        if (from == Waste && is_tab(to)) score += 5;
        if (is_found(to) && !is_found(from)) score += 10;
        if (is_found(from) && is_tab(to)) score -= 15;
        if (flipped) score += 5;
        if (score < 0) score = 0;
    } else if (scoring == Scoring::Vegas) {
        if (is_found(to) && !is_found(from)) score += 5;
        if (is_found(from) && !is_found(to)) score -= 5;
    }
    log_step(*this, uint8_t(from), uint8_t(to), uint8_t(count), 0, flipped, before, passes);
    ++moves;
    return true;
}

bool Game::undo()
{
    if (!undo_n) return false;
    const Step s = undo_log[--undo_n];
    if (s.kind == 1) {                      // draw: waste back onto the stock
        for (int i = 0; i < s.count; ++i) {
            Stack& w = pile[Waste];
            Stack& st = pile[Stock];
            st.c[st.n++] = uint8_t(w.c[--w.n] | kDown);
        }
    } else if (s.kind == 2) {               // recycle: stock back onto the waste
        for (int i = 0; i < s.count; ++i) {
            Stack& w = pile[Waste];
            Stack& st = pile[Stock];
            w.c[w.n++] = uint8_t(st.c[--st.n] & ~kDown);
        }
    } else {
        Stack& f = pile[s.from];
        Stack& t = pile[s.to];
        if (s.flipped && f.n) f.c[f.n - 1] |= kDown;
        const int base = t.n - s.count;
        for (int i = 0; i < s.count; ++i) f.c[f.n++] = t.c[base + i];
        t.n = uint8_t(base);
    }
    score = s.score;
    passes = s.passes;
    if (moves) --moves;
    return true;
}

bool Game::won() const
{
    for (int f = 0; f < 4; ++f) if (pile[Found0 + f].n != 13) return false;
    return true;
}

bool Game::can_finish() const
{
    if (pile[Stock].n || pile[Waste].n) return false;
    for (int t = 0; t < 7; ++t) if (first_up(Tab0 + t) != 0) return false;
    return !won();
}

bool Game::finish_step()
{
    // The lowest card that can go up, so the foundations rise evenly
    int best = -1, best_rank = 99;
    for (int t = 0; t < 7; ++t) {
        const Stack& s = pile[Tab0 + t];
        if (!s.n) continue;
        for (int f = 0; f < 4; ++f)
            if (can_move(Tab0 + t, s.n - 1, Found0 + f) && rank(s.top()) < best_rank) {
                best = t; best_rank = rank(s.top());
            }
    }
    if (best < 0) return false;
    const Stack& s = pile[Tab0 + best];
    for (int f = 0; f < 4; ++f)
        if (move(Tab0 + best, s.n - 1, Found0 + f)) return true;
    return false;
}

int Game::best_target(int from, int idx) const
{
    if (idx == pile[from].n - 1 && can_move(from, idx, found_for(pile[from].c[idx]))) return found_for(pile[from].c[idx]);
    for (int t = 0; t < 7; ++t) {
        const int to = Tab0 + t;
        // A King already at the bottom of its column doesn't move to an empty one
        if (is_tab(from) && idx == 0 && !pile[to].n) continue;
        if (can_move(from, idx, to)) return to;
    }
    return -1;
}

bool Game::hint(int* from, int* idx, int* to) const
{
    // 1) anything to a foundation; 2) a tableau run that turns up a card or
    // empties a column for a King; 3) the waste card onto the tableau;
    // 4) turn the stock
    for (int p = Waste; p < kPiles; ++p) {
        if (is_found(p) || !pile[p].n) continue;
        const int i = pile[p].n - 1;
        for (int f = 0; f < 4; ++f)
            if (can_move(p, i, Found0 + f)) { *from = p; *idx = i; *to = Found0 + f; return true; }
    }
    for (int t = 0; t < 7; ++t) {
        const int p = Tab0 + t, i = first_up(p);
        if (i >= pile[p].n || i == 0) continue;          // nothing hidden under it
        for (int u = 0; u < 7; ++u)
            if (can_move(p, i, Tab0 + u)) { *from = p; *idx = i; *to = Tab0 + u; return true; }
    }
    if (pile[Waste].n) {
        const int i = pile[Waste].n - 1;
        for (int u = 0; u < 7; ++u)
            if (can_move(Waste, i, Tab0 + u)) { *from = Waste; *idx = i; *to = Tab0 + u; return true; }
    }
    if (can_draw()) { *from = Stock; *idx = 0; *to = Waste; return true; }
    return false;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "SOL1", 4); n = 4;
    for (const Stack& s : pile) { buf[n++] = s.n; memcpy(buf + n, s.c, kMaxPile); n += kMaxPile; }
    buf[n++] = draw;
    buf[n++] = uint8_t(scoring);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(seed >> (8 * k));
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(uint32_t(score) >> (8 * k));
    buf[n++] = passes;
    buf[n++] = uint8_t(moves); buf[n++] = uint8_t(moves >> 8);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "SOL1", 4) != 0) return false;
    size_t n = 4;
    Stack tmp[kPiles];
    int total = 0;
    bool seen[52] = {};
    for (Stack& s : tmp) {
        s.n = buf[n++];
        if (s.n > kMaxPile) return false;
        memcpy(s.c, buf + n, kMaxPile); n += kMaxPile;
        for (int i = 0; i < s.n; ++i) {
            const int c = s.c[i] & 0x3F;
            if (c >= 52 || seen[c]) return false;
            seen[c] = true;
            ++total;
        }
    }
    if (total != 52) return false;
    // Foundations in suit order (saves from before the fixed order)
    Stack found[4];
    for (int f = 0; f < 4; ++f) {
        const Stack& s = tmp[Found0 + f];
        if (!s.n) continue;
        const int slot = found_for(s.c[0]) - Found0;
        if (found[slot].n) return false;
        found[slot] = s;
    }
    for (int f = 0; f < 4; ++f) tmp[Found0 + f] = found[f];
    for (int i = 0; i < kPiles; ++i) pile[i] = tmp[i];
    draw = buf[n++] == 1 ? 1 : 3;
    const uint8_t sc = buf[n++];
    scoring = sc <= 2 ? Scoring(sc) : Scoring::Standard;
    seed = 0;
    for (int k = 0; k < 4; ++k) seed |= uint32_t(buf[n++]) << (8 * k);
    uint32_t s = 0;
    for (int k = 0; k < 4; ++k) s |= uint32_t(buf[n++]) << (8 * k);
    score = int32_t(s);
    passes = buf[n++];
    moves = uint16_t(buf[n] | (buf[n + 1] << 8));
    undo_n = 0;
    return true;
}

} // namespace solitaire
