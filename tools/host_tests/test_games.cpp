// Host-side checks for the stage-2 games and the shared stats formats
// (built and run by CI): FourConnect, Tic-Tac-Toe, Sliding Tiles,
// Light Switch, two_player.*, puzzle_stats.*.
#include "../../src/games/common/puzzle_stats.h"
#include "../../src/games/common/two_player.h"
#include "../../src/games/checkers/checkers_core.h"
#include "../../src/games/chess/chess_core.h"
#include "../../src/games/cyddle/cyddle_core.h"
#include "../../src/games/fourconnect/fourconnect_core.h"
#include "../../src/games/lightswitch/lightswitch_core.h"
#include "../../src/games/minesweeper/minesweeper_core.h"
#include "../../src/games/reversi/reversi_core.h"
#include "../../src/games/sliding/sliding_core.h"
#include "../../src/games/tictactoe/tictactoe_core.h"
#include "../../src/games/twenty48/twenty48_core.h"
#include "../../src/games/yahtcyd/yahtcyd_core.h"
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

// ---- Chess: move generation checked by perft against published counts ----
static void load_fen(chess::Position& p, const char* fen)
{
    using namespace chess;
    p = Position{};
    int r = 7, f = 0;
    const char* c = fen;
    for (; *c && *c != ' '; ++c) {
        if (*c == '/') { --r; f = 0; continue; }
        if (*c >= '1' && *c <= '8') { f += *c - '0'; continue; }
        const char* pcs = "pnbrqk";
        const bool black = *c >= 'a';
        const char lc = black ? *c : static_cast<char>(*c + 32);
        int pc = 0;
        for (int k = 0; k < 6; ++k) if (pcs[k] == lc) pc = k + 1;
        p.sq[r * 8 + f] = static_cast<uint8_t>(pc | ((black ? 1 : 0) << 3));
        if (pc == King) p.king[black ? 1 : 0] = static_cast<uint8_t>(r * 8 + f);
        ++f;
    }
    ++c;
    p.side = *c == 'b' ? 1 : 0;
    c += 2;
    p.castle = 0;
    for (; *c && *c != ' '; ++c) {
        if (*c == 'K') p.castle |= 1;
        if (*c == 'Q') p.castle |= 2;
        if (*c == 'k') p.castle |= 4;
        if (*c == 'q') p.castle |= 8;
    }
    ++c;
    p.ep = (*c == '-') ? -1 : static_cast<int8_t>((c[1] - '1') * 8 + (c[0] - 'a'));
    p.hash = p.compute_hash();
}

static uint64_t perft(const chess::Position& p, int depth)
{
    chess::MoveList l;
    chess::generate(p, l);
    if (depth == 1) return l.n;
    uint64_t n = 0;
    for (int k = 0; k < l.n; ++k) {
        chess::Position q = p;
        q.make(l.m[k]);
        n += perft(q, depth - 1);
    }
    return n;
}

