#include "minesweeper_core.h"

#include <cstring>
#include <new>

namespace mines {

const LevelSize kSizes[kLevels] = {{8, 10, 10}, {9, 11, 15}, {10, 11, 20}};

namespace {

constexpr int kMaxTries = 1500;            // boards tried before taking the best one

// 128-bit cell set
struct Bits {
    uint64_t a = 0, b = 0;
    void set(int i)        { if (i < 64) a |= 1ull << i; else b |= 1ull << (i - 64); }
    bool has(int i) const  { return i < 64 ? (a >> i) & 1 : (b >> (i - 64)) & 1; }
    bool empty() const     { return !a && !b; }
    int  count() const     { return __builtin_popcountll(a) + __builtin_popcountll(b); }
    bool subset_of(const Bits& o) const { return !(a & ~o.a) && !(b & ~o.b); }
    Bits minus(const Bits& o) const { Bits r; r.a = a & ~o.a; r.b = b & ~o.b; return r; }
    bool operator==(const Bits& o) const { return a == o.a && b == o.b; }
};

} // namespace

void Board::start(int lv)
{
    *this = Board{};
    level = static_cast<uint8_t>(lv < 0 ? 0 : lv >= kLevels ? kLevels - 1 : lv);
    w = kSizes[level].w;
    h = kSizes[level].h;
    mines = kSizes[level].mines;
}

int Board::flags() const
{
    int n = 0;
    for (int i = 0; i < cells(); ++i) n += cell[i] == Cell::Flag;
    return n;
}

int Board::neighbors(int i, int out[8]) const
{
    const int x = i % w, y = i / w;
    int n = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            const int nx = x + dx, ny = y + dy;
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) out[n++] = ny * w + nx;
        }
    return n;
}

void Board::set_counts()
{
    int nb[8];
    for (int i = 0; i < cells(); ++i) {
        int c = 0;
        const int n = neighbors(i, nb);
        for (int k = 0; k < n; ++k) c += mine[nb[k]];
        near[i] = static_cast<uint8_t>(c);
    }
}

void Board::place(int first, Rng& rng)
{
    const int total = cells();
    int nb[8];
    for (int attempt = 0; attempt < kMaxTries; ++attempt) {
        memset(mine, 0, sizeof mine);
        // Cells that may hold a mine: all but the first tap and its neighbours
        uint8_t pool[kMaxCells];
        int pn = 0;
        bool keep[kMaxCells] = {};
        keep[first] = true;
        const int n = neighbors(first, nb);
        for (int k = 0; k < n; ++k) keep[nb[k]] = true;
        for (int i = 0; i < total; ++i) if (!keep[i]) pool[pn++] = static_cast<uint8_t>(i);
        for (int m = 0; m < mines && pn > 0; ++m) {
            const int j = static_cast<int>(rng.next() % uint32_t(pn));
            mine[pool[j]] = 1;
            pool[j] = pool[--pn];
        }
        set_counts();
        if (solvable(*this, first)) break;    // otherwise try again (last try stays)
    }
    placed = true;
}

bool Board::open_one(int i)
{
    if (cell[i] != Cell::Hidden) return true;
    if (mine[i]) {
        cell[i] = Cell::Open;
        boom = static_cast<int16_t>(i);
        status = Status::Lost;
        return false;
    }
    int stack[kMaxCells];
    int sp = 0;
    cell[i] = Cell::Open;
    stack[sp++] = i;
    int nb[8];
    while (sp) {
        const int c = stack[--sp];
        if (near[c]) continue;
        const int n = neighbors(c, nb);
        for (int k = 0; k < n; ++k) {
            const int j = nb[k];
            if (cell[j] == Cell::Hidden && !mine[j]) {
                cell[j] = Cell::Open;
                stack[sp++] = j;
            }
        }
    }
    return true;
}

void Board::finish_if_won()
{
    if (status != Status::Playing) return;
    for (int i = 0; i < cells(); ++i)
        if (!mine[i] && cell[i] != Cell::Open) return;
    status = Status::Won;
    for (int i = 0; i < cells(); ++i) if (mine[i]) cell[i] = Cell::Flag;
}

bool Board::open(int i, Rng& rng)
{
    if (status != Status::Playing || i < 0 || i >= cells()) return false;
    if (cell[i] == Cell::Flag) return false;
    if (!placed) place(i, rng);
    bool opened = false;
    if (cell[i] == Cell::Hidden) {
        open_one(i);
        opened = true;
    } else if (near[i]) {
        // Chord: the flags around this number match it -> open the rest
        int nb[8];
        const int n = neighbors(i, nb);
        int f = 0;
        for (int k = 0; k < n; ++k) f += cell[nb[k]] == Cell::Flag;
        if (f != near[i]) return false;
        for (int k = 0; k < n; ++k)
            if (cell[nb[k]] == Cell::Hidden) { open_one(nb[k]); opened = true; }
    }
    if (opened) {
        ++moves;
        finish_if_won();
    }
    return opened;
}

void Board::toggle_flag(int i)
{
    if (status != Status::Playing || i < 0 || i >= cells()) return;
    if (cell[i] == Cell::Hidden) cell[i] = Cell::Flag;
    else if (cell[i] == Cell::Flag) cell[i] = Cell::Hidden;
}

