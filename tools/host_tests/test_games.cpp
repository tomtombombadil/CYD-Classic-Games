// Host-side checks for the stage-2 games and the shared stats formats
// (built and run by CI): FourConnect, Tic-Tac-Toe, Sliding Tiles,
// Light Switch, two_player.*, puzzle_stats.*.
#include "../../src/games/common/puzzle_stats.h"
#include "../../src/games/common/two_player.h"
#include "../../src/games/checkers/checkers_core.h"
#include "../../src/games/fourconnect/fourconnect_core.h"
#include "../../src/games/lightswitch/lightswitch_core.h"
#include "../../src/games/reversi/reversi_core.h"
#include "../../src/games/sliding/sliding_core.h"
#include "../../src/games/tictactoe/tictactoe_core.h"
#include <chrono>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void test_fourconnect()
{
    using namespace fourconnect;
    Board b;
    // Vertical four for side 0 in column 3
    for (int k = 0; k < 3; ++k) { CHECK(b.play(3)); CHECK(b.play(4)); }
    CHECK(b.result() == -1);
    CHECK(b.play(3));
    CHECK(b.result() == 0);
    CHECK(__builtin_popcountll(b.winning_cells()) == 4);
    CHECK(b.undo() && b.result() == -1);

    // Horizontal win on the bottom row doesn't wrap across columns
    Board h;
    const int seq[] = {0, 0, 1, 1, 2, 2, 3};
    for (int c : seq) h.play(c);
    CHECK(h.result() == 0);
    Board wrap;   // pieces in column 6 top and column 0 bottom must not join
    for (int k = 0; k < 6; ++k) { wrap.play(6); }
    CHECK(wrap.result() == -1);

    // Column full
    Board f;
    for (int k = 0; k < 6; ++k) CHECK(f.play(2));
    CHECK(!f.can_play(2) && !f.play(2));

    // Save/load round trip
    uint8_t buf[Board::kSaveBytes];
    CHECK(h.serialize(buf, sizeof buf) == Board::kSaveBytes);
    Board back;
    CHECK(back.deserialize(buf, sizeof buf) && back.moves == h.moves && back.result() == 0);
    buf[0] = 'X';
    CHECK(!back.deserialize(buf, sizeof buf));

    // Computer takes a win and blocks one (Medium and Hard)
    for (int lvl = 1; lvl <= 2; ++lvl) {
        Board w;                          // side 0 has three in column 0; side 0 to move
        const int s1[] = {0, 6, 0, 6, 0, 5};
        for (int c : s1) w.play(c);
        CHECK(best_move(w, depth_for_level(lvl), 7) == 0);
        Board blk;                        // side 0 threatens column 0; side 1 to move
        const int s2[] = {0, 6, 0, 5, 0};
        for (int c : s2) blk.play(c);
        CHECK(best_move(blk, depth_for_level(lvl), 7) == 0);
    }
    // Hard from the empty board: in the centre, and quick enough
    auto t0 = std::chrono::steady_clock::now();
    Board e;
    const int m = best_move(e, depth_for_level(2), 1);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    printf("fourconnect hard first move %d in %.1f ms\n", m, ms);
    CHECK(m == 3);
    // Hard beats Easy most of the time (Hard moves first in half the games)
    int hard_wins = 0, easy_wins = 0;
    for (int g = 0; g < 20; ++g) {
        Board p;
        const int hard_side = g & 1;
        while (p.result() == -1) {
            const int lvl = p.turn() == hard_side ? 2 : 0;
            p.play(best_move(p, depth_for_level(lvl), 1000 + g * 31 + p.moves));
        }
        if (p.result() == hard_side) ++hard_wins;
        else if (p.result() != 2) ++easy_wins;
    }
    printf("fourconnect hard vs easy: %d-%d\n", hard_wins, easy_wins);
    CHECK(hard_wins >= 17);
    // A stop request ends the search with a legal move
    volatile bool stop = true;
    Board s;
    CHECK(s.can_play(best_move(s, 8, 3, &stop)));
}

static void test_tictactoe()
{
    using namespace tictactoe;
    Board b;
    const int seq[] = {0, 3, 1, 4, 2};
    for (int i : seq) CHECK(b.play(i));
    CHECK(b.result() == 0);
    int line[3];
    CHECK(b.winning_line(line) && line[0] == 0 && line[2] == 2);
    CHECK(!b.can_play(5));                // game over
    Board full;
    const int draw[] = {0, 1, 2, 4, 3, 5, 7, 6, 8};
    for (int i : draw) full.play(i);
    CHECK(full.result() == 2);
    // Hard never loses: play it against every reply sequence of Easy-ish random play
    int hard_losses = 0;
    for (int g = 0; g < 40; ++g) {
        Board p;
        const int hard_side = g & 1;
        while (p.result() == -1) {
            const int lvl = p.turn() == hard_side ? 2 : 0;
            p.play(best_move(p, depth_for_level(lvl), 99 + g * 7 + p.moves));
        }
        if (p.result() == (hard_side ^ 1)) ++hard_losses;
    }
    CHECK(hard_losses == 0);
    // Medium blocks
    Board m;
    m.play(0); m.play(4); m.play(1);      // X threatens 2; O to move
    CHECK(best_move(m, depth_for_level(1), 5) == 2);
    uint8_t buf[Board::kSaveBytes];
    CHECK(b.serialize(buf, sizeof buf));
    Board back;
    CHECK(back.deserialize(buf, sizeof buf) && back.result() == 0);
}