static uint32_t now_ms()
{
    using namespace std::chrono;
    return static_cast<uint32_t>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

static void test_chess()
{
    using namespace chess;
    struct { const char* fen; int depth; uint64_t nodes; } cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 4, 197281},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 3, 97862},   // "Kiwipete"
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 4, 43238},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 3, 9467},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 3, 62379},
    };
    for (auto& c : cases) {
        Position p;
        load_fen(p, c.fen);
        const uint64_t n = perft(p, c.depth);
        if (n != c.nodes) printf("perft %s depth %d: %llu, want %llu\n", c.fen, c.depth,
                                 (unsigned long long)n, (unsigned long long)c.nodes);
        CHECK(n == c.nodes);
    }
    // Fool's mate: checkmate detected, Black wins
    Game g;
    auto play_uci = [&](const char* mv) {
        MoveList l; g.legal(l);
        const int from = (mv[1] - '1') * 8 + (mv[0] - 'a'), to = (mv[3] - '1') * 8 + (mv[2] - 'a');
        for (int k = 0; k < l.n; ++k) if (l.m[k].from == from && l.m[k].to == to) return g.play(k);
        return false;
    };
    CHECK(play_uci("f2f3") && play_uci("e7e5") && play_uci("g2g4") && play_uci("d8h4"));
    CHECK(g.end() == End::Checkmate && g.result() == 1);
    // Save/load replays to the same position
    uint8_t* buf = new uint8_t[Game::kSaveBytes];
    CHECK(g.serialize(buf, Game::kSaveBytes));
    Game* back = new Game();
    CHECK(back->deserialize(buf, Game::kSaveBytes) && back->pos.hash == g.pos.hash && back->result() == 1);
    delete back;
    delete[] buf;
    // Repetition: knights out and back twice
    Game r;
    auto rp = [&](const char* mv) {
        MoveList l; r.legal(l);
        const int from = (mv[1] - '1') * 8 + (mv[0] - 'a'), to = (mv[3] - '1') * 8 + (mv[2] - 'a');
        for (int k = 0; k < l.n; ++k) if (l.m[k].from == from && l.m[k].to == to) return r.play(k);
        return false;
    };
    for (int k = 0; k < 2; ++k) { rp("g1f3"); rp("g8f6"); rp("f3g1"); rp("f6g8"); }
    CHECK(r.end() == End::Repetition);
    // Computer: finds mate in one; Hard beats Easy
    Position m1;
    load_fen(m1, "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");      // Rd8#
    Game mg;
    mg.pos = m1;
    mg.hashes[0] = m1.hash;
    MoveList l;
    mg.legal(l);
    const int pick = best_move(mg, 1, 5, now_ms);
    CHECK(pick >= 0 && l.m[pick].from == 3 && l.m[pick].to == 59);
    int hard = 0;
    double worst = 0;
    for (int n = 0; n < 2; ++n) {
        Game* q = new Game();
        const int hs = n & 1;
        while (q->result() == -1 && q->plies < 160) {
            const int lvl = q->turn() == hs ? 2 : 0;
            const uint32_t t0 = now_ms();
            q->play(best_move(*q, lvl, 900 + n * 11 + q->plies, now_ms));
            if (lvl == 2 && now_ms() - t0 > worst) worst = now_ms() - t0;
        }
        hard += q->result() == hs;
        if (q->result() == -1) {   // unfinished after 80 moves: count material
            int mat[2] = {0, 0};
            const int v[7] = {0, 1, 3, 3, 5, 9, 0};
            for (int s = 0; s < 64; ++s) if (q->pos.sq[s]) mat[side_of(q->pos.sq[s])] += v[piece_of(q->pos.sq[s])];
            hard += mat[hs] > mat[hs ^ 1] + 3;
        }
        delete q;
    }
    printf("chess hard vs easy: %d/2, worst hard move %.0f ms\n", hard, worst);
    CHECK(hard == 2);
}

static void test_cyddle()
{
    using namespace cyddle;
    Mark m[kLen];
    score("speed", "abide", m);                // one e present (the answer has one)
    CHECK(m[0] == Absent && m[2] == Present && m[3] == Absent && m[4] == Present);
    score("eerie", "there", m);                // green e last; one spare e, so only the first e is gold
    CHECK(m[0] == Present && m[1] == Absent && m[2] == Present && m[3] == Absent && m[4] == Correct);
    CHECK(is_word("crane") && is_word("zebra") && is_word("aahed") && !is_word("xqzzt") && !is_word("whore"));
    CHECK(answer_count() > 1500);
    // Every answer is accepted as a guess
    for (int k = 0; k < answer_count(); ++k) { char w[kLen]; answer_word(k, w); CHECK(is_word(w)); }
    Game g;
    Rng rng(9);
    g.start(1, rng);
    char a[kLen];
    answer_word(g.answer, a);
    g.type('x');
    g.type('q');
    CHECK(g.submit() == Submit::TooShort);
    g.back(); g.back();
    const char* wrong = memcmp(a, "crane", 5) ? "crane" : "slate";
    for (int k = 0; k < kLen; ++k) g.type(wrong[k]);
    CHECK(g.submit() == Submit::Ok && g.rows == 1 && !g.over());
    for (int k = 0; k < kLen; ++k) g.type(a[k]);
    CHECK(g.submit() == Submit::Ok && g.solved() && g.over());
    // Hard mode: with the answer "crane", "slate" shows a green e; then
    // "think" (no e at the end) is refused and "shade" is allowed
    Game h;
    for (int k = 0; k < answer_count(); ++k) { char w[kLen]; answer_word(k, w); if (!memcmp(w, "crane", 5)) h.answer = k; }
    h.level = 2;
    auto put = [&](const char* w) { for (int k = 0; k < kLen; ++k) h.type(w[k]); return h.submit(); };
    CHECK(put("slate") == Submit::Ok);
    CHECK(put("think") == Submit::MustUseHints);
    h.typed = 0;
    CHECK(put("shade") == Submit::Ok);
    // Words never repeat until all were played
    Game r;
    Rng rr(3);
    bool repeat = false;
    uint8_t seen[(2048 + 7) / 8] = {};
    for (int k = 0; k < 300; ++k) {
        r.start(1, rr);
        if ((seen[r.answer / 8] >> (r.answer % 8)) & 1) repeat = true;
        seen[r.answer / 8] |= 1u << (r.answer % 8);
    }
    CHECK(!repeat);
    uint8_t buf[Game::kSaveBytes];
    CHECK(g.serialize(buf, sizeof buf));
    Game back;
    CHECK(back.deserialize(buf, sizeof buf) && back.answer == g.answer && back.rows == 2 && back.solved());
}

