#include "sudoku_game.h"

#include <cstring>

namespace sudoku {


void Game::start(Difficulty d, sudoku::Rng& rng)
{
    diff_ = d;
    sudoku::generate(d, rng, puzzle_, solution_);
    restart();
}

void Game::start_with(Difficulty d, const sudoku::Grid& puzzle, const sudoku::Grid& solution)
{
    diff_ = d;
    puzzle_ = puzzle;
    solution_ = solution;
    restart();
}

void Game::restart()
{
    for (int i = 0; i < N; ++i) { value_[i] = puzzle_.c[i]; notes_[i] = 0; hinted_[i] = 0; }
    elapsed_s_ = 0;
    hints_used_ = 0;
    undo_n_ = 0;
    group_ = 0;
    active_ = true;
}

bool Game::conflict(int i) const
{
    const uint8_t v = value_[i];
    if (!v) return false;
    for (int j = 0; j < N; ++j)
        if (j != i && value_[j] == v && same_unit(i, j)) return true;
    return false;
}

int Game::placed_correct(int d) const
{
    int n = 0;
    for (int i = 0; i < N; ++i) n += (value_[i] == d && solution_.c[i] == d);
    return n;
}

int Game::count(int d) const
{
    int n = 0;
    for (int i = 0; i < N; ++i) n += (value_[i] == d);
    return n;
}

bool Game::solved() const
{
    if (!active_) return false;
    for (int i = 0; i < N; ++i) if (value_[i] != solution_.c[i]) return false;
    return true;
}

void Game::record(int i)
{
    if (undo_n_ == kUndoCap) {
        // Full: drop the oldest quarter. Groups split at the cut are only a
        // problem for actions nobody can reach by undo any more.
        constexpr int drop = kUndoCap / 4;
        memmove(undo_, undo_ + drop, (kUndoCap - drop) * sizeof(Change));
        undo_n_ -= drop;
    }
    undo_[undo_n_++] = Change{static_cast<uint8_t>(i), value_[i], notes_[i], group_};
}

void Game::clear_peer_notes(int i, int d)
{
    // d can no longer be a candidate anywhere cell i sees.
    const uint16_t bit = 1u << d;
    for (int j = 0; j < N; ++j) {
        if (j != i && (notes_[j] & bit) && same_unit(i, j)) {
            record(j);
            notes_[j] &= ~bit;
        }
    }
}

bool Game::enter(int i, int d, bool notes_mode)
{
    if (!active_ || i < 0 || i >= N || d < 1 || d > 9 || locked(i)) return false;

    if (notes_mode) {
        if (value_[i]) return false;            // notes only on empty cells
        begin_action();
        record(i);
        notes_[i] ^= (1u << d);
        return true;
    }

    begin_action();
    record(i);
    if (value_[i] == d) {                       // same digit again: clear it
        value_[i] = 0;
        return true;
    }
    value_[i] = d;
    notes_[i] = 0;
    clear_peer_notes(i, d);
    return true;
}

bool Game::erase(int i)
{
    if (!active_ || i < 0 || i >= N || locked(i)) return false;
    if (!value_[i] && !notes_[i]) return false;
    begin_action();
    record(i);
    value_[i] = 0;
    notes_[i] = 0;
    return true;
}

bool Game::undo()
{
    if (!undo_n_) return false;
    const uint16_t g = undo_[undo_n_ - 1].group;
    while (undo_n_ && undo_[undo_n_ - 1].group == g) {
        const Change& c = undo_[--undo_n_];
        value_[c.idx] = c.value;
        notes_[c.idx] = c.notes;
        // Hinted cells are locked, so the only recorded change to one is the
        // hint itself: undoing it un-hints the cell.
        hinted_[c.idx] = 0;
    }
    return true;
}

bool Game::needs_hint(int i) const
{
    return !locked(i) && value_[i] != solution_.c[i];
}

int Game::hint_target(int preferred) const
{
    if (!active_ || solved()) return -1;
    if (preferred >= 0 && preferred < N && needs_hint(preferred)) return preferred;
    for (int i = 0; i < N; ++i)
        if (value_[i] && needs_hint(i)) return i;          // a wrong entry
    // No mistakes on the board, so candidates from the current values are
    // trustworthy: point at the empty cell with the fewest of them.
    int best = -1, best_n = 10;
    for (int i = 0; i < N; ++i) {
        if (value_[i]) continue;
        uint16_t used = 0;
        for (int j = 0; j < N; ++j)
            if (value_[j] && same_unit(i, j)) used |= 1u << value_[j];
        const int n = 9 - __builtin_popcount(used & 0x3FE);
        if (n < best_n) { best = i; best_n = n; }
    }
    return best;
}

bool Game::apply_hint(int i)
{
    if (!active_ || i < 0 || i >= N || !needs_hint(i)) return false;
    begin_action();
    record(i);
    value_[i] = solution_.c[i];
    notes_[i] = 0;
    hinted_[i] = 1;
    if (hints_used_ < 255) ++hints_used_;
    clear_peer_notes(i, value_[i]);
    return true;
}

// ---- Save layout (little-endian) --------------------------------------------
//   u32 magic 'SUD2' | u8 difficulty | u8 active | u16 undo_n | u16 group
//   u32 elapsed_s | u8 hints_used | u8 hinted bits[11]
//   puzzle[81] | solution[81] | value[81] | notes u16[81]
//   Change[undo_n] (6 bytes each: idx, value, notes u16, group u16)
// 'SUD1' (alpha.3-.6) is the same without the hint fields, and still loads.
namespace {
constexpr uint32_t kMagic1 = 0x31445553;  // "SUD1"
constexpr uint32_t kMagic  = 0x32445553;  // "SUD2"
constexpr size_t   kHeader1 = 4 + 1 + 1 + 2 + 2 + 4;
constexpr size_t   kHeader = kHeader1 + 1 + 11;
constexpr size_t   kBody = 81 * 3 + 81 * 2;

template <class T> void put(uint8_t*& p, T v) { memcpy(p, &v, sizeof v); p += sizeof v; }
template <class T> T get(const uint8_t*& p) { T v; memcpy(&v, p, sizeof v); p += sizeof v; return v; }
} // namespace

size_t Game::max_serialized_size() { return kHeader + kBody + kUndoCap * 6; }

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    const size_t need = kHeader + kBody + size_t(undo_n_) * 6;
    if (cap < need) return 0;
    uint8_t* p = buf;
    put<uint32_t>(p, kMagic);
    put<uint8_t>(p, static_cast<uint8_t>(diff_));
    put<uint8_t>(p, active_ ? 1 : 0);
    put<uint16_t>(p, undo_n_);
    put<uint16_t>(p, group_);
    put<uint32_t>(p, elapsed_s_);
    put<uint8_t>(p, hints_used_);
    for (int b = 0; b < 11; ++b) {
        uint8_t bits = 0;
        for (int k = 0; k < 8; ++k) {
            const int i = b * 8 + k;
            if (i < N && hinted_[i]) bits |= 1u << k;
        }
        put<uint8_t>(p, bits);
    }
    memcpy(p, puzzle_.c, 81);   p += 81;
    memcpy(p, solution_.c, 81); p += 81;
    memcpy(p, value_, 81);      p += 81;
    for (int i = 0; i < N; ++i) put<uint16_t>(p, notes_[i]);
    for (int k = 0; k < undo_n_; ++k) {
        put<uint8_t>(p, undo_[k].idx);
        put<uint8_t>(p, undo_[k].value);
        put<uint16_t>(p, undo_[k].notes);
        put<uint16_t>(p, undo_[k].group);
    }
    return need;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kHeader1 + kBody) return false;
    const uint8_t* p = buf;
    const uint32_t magic = get<uint32_t>(p);
    if (magic != kMagic && magic != kMagic1) return false;
    const size_t header = (magic == kMagic) ? kHeader : kHeader1;
    const uint8_t d = get<uint8_t>(p);
    const uint8_t act = get<uint8_t>(p);
    const uint16_t un = get<uint16_t>(p);
    const uint16_t grp = get<uint16_t>(p);
    const uint32_t el = get<uint32_t>(p);
    if (d > 3 || un > kUndoCap || len < header + kBody + size_t(un) * 6) return false;
    uint8_t hints = 0, hbits[11] = {};
    if (magic == kMagic) {
        hints = get<uint8_t>(p);
        for (int b = 0; b < 11; ++b) hbits[b] = get<uint8_t>(p);
    }

    // Validate into a scratch copy, then commit. Static: a Game is ~4 KB,
    // too big for the ESP32's default task stack.
    static Game g;
    g.diff_ = static_cast<Difficulty>(d);
    g.active_ = act != 0;
    g.undo_n_ = un;
    g.group_ = grp;
    g.elapsed_s_ = el;
    g.hints_used_ = hints;
    for (int i = 0; i < N; ++i) g.hinted_[i] = (hbits[i / 8] >> (i % 8)) & 1;
    memcpy(g.puzzle_.c, p, 81);   p += 81;
    memcpy(g.solution_.c, p, 81); p += 81;
    memcpy(g.value_, p, 81);      p += 81;
    for (int i = 0; i < N; ++i) {
        g.notes_[i] = get<uint16_t>(p) & 0x3FE;
        if (g.value_[i] > 9 || g.puzzle_.c[i] > 9 || g.solution_.c[i] < 1 || g.solution_.c[i] > 9)
            return false;
        if (g.puzzle_.c[i] && g.puzzle_.c[i] != g.solution_.c[i]) return false;
    }
    for (int k = 0; k < un; ++k) {
        Change c;
        c.idx = get<uint8_t>(p);
        c.value = get<uint8_t>(p);
        c.notes = get<uint16_t>(p);
        c.group = get<uint16_t>(p);
        if (c.idx >= N || c.value > 9) return false;
        g.undo_[k] = c;
    }
    *this = g;
    return true;
}

} // namespace sudoku