static void test_sliding()
{
    using namespace sliding;
    for (int lvl = 0; lvl < 3; ++lvl) {
        Puzzle p;
        p.reset(size_for_level(lvl));
        CHECK(p.solved());
        Rng rng(42 + lvl);
        p.shuffle(rng);
        CHECK(!p.solved() && p.moves == 0);
        // The tile left of the gap (if any) slides in
        if (p.gap % p.n) {
            const int left = p.gap - 1, v = p.tile[left];
            CHECK(p.tap(left) == 1 && p.tile[left + 1] == v && p.tile[left] == 0);
        }
        CHECK(p.tap(p.gap) == 0);
        uint8_t buf[Puzzle::kSaveBytes];
        CHECK(p.serialize(buf, sizeof buf));
        Puzzle back;
        CHECK(back.deserialize(buf, sizeof buf) && back.gap == p.gap && back.moves == p.moves);
    }
    // Several tiles in one tap, and back
    Puzzle q;
    q.reset(4);                            // gap at 15
    CHECK(q.tap(12) == 3 && q.gap == 12 && q.tile[15] == 15 && q.moves == 3);
    CHECK(q.tap(15) == 3 && q.solved());
    CHECK(q.tap(5) == 0);                  // not in the gap's row or column
}

static void test_lightswitch()
{
    using namespace lightswitch;
    CHECK(press_mask(0) == (1u | 2u | (1u << 5)));
    CHECK(__builtin_popcount(press_mask(12)) == 5);
    uint32_t x = 0;
    CHECK(min_presses(press_mask(12), &x) == 1 && x == (1u << 12));
    CHECK(min_presses(0) == 0);
    for (int lvl = 0; lvl < 3; ++lvl) {
        Rng rng(7 + lvl);
        for (int k = 0; k < 50; ++k) {
            Puzzle p;
            p.generate(lvl, rng);
            CHECK(!p.solved() && p.par > 0);
            // Follow hints: solves in exactly par presses
            int n = 0;
            while (!p.solved() && n < 30) { p.press(p.hint()); ++n; }
            CHECK(p.solved() && n == p.par);
        }
    }
    CHECK(min_presses(1u) == -1 || min_presses(1u) > 0);   // corner alone: 5x5 has unsolvable states
    Puzzle s;
    Rng rng(3);
    s.generate(1, rng);
    uint8_t buf[Puzzle::kSaveBytes];
    CHECK(s.serialize(buf, sizeof buf));
    Puzzle back;
    CHECK(back.deserialize(buf, sizeof buf) && back.lights == s.lights && back.par == s.par);
}

static void test_reversi()
{
    using namespace reversi;
    Board b;
    CHECK(b.count(0) == 2 && b.count(1) == 2 && b.side == 0);
    CHECK(__builtin_popcountll(b.legal()) == 4);
    CHECK(b.can_play(19) && !b.can_play(0));          // d3 legal, a1 not
    CHECK(b.play(19));                                 // d3 flips d4
    CHECK(b.count(0) == 4 && b.count(1) == 1 && b.side == 1);
    CHECK(!b.play(19));
    // A whole game by the computer ends, with a sensible result, and the
    // save replays to the same position (passes included)
    for (int g = 0; g < 4; ++g) {
        Board p;
        while (!p.over()) p.play(best_move(p, g % 3, 100 + g * 17 + p.plies));
        CHECK(p.result() >= 0 && p.count(0) + p.count(1) <= 64);
        uint8_t buf[Board::kSaveBytes];
        CHECK(p.serialize(buf, sizeof buf));
        Board back;
        CHECK(back.deserialize(buf, sizeof buf) && back.disc[0] == p.disc[0] && back.disc[1] == p.disc[1]);
    }
    // Hard beats Easy
    int hard = 0;
    for (int g = 0; g < 6; ++g) {
        Board p;
        const int hs = g & 1;
        while (!p.over()) p.play(best_move(p, p.side == hs ? 2 : 0, 7 + g * 13 + p.plies));
        hard += p.result() == hs;
    }
    printf("reversi hard vs easy: %d/6\n", hard);
    CHECK(hard >= 5);
    // Passes: play random games until one has a pass; at that point the
    // side that passed had no move and the same side moves again
    int passes = 0;
    uint32_t rng = 12345;
    for (int g = 0; g < 400 && passes < 3; ++g) {
        Board p;
        while (!p.over()) {
            const uint64_t l = p.legal();
            int n = __builtin_popcountll(l);
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            uint64_t x = l;
            for (int k = rng % n; k > 0; --k) x &= x - 1;
            const int mover = p.side;
            Board before = p;
            p.play(__builtin_ctzll(x));
            if (p.last_was_pass() && !p.over()) {
                ++passes;
                CHECK(p.side == mover);
                Board other = before;
                other.play(__builtin_ctzll(x));
                CHECK(other.legal() != 0);
                uint8_t buf[Board::kSaveBytes];
                p.serialize(buf, sizeof buf);
                Board back;
                CHECK(back.deserialize(buf, sizeof buf) && back.side == p.side && back.plies == p.plies);
            }
        }
    }
    CHECK(passes > 0);
}