static void test_yahtcyd()
{
    using namespace yahtcyd;
    Game g;
    auto set = [&](int a, int b, int c, int d, int e) {
        g.dice[0] = a; g.dice[1] = b; g.dice[2] = c; g.dice[3] = d; g.dice[4] = e;
        g.rolls = 1;
    };
    CHECK(!g.can_score(Chance));                      // must roll first
    set(3, 3, 3, 5, 5);
    CHECK(g.potential(Threes) == 9 && g.potential(FullHouse) == 25 && g.potential(ThreeKind) == 19);
    CHECK(g.potential(FourKind) == 0 && g.potential(SmallStraight) == 0);
    set(2, 3, 4, 5, 2);
    CHECK(g.potential(SmallStraight) == 30 && g.potential(LargeStraight) == 0);
    set(6, 2, 3, 4, 5);
    CHECK(g.potential(LargeStraight) == 40 && g.potential(SmallStraight) == 30);
    set(4, 4, 4, 4, 4);
    CHECK(g.potential(YahtCyd) == 50 && g.potential(FullHouse) == 0);
    CHECK(g.score_box(YahtCyd) && g.score[YahtCyd] == 50 && g.rolls == 0);
    // A second Yaht-CYD: +100, must go in Fours while Fours is empty
    set(4, 4, 4, 4, 4);
    CHECK(!g.can_score(Chance) && g.can_score(Fours));
    CHECK(g.score_box(Fours) && g.extra == 1 && g.score[Fours] == 20);
    // A third, of sixes, with Sixes filled: joker scores Full House in full
    g.score[Sixes] = 18;
    set(6, 6, 6, 6, 6);
    CHECK(g.can_score(FullHouse) && g.potential(FullHouse) == 25);
    // Holding: only between rolls; held dice keep their value
    Game h;
    Rng rng(4);
    h.roll(rng);
    const uint8_t keep = h.dice[2];
    h.toggle_hold(2);
    h.roll(rng); h.roll(rng);
    CHECK(h.dice[2] == keep && !h.can_roll());
    // A whole game, always scoring the best box, ends with a total
    Game w;
    Rng r2(77);
    while (!w.over()) {
        w.roll(r2);
        int best = -1, bv = -1;
        for (int b = 0; b < kBoxes; ++b) if (w.can_score(b) && w.potential(b) > bv) { bv = w.potential(b); best = b; }
        CHECK(w.score_box(best));
    }
    CHECK(w.total() > 0 && w.turn() == kBoxes);
    uint8_t buf[Game::kSaveBytes];
    CHECK(w.serialize(buf, sizeof buf));
    Game back;
    CHECK(back.deserialize(buf, sizeof buf) && back.total() == w.total());
    // History line
    Record rec; rec.score = 245; rec.upper = 68; rec.bonus = 35; rec.yahts = 1; rec.seconds = 742;
    char line[96], full[100];
    CHECK(format_body(line, sizeof line, rec));
    CHECK(strcmp(line, "245,68,35,1,742,12:22\n") == 0);
    snprintf(full, sizeof full, "7,%s", line);
    Record rb;
    CHECK(parse_line(full, rb) && rb.score == 245 && rb.yahts == 1 && !parse_line(kCsvHeader, rb));
}