// ---- Save: "MIN1", level, flags, status, boom (2), moves (2), pad, then per
// cell: mine and state ----------------------------------------------------------------
size_t Board::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memset(buf, 0, kSaveBytes);
    memcpy(buf, "MIN1", 4);
    buf[4] = level;
    buf[5] = placed ? 1 : 0;
    buf[6] = static_cast<uint8_t>(status);
    buf[7] = static_cast<uint8_t>(boom & 0xFF);
    buf[8] = static_cast<uint8_t>((boom >> 8) & 0xFF);
    buf[9] = static_cast<uint8_t>(moves & 0xFF);
    buf[10] = static_cast<uint8_t>(moves >> 8);
    for (int i = 0; i < kMaxCells; ++i) {
        buf[12 + 2 * i] = mine[i];
        buf[13 + 2 * i] = static_cast<uint8_t>(cell[i]);
    }
    return kSaveBytes;
}

bool Board::deserialize(const uint8_t* buf, size_t len)
{
    if (len != kSaveBytes || memcmp(buf, "MIN1", 4) != 0) return false;
    if (buf[4] >= kLevels || buf[6] > 2) return false;
    Board b;
    b.start(buf[4]);
    b.placed = buf[5] != 0;
    b.status = static_cast<Status>(buf[6]);
    b.boom = static_cast<int16_t>(buf[7] | (buf[8] << 8));
    b.moves = static_cast<uint16_t>(buf[9] | (buf[10] << 8));
    int count = 0;
    for (int i = 0; i < kMaxCells; ++i) {
        const uint8_t m = buf[12 + 2 * i], c = buf[13 + 2 * i];
        if (m > 1 || c > 2) return false;
        if (i >= b.cells() && (m || c)) return false;
        b.mine[i] = m;
        b.cell[i] = static_cast<Cell>(c);
        count += m;
    }
    if (b.placed && count != b.mines) return false;
    if (b.boom < -1 || b.boom >= b.cells()) return false;
    b.set_counts();
    *this = b;
    return true;
}

// ---- Logic-only solver -----------------------------------------------------------------
bool solvable(const Board& src, int first)
{
    const int total = src.cells();
    // known: 0 unknown, 1 safe (opened), 2 mine
    uint8_t known[kMaxCells] = {};
    int nb[8];
    int safe_left = total - src.mines, mines_found = 0;

    // Opens a safe cell like the game does (zeros spread)
    auto open = [&](int start) {
        int stack[kMaxCells];
        int sp = 0;
        if (known[start]) return;
        known[start] = 1;
        --safe_left;
        stack[sp++] = start;
        while (sp) {
            const int c = stack[--sp];
            if (src.near[c]) continue;
            const int n = src.neighbors(c, nb);
            for (int k = 0; k < n; ++k)
                if (!known[nb[k]]) { known[nb[k]] = 1; --safe_left; stack[sp++] = nb[k]; }
        }
    };
    if (src.mine[first]) return false;
    open(first);

    struct Rule { Bits cells; int need; };
    struct Owner {                         // ~2.9 KB on the heap, not the stack
        Rule* p = new (std::nothrow) Rule[kMaxCells];
        ~Owner() { delete[] p; }
    } own;
    Rule* rules = own.p;
    if (!rules) return true;               // out of memory: take the board as it is
    while (safe_left > 0) {
        bool progress = false;
        // Rules: each open number with hidden neighbours
        int nr = 0;
        Bits all_unknown;
        for (int i = 0; i < total; ++i) if (!known[i]) all_unknown.set(i);
        for (int i = 0; i < total; ++i) {
            if (known[i] != 1 || !src.near[i]) continue;
            Rule r;
            r.need = src.near[i];
            const int n = src.neighbors(i, nb);
            for (int k = 0; k < n; ++k) {
                if (known[nb[k]] == 2) --r.need;
                else if (!known[nb[k]]) r.cells.set(nb[k]);
            }
            if (r.cells.empty()) continue;
            bool dup = false;
            for (int q = 0; q < nr && !dup; ++q) dup = rules[q].cells == r.cells;
            if (!dup) rules[nr++] = r;
        }
        auto apply = [&](const Bits& s, bool mine) {
            for (int i = 0; i < total; ++i) {
                if (!s.has(i) || known[i]) continue;
                if (mine) { known[i] = 2; ++mines_found; }
                else open(i);
                progress = true;
            }
        };
        // Single numbers
        for (int q = 0; q < nr; ++q) {
            if (rules[q].need == 0) apply(rules[q].cells, false);
            else if (rules[q].need == rules[q].cells.count()) apply(rules[q].cells, true);
        }
        if (progress) continue;
        // Subset rule: A inside B -> B minus A holds need(B) - need(A) mines
        for (int p = 0; p < nr && !progress; ++p)
            for (int q = 0; q < nr && !progress; ++q) {
                if (p == q || !rules[p].cells.subset_of(rules[q].cells)) continue;
                const Bits rest = rules[q].cells.minus(rules[p].cells);
                const int need = rules[q].need - rules[p].need;
                if (rest.empty()) continue;
                if (need == 0) apply(rest, false);
                else if (need == rest.count()) apply(rest, true);
            }
        if (progress) continue;
        // Mine count: all mines found -> the rest is safe; or every unknown is a mine
        const int left = src.mines - mines_found;
        if (left == 0) apply(all_unknown, false);
        else if (left == all_unknown.count()) apply(all_unknown, true);
        if (!progress) return false;
    }
    return true;
}

} // namespace mines