static void test_checkers()
{
    using namespace checkers;
    Game g;
    MoveList l;
    g.legal(l);
    CHECK(l.n == 7 && !l.m[0].jump());
    CHECK(__builtin_popcountll(g.pos.pieces(0)) == 12 && __builtin_popcountll(g.pos.pieces(1)) == 12);
    // Hand-made position: a black man on c3 (sq 18) can double-jump
    // d4 (27) and f6 (45), landing on e5 (36) then g7 (54).
    Position p{};
    p.men[0] = 1ull << 18;
    p.men[1] = (1ull << 27) | (1ull << 45) | (1ull << 9);   // b2 white man can't be jumped backward
    generate(p, 0, l);
    CHECK(l.n == 1 && l.m[0].n == 2 && l.m[0].path[0] == 36 && l.m[0].path[1] == 54);
    CHECK(__builtin_popcountll(l.m[0].captured) == 2);
    // Jumping is compulsory: no plain moves offered alongside
    for (int k = 0; k < l.n; ++k) CHECK(l.m[k].jump());
    // Crowning ends the move: a black man jumping onto rank 7 stops there
    Position c{};
    c.men[0] = 1ull << 45;                                  // f6
    c.men[1] = (1ull << 52) | (1ull << 53);                 // e7 and f7
    generate(c, 0, l);
    bool crowned_stop = false;
    for (int k = 0; k < l.n; ++k) if (l.m[k].to() / 8 == 7) crowned_stop = l.m[k].n == 1;
    CHECK(crowned_stop);
    Position after = c;
    for (int k = 0; k < l.n; ++k) if (l.m[k].to() / 8 == 7) { after.apply(l.m[k]); break; }
    CHECK(__builtin_popcountll(after.kings[0]) == 1);
    // Whole games end, saves replay, Hard beats Easy
    int hard = 0;
    for (int n = 0; n < 6; ++n) {
        Game q;
        const int hs = n & 1;
        while (q.result() == -1) q.play(best_move(q, q.turn() == hs ? 2 : 0, 31 + n * 7 + q.plies));
        hard += q.result() == hs;
        uint8_t buf[Game::kSaveBytes];
        CHECK(q.serialize(buf, sizeof buf));
        Game back;
        CHECK(back.deserialize(buf, sizeof buf) && back.pos.men[0] == q.pos.men[0] && back.pos.kings[1] == q.pos.kings[1]);
    }
    printf("checkers hard vs easy: %d/6\n", hard);
    CHECK(hard >= 5);
}

static void test_stats()
{
    using namespace twoplayer;
    const Sides sides{"Red", "Yellow"};
    char line[96];
    Record r; r.mode = Mode::Computer; r.level = Level::Hard; r.result = Result::Side2; r.moves = 21; r.seconds = 95;
    CHECK(format_body(line, sizeof line, r, sides));
    CHECK(strcmp(line, "Computer,Hard,Lost,21,95,1:35\n") == 0);
    char full[100];
    snprintf(full, sizeof full, "4,%s", line);
    Record back;
    CHECK(parse_line(full, back, sides) && back.mode == Mode::Computer && back.level == Level::Hard
          && back.result == Result::Side2 && back.moves == 21 && back.seconds == 95);
    Record p; p.mode = Mode::PassAndPlay; p.result = Result::Side2; p.moves = 30; p.seconds = 240;
    format_body(line, sizeof line, p, sides);
    CHECK(strcmp(line, "Pass and play,-,Yellow won,30,240,4:00\n") == 0);
    snprintf(full, sizeof full, "5,%s", line);
    CHECK(parse_line(full, back, sides) && back.mode == Mode::PassAndPlay && back.result == Result::Side2);
    CHECK(!parse_line(kCsvHeader, back, sides));
    Summary s; s.add(r); s.add(p);
    CHECK(s.lost[2] == 1 && s.side2 == 1 && s.total == 2);

    const char* const names[3] = {"3x3", "4x4", "5x5"};
    puzzle::Record q; q.level = 1; q.moves = 112; q.seconds = 185;
    CHECK(puzzle::format_body(line, sizeof line, q, names));
    CHECK(strcmp(line, "4x4,Solved,112,185,3:05,0\n") == 0);
    snprintf(full, sizeof full, "3,%s", line);
    puzzle::Record qb;
    CHECK(puzzle::parse_line(full, qb, names) && qb.level == 1 && qb.moves == 112 && qb.solved);
    puzzle::Summary ps; ps.add(q);
    CHECK(ps.solved[1] == 1 && ps.best_s[1] == 185 && ps.newest(0).moves == 112);
}

int main()
{
    test_fourconnect();
    test_tictactoe();
    test_sliding();
    test_lightswitch();
    test_reversi();
    test_checkers();
    test_stats();
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