static void test_minesweeper()
{
    using namespace mines;
    // Every level: first tap opens an area, the board is logic-solvable, and
    // a solver-driven play wins it.
    int failed_boards = 0;
    double worst_ms = 0;
    for (int level = 0; level < kLevels; ++level) {
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            Board b;
            b.start(level);
            Rng rng(seed * 7919u);
            const int first = int(seed % uint32_t(b.cells()));
            const auto t0 = std::chrono::steady_clock::now();
            CHECK(b.open(first, rng));
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (ms > worst_ms) worst_ms = ms;
            int count = 0;
            for (int i = 0; i < b.cells(); ++i) count += b.mine[i];
            CHECK(count == b.mines);
            CHECK(!b.mine[first] && b.near[first] == 0);
            CHECK(b.status == Status::Playing);
            if (!solvable(b, first)) ++failed_boards;
            // Win by opening every safe cell
            for (int i = 0; i < b.cells(); ++i) if (!b.mine[i]) b.open(i, rng);
            CHECK(b.status == Status::Won);
            CHECK(b.mines_left() == 0);          // mines flagged on a win
        }
    }
    printf("minesweeper: %d of 180 boards needed a guess, worst generate %.1f ms\n", failed_boards, worst_ms);
    CHECK(failed_boards == 0);

    // Hitting a mine loses; flags block taps; chord opens the rest
    Board b;
    b.start(0);
    Rng rng(42);
    CHECK(b.open(0, rng));
    int mine_at = -1;
    for (int i = 0; i < b.cells(); ++i) if (b.mine[i] && b.cell[i] == Cell::Hidden) { mine_at = i; break; }
    CHECK(mine_at >= 0);
    b.toggle_flag(mine_at);
    CHECK(b.cell[mine_at] == Cell::Flag && b.mines_left() == b.mines - 1);
    CHECK(!b.open(mine_at, rng));            // flagged: nothing happens
    // Save / load round trip mid-game
    uint8_t buf[Board::kSaveBytes];
    CHECK(b.serialize(buf, sizeof buf) == Board::kSaveBytes);
    Board c;
    CHECK(c.deserialize(buf, sizeof buf));
    CHECK(c.level == 0 && c.cell[mine_at] == Cell::Flag && c.moves == b.moves);
    CHECK(memcmp(c.near, b.near, sizeof b.near) == 0);
    buf[0] = 'X';
    CHECK(!c.deserialize(buf, sizeof buf));
    b.toggle_flag(mine_at);
    CHECK(b.open(mine_at, rng));
    CHECK(b.status == Status::Lost && b.boom == mine_at);

    // Chord: flag all mines around an open number, tap it
    Board d;
    d.start(1);
    Rng r2(5);
    d.open(40, r2);
    bool chorded = false;
    int nb[8];
    for (int i = 0; i < d.cells() && !chorded; ++i) {
        if (d.cell[i] != Cell::Open || !d.near[i]) continue;
        const int n = d.neighbors(i, nb);
        int hidden_safe = 0;
        for (int k = 0; k < n; ++k) hidden_safe += !d.mine[nb[k]] && d.cell[nb[k]] == Cell::Hidden;
        if (!hidden_safe) continue;
        for (int k = 0; k < n; ++k) if (d.mine[nb[k]] && d.cell[nb[k]] == Cell::Hidden) d.toggle_flag(nb[k]);
        CHECK(d.open(i, r2));
        for (int k = 0; k < n; ++k) CHECK(d.mine[nb[k]] ? d.cell[nb[k]] == Cell::Flag : d.cell[nb[k]] == Cell::Open);
        chorded = true;
    }
    CHECK(chorded);
}

