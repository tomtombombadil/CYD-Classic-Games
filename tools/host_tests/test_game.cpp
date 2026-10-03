// Host-side checks for src/games/sudoku/game.*  (built and run by CI)
#include "../../src/games/sudoku/sudoku_game.h"
#include <cstdio>
#include <vector>

using namespace sudoku;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    sudoku::Rng rng(777);
    static Game g;
    g.start(Difficulty::Easy, rng);

    int empty = -1, given_cell = -1;
    for (int i = 0; i < N; ++i) {
        if (g.given(i)) { if (given_cell < 0) given_cell = i; }
        else if (empty < 0) empty = i;
    }
    CHECK(!g.enter(given_cell, 1, false));          // givens are locked
    CHECK(!g.erase(given_cell));

    // Find a peer of `empty` that is also empty and put notes there
    int peer = -1;
    for (int j = 0; j < N; ++j) if (j != empty && !g.given(j) && sudoku::same_unit(empty, j)) { peer = j; break; }
    CHECK(peer >= 0);
    CHECK(g.enter(peer, 5, true));
    CHECK(g.enter(peer, 7, true));
    CHECK(g.has_note(peer, 5) && g.has_note(peer, 7));

    // Placing 5 in `empty` auto-clears note 5 in the peer, keeps 7
    CHECK(g.enter(empty, 5, false));
    CHECK(g.value(empty) == 5);
    CHECK(!g.has_note(peer, 5) && g.has_note(peer, 7));

    // One undo restores both the value and the cleared note
    CHECK(g.undo());
    CHECK(g.value(empty) == 0 && g.has_note(peer, 5));

    // Same digit twice toggles it off
    CHECK(g.enter(empty, 3, false));
    CHECK(g.enter(empty, 3, false));
    CHECK(g.value(empty) == 0);

    // Notes not allowed on a filled cell
    CHECK(g.enter(empty, 4, false));
    CHECK(!g.enter(empty, 2, true));
    CHECK(g.erase(empty) && g.value(empty) == 0);

    // count() includes wrong entries; placed_correct() does not
    {
        int wrong_digit = 0;
        sudoku::Grid pz{}, sl{};
        for (int i = 0; i < N; ++i) pz.c[i] = g.given(i) ? g.value(i) : 0;
        sudoku::count_solutions(pz, 2, &sl);
        wrong_digit = sl.c[empty] % 9 + 1;           // any digit but the right one
        const int before_count = g.count(wrong_digit), before_ok = g.placed_correct(wrong_digit);
        g.enter(empty, wrong_digit, false);
        CHECK(g.count(wrong_digit) == before_count + 1);
        CHECK(g.placed_correct(wrong_digit) == before_ok);
        g.undo();
        CHECK(g.count(wrong_digit) == before_count);
    }

    // Conflicts: put the same digit as a given peer into the cell
    int clash_digit = 0;
    for (int j = 0; j < N; ++j) if (g.given(j) && sudoku::same_unit(empty, j)) { clash_digit = g.value(j); break; }
    CHECK(clash_digit);
    g.enter(empty, clash_digit, false);
    CHECK(g.conflict(empty));
    g.undo();
    CHECK(!g.conflict(empty));

    // Save / load round trip keeps everything, including undo history
    g.enter(peer, 9, true);
    std::vector<uint8_t> buf(Game::max_serialized_size());
    size_t n = g.serialize(buf.data(), buf.size());
    CHECK(n > 0);
    static Game h;
    CHECK(h.deserialize(buf.data(), n));
    for (int i = 0; i < N; ++i) CHECK(h.value(i) == g.value(i) && h.notes(i) == g.notes(i) && h.given(i) == g.given(i));
    CHECK(h.can_undo());
    // Old 'SUD1' saves (no hint fields) still load
    {
        std::vector<uint8_t> v1(buf.begin(), buf.begin() + n);
        v1.erase(v1.begin() + 14, v1.begin() + 26);   // drop hints_used + 11 bit bytes
        v1[0] = 'S'; v1[1] = 'U'; v1[2] = 'D'; v1[3] = '1';
        static Game o;
        CHECK(o.deserialize(v1.data(), v1.size()));
        for (int i = 0; i < N; ++i) CHECK(o.value(i) == g.value(i) && o.notes(i) == g.notes(i) && !o.hinted(i));
        CHECK(o.hints_used() == 0 && o.can_undo());
    }
    buf[0] ^= 0xFF;
    CHECK(!h.deserialize(buf.data(), n));            // corrupt file rejected

    // Solving: fill every cell from the solver; solved() becomes true
    sudoku::Grid puzzle{}, sol{};
    for (int i = 0; i < N; ++i) puzzle.c[i] = g.given(i) ? g.value(i) : 0;
    CHECK(sudoku::count_solutions(puzzle, 2, &sol) == 1);
    for (int i = 0; i < N; ++i) if (!g.given(i)) g.enter(i, sol.c[i], false);
    CHECK(g.solved());
    for (int d = 1; d <= 9; ++d) CHECK(g.placed_correct(d) == 9);

    // ---- Hints ----
    {
        static Game hg;
        sudoku::Rng r2(99);
        hg.start(Difficulty::Medium, r2);
        sudoku::Grid pz{}, sl{};
        for (int i = 0; i < N; ++i) pz.c[i] = hg.given(i) ? hg.value(i) : 0;
        CHECK(sudoku::count_solutions(pz, 2, &sl) == 1);

        // No mistakes: target is an empty cell, and on a fresh puzzle the
        // easiest one is a naked single
        int t = hg.hint_target(-1);
        CHECK(t >= 0 && hg.value(t) == 0);

        // A selected empty cell is preferred
        int other = -1;
        for (int i = 0; i < N; ++i) if (!hg.given(i) && i != t) { other = i; break; }
        CHECK(hg.hint_target(other) == other);

        // A wrong entry elsewhere takes priority over an empty cell
        const int wrong_d = sl.c[other] % 9 + 1;
        hg.enter(other, wrong_d, false);
        CHECK(hg.hint_target(-1) == other);
        CHECK(hg.hint_target(t) == t);                    // ...unless another cell is selected

        // Applying fixes it, locks it, counts it, and clears peer notes
        int peer = -1;
        for (int j = 0; j < N; ++j) if (!hg.given(j) && j != other && !hg.value(j) && sudoku::same_unit(other, j)) { peer = j; break; }
        CHECK(peer >= 0);
        hg.enter(peer, sl.c[other], true);
        CHECK(hg.apply_hint(other));
        CHECK(hg.value(other) == sl.c[other] && hg.hinted(other) && hg.locked(other));
        CHECK(hg.hints_used() == 1);
        CHECK(!hg.has_note(peer, sl.c[other]));
        CHECK(!hg.enter(other, 1, false) && !hg.erase(other));   // locked
        CHECK(!hg.apply_hint(other));                            // already right

        // Survives save/load
        std::vector<uint8_t> b2(Game::max_serialized_size());
        size_t n2 = hg.serialize(b2.data(), b2.size());
        static Game hl;
        CHECK(hl.deserialize(b2.data(), n2));
        CHECK(hl.hinted(other) && hl.hints_used() == 1 && hl.value(other) == sl.c[other]);

        // Undo removes the hint (count stays) and restores the wrong entry + note
        CHECK(hg.undo());
        CHECK(!hg.hinted(other) && hg.value(other) == wrong_d && hg.has_note(peer, sl.c[other]));
        CHECK(hg.hints_used() == 1);

        // Hinting until solved
        int guard = 0;
        for (int tgt; (tgt = hg.hint_target(-1)) >= 0 && guard < 100; ++guard) CHECK(hg.apply_hint(tgt));
        CHECK(hg.solved());
        CHECK(hg.hint_target(-1) == -1);
    }

    // Undo history overflow is handled
    g.restart();
    for (int k = 0; k < 2000; ++k) g.enter(empty, 1 + k % 9, true);
    CHECK(g.can_undo());

    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