static void test_twenty48()
{
    using namespace twenty48;
    Rng rng(5);
    auto row = [](Game& g, int r, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
        g.cell[r * 4] = a; g.cell[r * 4 + 1] = b; g.cell[r * 4 + 2] = c; g.cell[r * 4 + 3] = d;
    };
    {   // 2 2 2 2 left -> 4 4 . . (pairs nearest the wall first), score 8
        Game g; row(g, 0, 1, 1, 1, 1);
        uint8_t m = 0;
        CHECK(g.slide(Dir::Left, rng, &m));
        CHECK(g.cell[0] == 2 && g.cell[1] == 2 && m == 2 && g.score == 8 && g.moves == 1);
        int tiles = 0; for (int i = 0; i < kCells; ++i) tiles += g.cell[i] != 0;
        CHECK(tiles == 3);                                 // two 4s + the new tile
    }
    {   // 4 4 8 . left -> 8 8 . . (no merging twice in one slide)
        Game g; row(g, 1, 2, 2, 3, 0);
        CHECK(g.slide(Dir::Left, rng));
        CHECK(g.cell[4] == 3 && g.cell[5] == 3 && g.score == 8);
    }
    {   // 2 2 4 . right -> . . 4 4 ; 2 . 2 2 right -> . . 2 4
        Game g; row(g, 0, 1, 1, 2, 0); row(g, 3, 1, 0, 1, 1);
        CHECK(g.slide(Dir::Right, rng));
        CHECK(g.cell[2] == 2 && g.cell[3] == 2);
        CHECK(g.cell[14] == 1 && g.cell[15] == 2);
    }
    {   // columns: up and down
        Game g; g.cell[0] = 1; g.cell[4] = 1; g.cell[12] = 2;
        CHECK(g.slide(Dir::Up, rng));
        CHECK(g.cell[0] == 2 && g.cell[4] == 2);
        Game h; h.cell[3] = 3; h.cell[7] = 3; h.cell[11] = 3;
        CHECK(h.slide(Dir::Down, rng));
        CHECK(h.cell[15] == 4 && h.cell[11] == 3);
    }
    {   // a slide that moves nothing changes nothing
        Game g; row(g, 0, 1, 2, 3, 4);
        CHECK(!g.can_slide(Dir::Left) && !g.slide(Dir::Left, rng) && g.moves == 0);
        CHECK(g.can_slide(Dir::Down));
    }
    {   // full board, no pairs: over
        Game g;
        const uint8_t pat[16] = {1,2,1,2, 2,1,2,1, 1,2,1,2, 2,1,2,1};
        for (int i = 0; i < kCells; ++i) g.cell[i] = pat[i];
        CHECK(g.over());
        g.cell[0] = 2;                                     // now 4 next to 4
        CHECK(!g.over());
    }
    {   // reaching 2048
        Game g; row(g, 0, 10, 10, 0, 0);
        uint8_t m = 0;
        CHECK(g.slide(Dir::Left, rng, &m) && m == kWinExp && g.won);
    }
    // Random games: invariants, save round trip
    uint32_t best = 0;
    for (int n = 0; n < 50; ++n) {
        Game g; Rng r(1000 + n);
        g.start(r);
        int tiles = 0; for (int i = 0; i < kCells; ++i) tiles += g.cell[i] != 0;
        CHECK(tiles == 2);
        while (!g.over()) {
            // simple corner strategy so games get somewhere
            const Dir order[4] = {Dir::Down, Dir::Left, Dir::Right, Dir::Up};
            bool moved = false;
            for (Dir d : order) if (g.slide(d, r)) { moved = true; break; }
            CHECK(moved);
            if (!moved) break;
        }
        if (g.score > best) best = g.score;
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf));
        CHECK(memcmp(h.cell, g.cell, kCells) == 0 && h.score == g.score && h.moves == g.moves && h.won == g.won);
    }
    CHECK(best > 1000);
    // History CSV
    Record rec; rec.score = 20512; rec.tile = 2048; rec.moves = 1043; rec.seconds = 1840;
    char body[96];
    CHECK(format_body(body, sizeof body, rec) > 0);
    CHECK(strcmp(body, "20512,2048,1043,1840,30:40\n") == 0);
    char line[120];
    snprintf(line, sizeof line, "4,%s", body);
    Record back;
    CHECK(parse_line(line, back) && back.score == 20512 && back.tile == 2048 && back.moves == 1043);
    Summary s; s.add(back); rec.score = 100; rec.tile = 16; s.add(rec);
    CHECK(s.games == 2 && s.best == 20512 && s.best_tile == 2048 && s.wins == 1 && s.newest(0).score == 100);
    printf("2048: best random-play score %lu\n", (unsigned long)best);
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
    puzzle::Record lr; lr.level = 2; lr.solved = false; lr.lost = true; lr.moves = 7; lr.seconds = 40;
    CHECK(puzzle::format_body(line, sizeof line, lr, names));
    CHECK(strcmp(line, "5x5,Lost,7,40,0:40,0\n") == 0);
    snprintf(full, sizeof full, "4,%s", line);
    CHECK(puzzle::parse_line(full, qb, names) && !qb.solved && qb.lost);
    ps.add(qb);
    CHECK(ps.lost[2] == 1 && ps.gave_up[2] == 0);
}

int main()
{
    test_fourconnect();
    test_tictactoe();
    test_sliding();
    test_lightswitch();
    test_reversi();
    test_checkers();
    test_chess();
    test_cyddle();
    test_yahtcyd();
    test_minesweeper();
    test_twenty48();
    test_stats();
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
