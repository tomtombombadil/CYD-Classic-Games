// Host-side checks for the stage-2 games and the shared stats formats
// (built and run by CI): FourConnect, Tic-Tac-Toe, Sliding Tiles,
// Light Switch, two_player.*, puzzle_stats.*.
#include "../../src/games/common/puzzle_stats.h"
#include "../../src/games/common/two_player.h"
#include "../../src/games/blackjack/blackjack_core.h"
#include "../../src/games/checkers/checkers_core.h"
#include "../../src/games/chess/chess_core.h"
#include "../../src/games/cyddle/cyddle_core.h"
#include "../../src/games/fourconnect/fourconnect_core.h"
#include "../../src/games/freecell/freecell_core.h"
#include "../../src/games/golf/golf_core.h"
#include "../../src/games/lightswitch/lightswitch_core.h"
#include "../../src/games/mastercyd/mastercyd_core.h"
#include "../../src/games/memory/memory_core.h"
#include "../../src/games/minesweeper/minesweeper_core.h"
#include "../../src/games/nonogram/nonogram_core.h"
#include "../../src/games/pegs/pegs_core.h"
#include "../../src/games/pyramid/pyramid_core.h"
#include "../../src/games/reversi/reversi_core.h"
#include "../../src/games/sliding/sliding_core.h"
#include "../../src/games/spider/spider_core.h"
#include "../../src/games/solitaire/solitaire_core.h"
#include "../../src/games/solitaire/solitaire_solve.h"
#include "../../src/games/tictactoe/tictactoe_core.h"
#include "../../src/games/twenty48/twenty48_core.h"
#include "../../src/games/yahtcyd/yahtcyd_core.h"
#include "../../src/ui/log_pack.h"
#include "../../src/games/rpgdice/rpgdice_core.h"
#include "../../src/games/vpoker/vpoker_core.h"
#include "../../src/games/holdem/holdem_core.h"
#include "../../src/games/farkle/farkle_core.h"
#include "../../src/games/mancala/mancala_core.h"
#include "../../src/games/morris/morris_core.h"
#include <chrono>
#include <string>
#include <vector>
#include <unordered_set>
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

static void test_mastercyd()
{
    using namespace mastercyd;
    {   // scoring: exact first, then color matches without double counting
        const uint8_t code[4] = {0, 1, 2, 3};
        const uint8_t g1[4] = {0, 1, 2, 3}, g2[4] = {3, 2, 1, 0}, g3[4] = {0, 0, 0, 0}, g4[4] = {4, 4, 5, 5};
        Feedback f = score(code, g1, 4); CHECK(f.exact == 4 && f.near == 0);
        f = score(code, g2, 4); CHECK(f.exact == 0 && f.near == 4);
        f = score(code, g3, 4); CHECK(f.exact == 1 && f.near == 0);
        f = score(code, g4, 4); CHECK(f.exact == 0 && f.near == 0);
        const uint8_t c2[4] = {1, 1, 2, 2}, g5[4] = {2, 1, 1, 3};
        f = score(c2, g5, 4); CHECK(f.exact == 1 && f.near == 2);
        const uint8_t c3[5] = {5, 5, 5, 0, 1}, g6[5] = {0, 5, 1, 5, 5};
        f = score(c3, g6, 5); CHECK(f.exact == 1 && f.near == 4);
    }
    {   // Easy: no repeats in the code or the guess row
        for (int s = 1; s < 200; ++s) {
            Rng rng(s); Game g; g.start(0, rng);
            bool seen[kColors] = {};
            for (int i = 0; i < 4; ++i) { CHECK(!seen[g.secret[i]]); seen[g.secret[i]] = true; }
        }
        Rng rng(3); Game g; g.start(0, rng);
        CHECK(g.place(2) && !g.place(2) && g.place(3));
        g.clear(0);
        CHECK(g.cur[0] == kEmpty && g.place(2) && g.cur[0] == 2);
        CHECK(!g.full() && !g.submit());
    }
    {   // Play to the end: a solver using consistent guesses always wins in 10
        for (int lv = 0; lv < kLevels; ++lv)
            for (int s = 1; s <= 30; ++s) {
                Rng rng(s * 7 + lv); Game g; g.start(lv, rng);
                const int n = g.pegs();
                int total = 1; for (int i = 0; i < n; ++i) total *= kColors;
                int cand = 0;
                while (!g.over()) {
                    // next code (in counting order) consistent with every answer so far
                    uint8_t c[kMaxPegs] = {};
                    bool found = false;
                    for (; cand < total && !found; ++cand) {
                        int v = cand;
                        for (int i = 0; i < n; ++i) { c[i] = uint8_t(v % kColors); v /= kColors; }
                        if (!g.repeats()) {
                            bool dup = false;
                            for (int i = 0; i < n; ++i) for (int j = i + 1; j < n; ++j) dup |= c[i] == c[j];
                            if (dup) continue;
                        }
                        bool ok = true;
                        for (int r = 0; r < g.rows && ok; ++r) {
                            const Feedback f = score(c, g.guess[r], n);
                            ok = f.exact == g.fb[r].exact && f.near == g.fb[r].near;
                        }
                        found = ok;
                    }
                    CHECK(found);
                    if (!found) break;
                    --cand;                                  // retry it if it's not the answer
                    for (int i = 0; i < n; ++i) CHECK(g.place(c[i]));
                    CHECK(g.submit());
                    ++cand;
                }
                CHECK(g.solved());
                uint8_t buf[Game::kSaveBytes];
                CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
                Game h;
                CHECK(h.deserialize(buf, sizeof buf) && h.rows == g.rows && h.solved() && h.level == g.level);
            }
    }
}

static void test_pegs()
{
    using namespace pegs;
    Game g;
    g.start(English);
    CHECK(g.peg_count() == 32 && !g.peg(24));
    // Opening jumps into the centre: from 10 (2,3) over 17, from 38 (5,3), 22 (3,1), 26 (3,5)
    CHECK(g.find(10, 24) && g.find(38, 24) && g.find(22, 24) && g.find(26, 24));
    CHECK(!g.find(24, 10) && !g.find(9, 24) && g.legal(nullptr) == 4);
    CHECK(g.play(10, 24) && g.peg_count() == 31 && !g.peg(17) && !g.peg(10) && g.peg(24));
    CHECK(g.undo() && g.peg_count() == 32 && g.peg(17) && !g.peg(24) && !g.undo());
    Game t; t.start(Triangle);
    CHECK(t.peg_count() == 14 && t.legal(nullptr) == 2);      // into the top from row 2
    Game e; e.start(European);
    CHECK(e.peg_count() == 36);
    // Every level can be finished with one peg: replay a known solution
    // (found offline by a symmetry-reduced search)
    static const uint8_t sol_tri[][2] = {{14,0},{16,14},{0,16},{21,7},{24,8},{29,15},{30,16},{7,23},{8,24},{32,16},{16,30},{31,29},{28,30}};
    static const uint8_t sol_eng[][2] = {{10,24},{15,17},{2,16},{4,2},{17,15},{14,16},{18,4},{20,18},{23,9},{2,16},{21,23},{23,9},{25,11},{4,18},{27,25},{25,11},{37,23},{28,30},{30,16},{9,23},{23,25},{32,18},{11,25},{34,32},{31,33},{46,32},{25,39},{44,46},{46,32},{33,31},{31,45}};
    static const uint8_t sol_eur[][2] = {{8,10},{11,9},{22,8},{8,10},{36,22},{17,15},{3,17},{14,16},{28,14},{17,15},{14,16},{19,17},{33,19},{20,18},{23,9},{2,16},{37,23},{32,30},{45,31},{46,32},{17,15},{15,29},{31,17},{23,37},{44,30},{29,31},{25,11},{4,18},{17,19},{12,26},{27,25},{31,33},{34,32},{25,39},{40,38}};
    const uint8_t (*sols[3])[2] = {sol_tri, sol_eng, sol_eur};
    const int lens[3] = {int(sizeof sol_tri / 2), int(sizeof sol_eng / 2), int(sizeof sol_eur / 2)};
    for (int lv = 0; lv < kLevels; ++lv) {
        Game s; s.start(lv);
        bool ok = true;
        for (int i = 0; i < lens[lv] && ok; ++i) ok = s.play(sols[lv][i][0], sols[lv][i][1]);
        CHECK(ok && s.solved() && s.stuck());
        uint8_t buf[Game::kSaveBytes];
        CHECK(s.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.pegs == s.pegs && h.moves == s.moves);
        while (h.undo()) {}
        Game fresh; fresh.start(lv);
        CHECK(h.pegs == fresh.pegs);
    }
}

static void test_memory()
{
    using namespace memory;
    for (int lv = 0; lv < kLevels; ++lv) {
        Rng rng(40 + lv); Game g; g.start(lv, rng);
        int count[kPictures] = {};
        for (int i = 0; i < g.tiles(); ++i) ++count[g.pic[i]];
        for (int p = 0; p < kPictures; ++p) CHECK(count[p] == 0 || count[p] == 2);
        // Perfect memory: turn tiles left to right, pairing as soon as known
        int seen_at[kPictures]; for (int& s : seen_at) s = -1;
        for (int i = 0; i < g.tiles() && !g.solved(); ++i) {
            if (g.matched[i]) continue;
            const int other = seen_at[g.pic[i]];
            if (other >= 0) {
                CHECK(g.tap(i) == Tap::First);
                CHECK(g.tap(other) == Tap::Match);
                continue;
            }
            CHECK(g.tap(i) == Tap::First);
            seen_at[g.pic[i]] = i;
            // turn the next unknown tile as the second of the pair
            int j = i + 1;
            while (j < g.tiles() && g.matched[j]) ++j;
            if (j >= g.tiles()) break;
            const Tap r = g.tap(j);
            if (r == Tap::Match) { ++i; continue; }
            CHECK(r == Tap::Miss && g.face_up(i) && g.face_up(j));
            if (seen_at[g.pic[j]] < 0) seen_at[g.pic[j]] = j;
            else {                                       // j's partner known: next tap takes it
                const int k = seen_at[g.pic[j]];
                CHECK(g.tap(j) == Tap::Ignored);         // tapping a showing tile just hides the pair
                CHECK(!g.face_up(i) && !g.face_up(j));
                CHECK(g.tap(k) == Tap::First && g.tap(j) == Tap::Match);
            }
            i = j;
        }
        // Finish anything left by pairing known tiles
        for (int a = 0; a < g.tiles() && !g.solved(); ++a)
            for (int b = a + 1; b < g.tiles(); ++b)
                if (!g.matched[a] && !g.matched[b] && g.pic[a] == g.pic[b]) {
                    g.up_a = g.up_b = -1;                    // put any showing tiles down
                    CHECK(g.tap(a) == Tap::First && g.tap(b) == Tap::Match);
                }
        CHECK(g.solved() && g.turns >= g.pairs());
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.solved() && h.turns == g.turns);
    }
    // A miss stays up until the next tap, which also turns the new tile
    Rng rng(5); Game g; g.start(0, rng);
    int a = 0, b = 1;
    while (g.pic[b] == g.pic[a]) ++b;
    int c = 0;
    while (c == a || c == b) ++c;
    CHECK(g.tap(a) == Tap::First && g.tap(b) == Tap::Miss && g.turns == 1);
    CHECK(g.tap(c) == Tap::First && !g.face_up(a) && !g.face_up(b) && g.face_up(c));
}

static void test_nonogram()
{
    using namespace nonogram;
    {   // clues
        const Clue a = clue_of(0b0110111, 7);
        CHECK(a.n == 2 && a.run[0] == 3 && a.run[1] == 2);
        CHECK(clue_of(0, 5).n == 0);
        const Clue b = clue_of(0b10101, 5);
        CHECK(b.n == 3 && b.run[0] == 1 && b.run[2] == 1);
    }
    {   // a known picture: play it, check row/col completion and solving
        const uint16_t pic[5] = {0b11111, 0b10001, 0b10101, 0b10001, 0b11111};   // a framed dot
        Game g; g.set_picture(5, pic);
        uint8_t out[25];
        CHECK(line_solve(5, g.rows, g.cols, out));
        for (int r = 0; r < 5; ++r) for (int c = 0; c < 5; ++c)
            CHECK((out[r * 5 + c] == Filled) == bool(pic[r] >> c & 1));
        CHECK(!g.solved() && !g.row_done(0));
        for (int r = 0; r < 5; ++r) for (int c = 0; c < 5; ++c)
            if (pic[r] >> c & 1) g.tap(r, c, Filled);
        CHECK(g.solved());
        g.tap(0, 0, Marked);                         // no changes once solved
        CHECK(g.cell[0] == Filled && g.solved());
    }
    {   // an "A" has one answer, but line logic alone can't find it: the
        // generator would skip it
        const uint16_t pic[5] = {0b01110, 0b10001, 0b11111, 0b10001, 0b10001};
        Game g; g.set_picture(5, pic);
        uint8_t out[25];
        CHECK(!line_solve(5, g.rows, g.cols, out));
    }
    {   // a picture with two answers is not line-solvable
        const uint16_t two[2] = {0b01, 0b10};
        Game g; g.set_picture(2, two);
        uint8_t out[4];
        CHECK(!line_solve(2, g.rows, g.cols, out));
    }
    // Generated puzzles: line-solvable to exactly the picture, mirrored, clues fit
    long worst_tries_us = 0;
    for (int lv = 0; lv < kLevels; ++lv)
        for (int s = 1; s <= 40; ++s) {
            Rng rng(s * 31 + lv);
            Game g;
            const auto t0 = std::chrono::steady_clock::now();
            g.start(lv, rng);
            const long us = long(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
            if (us > worst_tries_us) worst_tries_us = us;
            CHECK(g.n == level_size(lv));
            uint8_t out[kMaxN * kMaxN];
            CHECK(line_solve(g.n, g.rows, g.cols, out));
            for (int r = 0; r < g.n; ++r) {
                CHECK(g.rows[r].n <= kMaxClues && g.cols[r].n <= kMaxClues);
                for (int c = 0; c < g.n; ++c) {
                    CHECK((out[r * g.n + c] == Filled) == bool(g.picture[r] >> c & 1));
                    CHECK(bool(g.picture[r] >> c & 1) == bool(g.picture[r] >> (g.n - 1 - c) & 1));
                }
            }
            uint8_t buf[Game::kSaveBytes];
            g.tap(0, 0, Filled); g.tap(1, 1, Marked);
            CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
            Game h;
            CHECK(h.deserialize(buf, sizeof buf) && h.cell[0] == Filled && h.cell[h.n + 1] == Marked && h.picture[3] == g.picture[3]);
        }
    printf("nonogram: worst generate %.1f ms\n", worst_tries_us / 1000.0);
}

static int sol_count(const solitaire::Game& g)
{
    int n = 0;
    for (const auto& s : g.pile) n += s.n;
    return n;
}

static void test_solitaire()
{
    using namespace solitaire;
    Game* gp = new Game();
    Game& g = *gp;
    g.deal(12345, 3, Scoring::Standard);
    CHECK(g.pile[Stock].n == 24 && sol_count(g) == 52);
    for (int c = 0; c < 7; ++c) {
        CHECK(g.pile[Tab0 + c].n == c + 1 && g.first_up(Tab0 + c) == c);
    }
    // Draw 3 turns three cards, face up; undo puts them back
    CHECK(g.draw_stock() && g.pile[Waste].n == 3 && g.pile[Stock].n == 21 && Game::face_up(g.pile[Waste].top()));
    CHECK(g.undo() && g.pile[Waste].n == 0 && g.pile[Stock].n == 24 && !Game::face_up(g.pile[Stock].top()));
    // Recycling the waste: Standard draw 3 costs 20 (never below 0)
    for (int i = 0; i < 8; ++i) CHECK(g.draw_stock());
    CHECK(g.pile[Stock].n == 0 && g.pile[Waste].n == 24);
    g.score = 50;
    CHECK(g.draw_stock() && g.pile[Stock].n == 24 && g.score == 30 && g.passes == 1);
    CHECK(g.undo() && g.score == 50 && g.passes == 0 && g.pile[Waste].n == 24);

    // Rules on a hand-built position
    Game h; h.deal(1, 1, Scoring::Standard);
    for (auto& s : h.pile) s = Stack{};
    auto C = [](int rk, int st) { return uint8_t(st * 13 + rk - 1); };   // suits: 0 S, 1 H, 2 D, 3 C
    h.pile[Tab0].c[0] = uint8_t(C(9, 0) | kDown); h.pile[Tab0].c[1] = C(8, 1); h.pile[Tab0].n = 2;   // 8H on a hidden 9S
    h.pile[Tab0 + 1].c[0] = C(9, 3); h.pile[Tab0 + 1].n = 1;                                      // 9C
    h.pile[Waste].c[0] = C(1, 2); h.pile[Waste].n = 1;                                            // AD
    h.pile[Tab0 + 2].c[0] = C(13, 1); h.pile[Tab0 + 2].c[1] = C(12, 0); h.pile[Tab0 + 2].n = 2;    // KH QS
    CHECK(h.can_move(Tab0, 1, Tab0 + 1));            // red 8 on black 9
    CHECK(!h.can_move(Tab0 + 1, 0, Tab0));           // 9 on 8: no
    CHECK(h.can_move(Waste, 0, Found0 + 3) && !h.can_move(Waste, 0, Found0) && !h.can_move(Tab0, 1, Found0 + 1));
    CHECK(found_for(C(1, 0)) == Found0 && found_for(C(1, 1)) == Found0 + 1 && found_for(C(1, 3)) == Found0 + 2 && found_for(C(1, 2)) == Found0 + 3);
    CHECK(!h.can_move(Tab0 + 2, 1, Tab0 + 3));       // queen into an empty column: no
    CHECK(h.can_move(Tab0 + 2, 0, Tab0 + 3));        // the K with its run: yes
    CHECK(h.best_target(Waste, 0) == Found0 + 3);       // diamonds: the fourth foundation
    CHECK(h.move(Tab0, 1, Tab0 + 1) && h.score == 5 && Game::face_up(h.pile[Tab0].top()));   // turned up +5
    CHECK(h.move(Waste, 0, Found0 + 3) && h.score == 15);
    CHECK(h.undo() && h.undo() && h.score == 0 && !Game::face_up(h.pile[Tab0].c[0]) && h.pile[Tab0].n == 2);

    // Vegas: -52 a deal, +5 a foundation card; Draw 1 never recycles
    Game v; v.deal(7, 1, Scoring::Vegas);
    CHECK(v.score == -52);
    while (v.pile[Stock].n) CHECK(v.draw_stock());
    CHECK(!v.can_draw());

    // Play many deals with the hint player: cards never get lost, undo goes
    // all the way back, a few deals get won
    int wins = 0;
    for (uint32_t s = 1; s <= 60; ++s) {
        g.deal(s * 2654435761u, s % 2 ? 1 : 3, Scoring::Standard);
        int steps = 0, recycles = 0;
        while (!g.won() && steps < 600) {
            if (g.can_finish()) { while (g.finish_step()) {} break; }
            int f, i, to;
            if (!g.hint(&f, &i, &to)) break;
            if (f == Stock) {
                if (!g.pile[Stock].n && ++recycles > 3) break;
                CHECK(g.draw_stock());
            } else {
                CHECK(g.move(f, i, to));
            }
            ++steps;
            CHECK(sol_count(g) == 52);
        }
        wins += g.won();
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game* r = new Game();
        CHECK(r->deserialize(buf, sizeof buf) && r->score == g.score && r->pile[Found0].n == g.pile[Found0].n);
        delete r;
        if (g.undo_n < kUndo) {                      // whole history kept: undo back to the deal
            while (g.undo()) {}
            Game* fresh = new Game();
            fresh->deal(g.seed, g.draw, g.scoring);
            bool same = true;
            for (int p = 0; p < kPiles; ++p)
                same &= fresh->pile[p].n == g.pile[p].n && memcmp(fresh->pile[p].c, g.pile[p].c, g.pile[p].n) == 0;
            CHECK(same && g.score == 0);
            delete fresh;
        }
    }
    printf("solitaire: hint player won %d of 60 deals\n", wins);
    // Winnable deals only: the checker's wins are real, and a deal it calls
    // winnable is found within a few tries for every option
    {
        Game* w = new Game();
        uint32_t nodes = 0;
        int proven = 0;
        for (uint32_t s = 1; s <= 20; ++s)
            proven += check_deal(*w, s * 40503u, 1, Scoring::Standard, 20000, nullptr, &nodes) == Verdict::Win;
        CHECK(proven >= 5);
        for (int draw : {1, 3})
            for (int sc = 0; sc < 3; ++sc) {
                int tries = 0;
                const auto t0 = std::chrono::steady_clock::now();
                const uint32_t seed = find_winnable(12345u + draw * 7 + sc, draw, Scoring(sc), 20000, nullptr, &tries);
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
                CHECK(tries > 0 && check_deal(*w, seed, draw, Scoring(sc), 20000) == Verdict::Win);
                printf("solitaire: winnable deal, draw %d scoring %d: %d tries, %.0f ms\n", draw, sc, tries, ms);
            }
        delete w;
    }
    CHECK(wins > 0);
    delete gp;
}

static void test_golf()
{
    using namespace golf;
    int wins = 0, best = 99;
    for (uint32_t s = 1; s <= 200; ++s) {
        Game g; g.deal(s * 977);
        CHECK(g.left() == 35 && g.stock_n == 16 && g.waste_n == 1);
        // greedy: play while possible (longest look: prefer a card whose
        // column has another follow-up), else turn the stock
        while (!g.won() && !g.stuck()) {
            const int h = g.hint();
            CHECK(h >= 0);
            if (h == 7) CHECK(g.draw()); else CHECK(g.play(h));
            int total = g.stock_n + g.waste_n + g.left();
            CHECK(total == 52);
        }
        wins += g.won();
        if (g.left() < best) best = g.left();
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.left() == g.left() && h.log_n == g.log_n);
        while (h.undo()) {}
        Game f; f.deal(s * 977);
        CHECK(memcmp(f.col, h.col, sizeof f.col) == 0 && h.stock_n == 16 && h.waste_n == 1);
    }
    // a King takes nothing; ranks don't wrap
    Game k; k.deal(1);
    k.waste[0] = 12;                                  // King of spades
    k.col[0][4] = 0;                                  // Ace on top of column 0
    k.col[1][4] = 11;                                 // Queen
    CHECK(!k.can_play(0) && !k.can_play(1));
    k.waste[0] = 0;                                   // an Ace on the waste
    k.col[0][4] = 12;                                 // King
    CHECK(!k.can_play(0));
    k.col[2][4] = 1;                                  // a 2
    CHECK(k.can_play(2));
    printf("golf: greedy won %d of 200, best %d left\n", wins, best);
}

static void test_pyramid()
{
    using namespace pyramid;
    CHECK(row_of(0) == 0 && row_of(1) == 1 && row_of(2) == 1 && row_of(27) == 6 && row_of(21) == 6);
    int wins = 0;
    for (uint32_t s = 1; s <= 200; ++s) {
        Game g; g.deal(s * 7919);
        CHECK(g.free(21) && g.free(27) && !g.free(0) && !g.free(15));
        int guard = 0;
        while (!g.won() && !g.stuck() && guard++ < 500) {
            int a, b;
            CHECK(g.hint(&a, &b));
            if (a == -2) CHECK(g.draw()); else CHECK(g.pair(a, b));
        }
        wins += g.won();
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.gone == g.gone && h.waste_n == g.waste_n);
        if (g.log_n < kLog) {
            while (g.undo()) {}
            Game f; f.deal(s * 7919);
            CHECK(g.gone == 0 && g.stock_n == 24 && g.waste_n == 0 && g.passes == 1 && memcmp(g.stock, f.stock, 24) == 0);
        }
    }
    printf("pyramid: hint player won %d of 200\n", wins);
}

static int spider_count(const spider::Game& g)
{
    int t = g.stock_n + 13 * g.done;
    for (int c = 0; c < spider::kCols; ++c) t += g.n[c];
    return t;
}

static void test_spider()
{
    using namespace spider;
    Game* gp = new Game();
    Game& g = *gp;
    g.deal(99, 0);
    CHECK(spider_count(g) == 104 && g.stock_n == 50 && g.n[0] == 6 && g.n[9] == 5 && g.first_up(0) == 5);
    // One suit: every card a spade
    for (int c = 0; c < kCols; ++c) for (int i = 0; i < g.n[c]; ++i) CHECK(suit(g.col[c][i]) == 0);
    // Four suits: 8 of each rank, 26 of each suit
    g.deal(99, 2);
    int per_suit[4] = {};
    for (int c = 0; c < kCols; ++c) for (int i = 0; i < g.n[c]; ++i) ++per_suit[suit(g.col[c][i])];
    for (int i = 0; i < g.stock_n; ++i) ++per_suit[suit(g.stock[i])];
    CHECK(per_suit[0] == 26 && per_suit[1] == 26 && per_suit[2] == 26 && per_suit[3] == 26);
    // A full run comes off by itself, and undo puts it back
    g.deal(5, 0);
    for (int c = 0; c < kCols; ++c) g.n[c] = 0;
    g.col[0][0] = uint8_t(0 | kDown); g.col[0][1] = 3; g.n[0] = 2;               // hidden A, then a 4
    for (int r = 13; r >= 2; --r) g.col[1][g.n[1]++] = uint8_t(r - 1);          // K..2 of spades
    g.col[2][0] = 0; g.n[2] = 1;                                                 // an Ace
    for (int c = 3; c < kCols; ++c) { g.col[c][0] = 5; g.n[c] = 1; }
    g.stock_n = 0;
    const int score0 = g.score;
    CHECK(g.can_move(2, 0, 1) && g.move(2, 0, 1));
    CHECK(g.done == 1 && g.n[1] == 0 && g.score == score0 - 1 + 100);
    CHECK(g.undo() && g.done == 0 && g.n[1] == 12 && g.n[2] == 1 && g.score == score0);
    // can't deal with an empty column; moving the 4 turns up the Ace
    g.n[2] = 0;
    g.stock_n = 10;
    CHECK(!g.can_deal());
    CHECK(g.move(0, 1, 2) && up(g.col[0][0]) && g.undo() && !up(g.col[0][0]));
    // Many deals with the hint player: no card lost, undo all the way back
    int won = 0;
    for (uint32_t s = 1; s <= 30; ++s) {
        g.deal(s * 2246822519u, 0);
        for (int step = 0; step < 400 && !g.won(); ++step) {
            int f, i, to;
            if (!g.hint(&f, &i, &to)) break;
            if (f < 0) CHECK(g.deal_row()); else CHECK(g.move(f, i, to));
            CHECK(spider_count(g) == 104);
        }
        won += g.won();
        uint8_t* buf = new uint8_t[Game::kSaveBytes];
        CHECK(g.serialize(buf, Game::kSaveBytes) == Game::kSaveBytes);
        Game* r = new Game();
        CHECK(r->deserialize(buf, Game::kSaveBytes) && r->done == g.done && r->score == g.score && r->stock_n == g.stock_n);
        delete r;
        delete[] buf;
        if (g.log_n < kLog) {
            while (g.undo()) {}
            Game* f = new Game();
            f->deal(g.seed, 0);
            bool same = f->stock_n == g.stock_n && g.score == 500 && g.done == 0;
            for (int c = 0; c < kCols; ++c) same &= f->n[c] == g.n[c] && memcmp(f->col[c], g.col[c], g.n[c]) == 0;
            CHECK(same);
            delete f;
        }
    }
    printf("spider: hint player won %d of 30 one-suit deals\n", won);
    delete gp;
}

static int fc_count(const freecell::Game& g)
{
    int t = g.found[0] + g.found[1] + g.found[2] + g.found[3];
    for (uint8_t c : g.cell) t += c != 0xFF;
    for (int c = 0; c < 8; ++c) t += g.n[c];
    return t;
}

static void test_freecell()
{
    using namespace freecell;
    Game* gp = new Game();
    Game& g = *gp;
    g.deal(11982);
    CHECK(fc_count(g) == 52 && g.n[0] == 7 && g.n[7] == 6 && g.free_cells() == 4);
    CHECK(g.max_run(false) == 5 && g.max_run(true) == 5);
    // Hand-built: run sizes with free cells and empty columns
    for (int c = 0; c < 8; ++c) g.n[c] = 0;
    memset(g.found, 0, 4);
    auto C = [](int r, int s) { return uint8_t(s * 13 + r - 1); };
    g.col[0][0] = C(9, 3); g.col[0][1] = C(8, 1); g.col[0][2] = C(7, 0); g.col[0][3] = C(6, 2); g.n[0] = 4;   // 9C 8H 7S 6D
    g.col[1][0] = C(10, 1); g.n[1] = 1;                                                                   // 10H
    for (int c = 2; c < 8; ++c) { g.col[c][0] = C(13, c % 4); g.n[c] = 1; }
    g.cell[0] = C(5, 0); g.cell[1] = C(5, 3); g.cell[2] = 0xFF; g.cell[3] = 0xFF;                      // 2 free cells
    CHECK(g.run_start(0) == 0 && g.max_run(false) == 3);
    CHECK(!g.can_move(Col0, 0, Col0 + 1));                // 4 cards, room for 3
    g.cell[1] = 0xFF;                                     // 3 free cells -> 4
    CHECK(g.can_move(Col0, 0, Col0 + 1) && g.move(Col0, 0, Col0 + 1));
    CHECK(g.n[0] == 0 && g.n[1] == 5);
    CHECK(g.undo() && g.n[0] == 4 && g.n[1] == 1);
    // Automatic foundation moves, and undo puts them back
    g.deal(7);
    for (int c = 0; c < 8; ++c) g.n[c] = 0;
    memset(g.found, 0, 4);
    for (uint8_t& c : g.cell) c = 0xFF;
    g.col[0][0] = C(2, 0); g.col[0][1] = C(1, 0); g.n[0] = 2;      // 2S over AS? no: AS on top
    g.col[1][0] = C(13, 1); g.col[1][1] = C(3, 1); g.n[1] = 2;
    CHECK(g.can_move(Col0 + 1, 1, Cell0) && g.move(Col0 + 1, 1, Cell0));
    CHECK(g.found[0] == 2 && g.n[0] == 0);                // the AS and 2S went up by themselves
    CHECK(g.undo() && g.found[0] == 0 && g.n[0] == 2 && g.col[0][1] == C(1, 0) && g.n[1] == 2);
    // Hint player on real deals: nothing lost, undo back to the deal
    int won = 0;
    for (uint32_t s = 1; s <= 40; ++s) {
        g.deal(s * 2654435761u);
        for (int k = 0; k < 300 && !g.won(); ++k) {
            int f, i, to;
            if (!g.hint(&f, &i, &to)) {
                // nothing obvious: put a column's top card into a free cell
                bool moved = false;
                for (int c = 0; c < 8 && !moved; ++c)
                    if (g.n[c] && g.free_cells()) for (int cl = 0; cl < 4 && !moved; ++cl) moved = g.move(Col0 + c, g.n[c] - 1, cl);
                if (!moved) break;
            } else CHECK(g.move(f, i, to));
            CHECK(fc_count(g) == 52);
        }
        won += g.won();
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game* r = new Game();
        CHECK(r->deserialize(buf, sizeof buf) && fc_count(*r) == 52 && r->found[0] == g.found[0]);
        delete r;
        if (g.log_n < kLog) {
            while (g.undo()) {}
            Game* f = new Game();
            f->deal(g.seed);
            bool same = memcmp(f->n, g.n, 8) == 0 && g.free_cells() == 4 && fc_count(g) == 52;
            for (int c = 0; c < 8; ++c) same &= memcmp(f->col[c], g.col[c], g.n[c]) == 0;
            CHECK(same);
            delete f;
        }
    }
    printf("freecell: hint player won %d of 40\n", won);
    delete gp;
}

static void test_blackjack()
{
    using namespace blackjack;
    auto H = [](std::initializer_list<int> ranks) {
        Hand h;
        for (int r : ranks) h.c[h.n++] = uint8_t(r - 1);     // spades
        return h;
    };
    bool soft = false;
    CHECK(H({1, 6}).value(&soft) == 17 && soft);
    CHECK(H({1, 6, 10}).value(&soft) == 17 && !soft);
    CHECK(H({1, 1, 9}).value() == 21 && H({13, 12, 2}).value() == 22);
    CHECK(H({1, 13}).blackjack() && !H({1, 5, 5}).blackjack());
    // Rig a shoe: player A, dealer 9, player K, dealer 7 -> blackjack pays 3:2
    Game* gp = new Game();
    Game& g = *gp;
    g.new_shoe(1);
    const uint8_t rig[] = {0, 8, 12, 6};
    memcpy(g.shoe, rig, 4);
    g.bet = 20;
    CHECK(g.deal() && g.phase == Phase::Done && g.result[0] == Result::Blackjack && g.chips == 530 && g.net == 30);
    // Player 10+6 hits a 5 (21: done), dealer 10+7 stands: win
    g.new_shoe(2);
    const uint8_t rig2[] = {9, 9, 5, 6, 4};
    memcpy(g.shoe, rig2, 5);
    g.chips = 500; g.bet = 10;
    CHECK(g.deal() && g.phase == Phase::Playing && g.can_hit() && g.can_double() && !g.can_split());
    CHECK(g.hit() && g.phase == Phase::Done && g.result[0] == Result::Win && g.chips == 510);
    // Split 8s, double the first
    g.new_shoe(3);
    const uint8_t rig3[] = {7, 9, 20, 8, 2, 9, 12, 12};   // P 8s, D 10+9; then 3, 10 for the hands; 13 for the double
    memcpy(g.shoe, rig3, 8);
    g.chips = 500; g.bet = 10;
    CHECK(g.deal() && g.can_split() && g.split() && g.hands == 2 && g.chips == 480);
    CHECK(g.hand[0].value() == 11 && g.can_double() && g.double_down());
    CHECK(g.active == 1 && g.stand() && g.phase == Phase::Done);
    // hand 0: 8+3+K = 21 vs 19 win (bet 20 -> +20), hand 1: 8+10 = 18 lose (-10)
    CHECK(g.result[0] == Result::Win && g.result[1] == Result::Lose && g.net == 10 && g.chips == 510);
    // Random rounds: chips never negative, stake bookkeeping balances
    g.new_shoe(77);
    g.chips = 500;
    int32_t net_total = 0;
    for (int r = 0; r < 3000; ++r) {
        if (g.chips < 10) g.refill();          // (broke() only below the 5 minimum)
        g.bet = 10;
        const int32_t before = g.chips;
        CHECK(g.deal());
        while (g.phase == Phase::Playing) {
            const int v = g.hand[g.active].value();
            if (g.can_split() && points(g.hand[0].c[0]) == 8) g.split();
            else if (v == 11 && g.can_double()) g.double_down();
            else if (v < 17) g.hit();
            else g.stand();
        }
        CHECK(g.chips == before + g.net && g.chips >= 0);
        net_total += g.net;
    }
    uint8_t* buf = new uint8_t[Game::kSaveBytes];
    CHECK(g.serialize(buf, Game::kSaveBytes) == Game::kSaveBytes);
    Game* h = new Game();
    CHECK(h->deserialize(buf, Game::kSaveBytes) && h->chips == g.chips && h->pos == g.pos && h->dealer.n == g.dealer.n);
    delete h;
    delete[] buf;
    // History
    Record rec{20, Result::Blackjack, 30, 560};
    char body[64];
    CHECK(format_body(body, sizeof body, rec) && strcmp(body, "20,Blackjack,30,560\n") == 0);
    char line[80];
    snprintf(line, sizeof line, "12,%s", body);
    Record back;
    CHECK(parse_line(line, back) && back.result == Result::Blackjack && back.net == 30 && back.chips == 560);
    Summary s; s.add(back);
    CHECK(s.hands == 1 && s.wins == 1 && s.blackjacks == 1 && s.best_chips == 560);
    printf("blackjack: basic-ish play over 3000 rounds of 10: net %ld\n", long(net_total));
    delete gp;
}


// ---- Log packing (Send Log QR code) ------------------------------------------------------
// A small inflater for fixed-Huffman DEFLATE blocks, enough to check ours
static bool inflate_fixed(const uint8_t* in, size_t n, std::string& out)
{
    size_t pos = 0; int bit = 0;
    auto get = [&](int count) -> int {
        int v = 0;
        for (int k = 0; k < count; ++k) {
            if (pos >= n) return -1;
            v |= ((in[pos] >> bit) & 1) << k;
            if (++bit == 8) { bit = 0; ++pos; }
        }
        return v;
    };
    auto code = [&](int len) { int v = 0; for (int k = 0; k < len; ++k) { int b = get(1); if (b < 0) return -1; v = v << 1 | b; } return v; };
    static const int lb[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const int le[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const int db[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const int de[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    if (get(1) != 1 || get(2) != 1) return false;
    for (;;) {
        int v = code(7), sym;
        if (v < 0) return false;
        if (v <= 0x17) sym = 256 + v;
        else {
            v = v << 1 | get(1);
            if (v >= 0x30 && v <= 0xBF) sym = v - 0x30;
            else if (v >= 0xC0 && v <= 0xC7) sym = 280 + v - 0xC0;
            else { v = v << 1 | get(1); if (v < 0x190 || v > 0x1FF) return false; sym = 144 + v - 0x190; }
        }
        if (sym < 256) { out += char(sym); continue; }
        if (sym == 256) return true;
        const int li = sym - 257;
        const int len = lb[li] + get(le[li]);
        const int dc = code(5);
        if (dc < 0 || dc > 29) return false;
        const int dist = db[dc] + get(de[dc]);
        if (dist > int(out.size())) return false;
        for (int k = 0; k < len; ++k) out += out[out.size() - dist];
    }
}

static void test_log_pack()
{
    std::string text;
    for (int boot = 0; boot < 30; ++boot) {
        text += "0:00:00 === Boot: firmware v0.9.0 (1a2b3c4), 2.8\" ST7789 Resistive\n0:00:00 Last reset: power on\n";
        char line[64];
        for (int k = 0; k < 5; ++k) { snprintf(line, sizeof line, "0:%02d:%02d Open game%d\n", k * 7 % 60, (boot * 13 + k) % 60, (boot + k) % 9); text += line; }
    }
    text += "0:00:00 Crash: Task watchdog got triggered.\n0:00:00 Backtrace: 400d8f3c 400d9122 400da410\n";
    std::vector<uint8_t> z(text.size() + 64);
    const size_t zn = logpack::deflate(reinterpret_cast<const uint8_t*>(text.data()), text.size(), z.data(), z.size());
    CHECK(zn > 0 && zn * 4 < text.size());          // logs shrink a lot
    std::string back;
    CHECK(inflate_fixed(z.data(), zn, back) && back == text);
    // Edge cases
    for (const char* t : {"", "a", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "abcabcabcabcx"}) {
        uint8_t o[256]; std::string b;
        const size_t on = logpack::deflate(reinterpret_cast<const uint8_t*>(t), strlen(t), o, sizeof o);
        CHECK(on > 0 && inflate_fixed(o, on, b) && b == t);
    }
    // Base43: 2 bytes -> 3 characters, only QR alphanumeric ones, no space or %
    const uint8_t raw[5] = {0xFF, 0xFF, 0x00, 0x01, 0x80};
    char enc[16];
    CHECK(logpack::base43(raw, 5, enc, sizeof enc) == 8);
    for (const char* c = enc; *c; ++c) CHECK(strchr(logpack::kAlphabet, *c) && *c != ' ' && *c != '%');
    uint8_t dec[5]; int dn = 0;
    auto val = [](char c) { return int(strchr(logpack::kAlphabet, c) - logpack::kAlphabet); };
    for (int k = 0; k + 2 < 8; k += 3) { const int v = val(enc[k]) + 43 * val(enc[k + 1]) + 1849 * val(enc[k + 2]); dec[dn++] = uint8_t(v >> 8); dec[dn++] = uint8_t(v); }
    dec[dn++] = uint8_t(val(enc[6]) + 43 * val(enc[7]));
    CHECK(dn == 5 && memcmp(dec, raw, 5) == 0);
    CHECK(logpack::base43(raw, 5, enc, 8) == 0);   // too small
}

// ---- RPG Dice --------------------------------------------------------------------------------
static void test_rpgdice()
{
    using namespace rpgdice;
    Pool p;
    CHECK(p.empty());
    p.add(D6); p.add(D6); p.add(D8); p.bump_mod(3);
    char t[64];
    p.format(t, sizeof t);
    CHECK(strcmp(t, "2d6 + 1d8 + 3") == 0);
    Pool q; q.add(D20); q.bump_mod(-2); q.format(t, sizeof t);
    CHECK(strcmp(t, "1d20 - 2") == 0);
    Pool full; for (int k = 0; k < kMaxDice; ++k) CHECK(full.add(D4));
    CHECK(!full.add(D4));
    for (int k = 0; k < 300; ++k) full.bump_mod(1);
    CHECK(full.mod == kMaxMod);
    // Every die lands in range and each face comes up
    Rng rng(12345);
    for (int d = 0; d < kDieTypes; ++d) {
        Pool one; one.add(d);
        bool seen[101] = {};
        for (int k = 0; k < 20000; ++k) {
            Rolled r; roll(one, rng, r);
            CHECK(r.n == 1 && r.value[0] >= 1 && r.value[0] <= kSides[d] && r.total == r.value[0]);
            seen[r.value[0]] = true;
        }
        for (int v = 1; v <= kSides[d]; ++v) CHECK(seen[v]);
    }
    // Totals add up with the modifier; history text
    State* s = new State();
    s->pool = p;
    s->roll_pool(rng);
    const Rolled& r = s->last[0];
    CHECK(r.n == 3 && r.total == r.value[0] + r.value[1] + r.value[2] + 3);
    CHECK(s->history(0) && strstr(s->history(0), "2d6+1d8+3:") == s->history(0));
    // A preset: four lines rolled together
    Preset& pr = s->presets[2];
    snprintf(pr.name, sizeof pr.name, "Fighter");
    pr.lines = 2;
    pr.pool[0].add(D20); pr.pool[0].mod = 7; pr.pool[0].label = 1;
    pr.pool[1].add(D8);  pr.pool[1].mod = 5; pr.pool[1].label = 2;
    s->roll_preset(2, rng);
    CHECK(s->shown == 2 && s->preset == 2);
    CHECK(s->last[0].total >= 8 && s->last[0].total <= 27 && s->last[1].total >= 6 && s->last[1].total <= 13);
    CHECK(strncmp(s->history(0), "Fighter: Hit ", 13) == 0);
    CHECK(s->hist_n == 2);
    // History ring keeps the newest kHistory
    for (int k = 0; k < kHistory + 5; ++k) s->roll_pool(rng);
    CHECK(s->hist_n == kHistory && s->history(kHistory) == nullptr);
    // Save and load
    std::vector<uint8_t> buf(State::kSaveBytes);
    const size_t n = s->serialize(buf.data(), buf.size());
    State* b = new State();
    CHECK(n > 0 && b->deserialize(buf.data(), n));
    CHECK(strcmp(b->presets[2].name, "Fighter") == 0 && b->presets[2].lines == 2 && b->presets[2].pool[1].mod == 5);
    CHECK(b->hist_n == kHistory && strcmp(b->history(0), s->history(0)) == 0);
    CHECK(b->last[0].total == s->last[0].total && b->shown == s->shown);
    CHECK(!b->deserialize(buf.data(), 100));
    // Coin alone reads Heads / Tails
    Rolled c{}; c.n = 1; c.die[0] = Coin; c.value[0] = 2; c.total = 2;
    format_rolled(c, t, sizeof t, false);
    CHECK(strcmp(t, "Coin: Heads") == 0);
    delete s;
    delete b;
}

// ---- Video Poker -------------------------------------------------------------------------------
static void test_vpoker()
{
    using namespace vpoker;
    // Cards from text: "AS KH 10D 2C" (rank, suit S H D C)
    auto hand = [](const char* t, uint8_t out[5]) {
        for (int i = 0; i < 5; ++i) {
            while (*t == ' ') ++t;
            int r;
            if (*t == 'A') { r = 1; ++t; } else if (*t == 'K') { r = 13; ++t; } else if (*t == 'Q') { r = 12; ++t; }
            else if (*t == 'J') { r = 11; ++t; } else { r = 0; while (*t >= '0' && *t <= '9') r = r * 10 + (*t++ - '0'); }
            const int s = *t == 'S' ? 0 : *t == 'H' ? 1 : *t == 'D' ? 2 : 3;
            ++t;
            out[i] = uint8_t(s * 13 + r - 1);
        }
    };
    struct Case { const char* cards; Rank r; };
    const Case ranks[] = {
        {"10H JH QH KH AH", RoyalFlush}, {"5C 6C 7C 8C 9C", StraightFlush}, {"AS 2S 3S 4S 5S", StraightFlush},
        {"9S 9H 9D 9C 2H", FourKind}, {"3S 3H 3D 7C 7H", FullHouse}, {"2D 8D JD 4D KD", Flush},
        {"AS 2H 3D 4C 5S", Straight}, {"10S JH QD KC AS", Straight}, {"QS KH AD 2C 3S", Nothing},
        {"7S 7H 7D 2C 9H", ThreeKind}, {"4S 4H 9D 9C KH", TwoPair}, {"JS JH 2D 5C 8H", JacksOrBetter},
        {"10S 10H 2D 5C 8H", Nothing}, {"AS 3H 6D 9C QH", Nothing},
    };
    for (const Case& c : ranks) { uint8_t h[5]; hand(c.cards, h); CHECK(evaluate(h) == c.r); }
    CHECK(pay(RoyalFlush, 5) == 4000 && pay(RoyalFlush, 4) == 1000 && pay(FullHouse, 3) == 27 && pay(Nothing, 5) == 0);
    // The simple strategy's choices (bit i = card i held)
    struct Hold { const char* cards; uint8_t mask; };
    const Hold holds[] = {
        {"10H JH QH KH 2C", 0x0F},             // 4 to a royal
        {"10H JH QH KH AS", 0x0F},             // 4 to a royal beats the straight
        {"7S 7H 7D 2C 9H", 0x07},              // trips
        {"4S 4H 9D 9C KH", 0x0F},              // two pair
        {"JS JH 2D 5C 8H", 0x03},              // high pair
        {"JS QS KS 3D 3H", 0x07},              // 3 to a royal beats a low pair
        {"2S 6S 9S QS 4H", 0x0F},              // 4 to a flush
        {"5S 5H 9D JC 2H", 0x03},              // low pair
        {"5S 6H 7D 8C KH", 0x0F},              // 4 to an open straight
        {"QS KS 3D 7C 9H", 0x03},              // 2 suited high cards
        {"JS QH KD 4C 2H", 0x03},              // three unsuited high cards: the lowest two
        {"10S JS 3D 6C 8H", 0x03},             // suited 10 and J
        {"KS 3H 5D 8C 9S", 0x01},              // one high card
        {"2S 5H 7D 9C 4S", 0x00},              // nothing: draw five
    };
    for (const Hold& c : holds) {
        uint8_t h[5]; hand(c.cards, h);
        const uint8_t m = hint(h);
        if (m != c.mask) printf("vpoker hint %s: %02x, expected %02x\n", c.cards, m, c.mask);
        CHECK(m == c.mask);
    }
    // A round: bet comes off, the draw pays
    Game g;
    g.bet = 5;
    CHECK(g.deal(42) && g.credits == kStartCredits - 5 && g.phase == Phase::Dealt);
    g.toggle_hold(0);
    const uint8_t kept = g.hand[0];
    CHECK(g.draw() && g.hand[0] == kept && g.phase == Phase::Done);
    CHECK(g.credits == kStartCredits - 5 + pay(g.last_rank, 5));
    uint8_t buf[Game::kSaveBytes];
    Game b;
    CHECK(g.serialize(buf, sizeof buf) == sizeof buf && b.deserialize(buf, sizeof buf));
    CHECK(b.credits == g.credits && memcmp(b.hand, g.hand, 5) == 0 && b.phase == Phase::Done);
    Record r{5, TwoPair, 10, 515}, back;
    char line[64] = "7,";
    format_body(line + 2, sizeof line - 2, r);
    CHECK(parse_line(line, back) && back.rank == TwoPair && back.win == 10 && back.credits == 515);
    // The hint's play returns close to the full-pay strategy's 99.5 % (frequent hands only: no royals luck)
    Game sim;
    sim.credits = 1 << 30;
    long hands = 0, pairs_up = 0;
    for (int n = 0; n < 20000; ++n) {
        sim.bet = 5;
        sim.deal(uint32_t(n) * 2654435761u + 9);
        sim.held = hint(sim.hand);
        sim.draw();
        ++hands;
        pairs_up += sim.last_rank >= JacksOrBetter;
    }
    printf("vpoker: hint play won %ld of %ld hands\n", pairs_up, hands);
    CHECK(pairs_up * 100 > hands * 43 && pairs_up * 100 < hands * 48);   // ~45.4 %
}

// ---- Texas Hold'em ------------------------------------------------------------------------------
static void test_holdem()
{
    using namespace holdem;
    auto C = [](int rank, int suit) { return uint8_t(suit * 13 + rank - 1); };
    // Hand values: categories and order
    const uint8_t royal[7] = {C(10, 1), C(11, 1), C(12, 1), C(13, 1), C(1, 1), C(2, 0), C(3, 2)};
    const uint8_t wheel[7] = {C(1, 0), C(2, 1), C(3, 2), C(4, 3), C(5, 0), C(9, 1), C(13, 2)};
    const uint8_t boat[7]  = {C(7, 0), C(7, 1), C(7, 2), C(9, 3), C(9, 0), C(9, 1), C(2, 2)};
    const uint8_t flush[7] = {C(2, 3), C(6, 3), C(9, 3), C(11, 3), C(13, 3), C(13, 0), C(13, 1)};
    CHECK(category(score(royal, 7)) == StraightFlush);
    CHECK(category(score(wheel, 7)) == Straight && ((score(wheel, 7) >> 16) & 15) == 5);
    CHECK(category(score(boat, 7)) == FullHouse && ((score(boat, 7) >> 16) & 15) == 9);   // 999 over 77
    CHECK(category(score(flush, 7)) == Flush);
    const uint8_t pairA[5] = {C(1, 0), C(1, 1), C(5, 2), C(8, 3), C(10, 0)};
    const uint8_t pairK[5] = {C(13, 0), C(13, 1), C(12, 2), C(11, 3), C(9, 0)};
    CHECK(score(pairA, 5) > score(pairK, 5));
    // 7 cards = the best five of them
    Rng rng(5);
    for (int t = 0; t < 3000; ++t) {
        uint8_t d[52];
        for (int i = 0; i < 52; ++i) d[i] = uint8_t(i);
        for (int i = 51; i > 0; --i) { const int j = rng.below(i + 1); const uint8_t x = d[i]; d[i] = d[j]; d[j] = x; }
        uint32_t best = 0;
        for (int a = 0; a < 7; ++a) for (int b = a + 1; b < 7; ++b) {
            uint8_t f[5]; int k = 0;
            for (int i = 0; i < 7; ++i) if (i != a && i != b) f[k++] = d[i];
            const uint32_t v = score(f, 5);
            if (v > best) best = v;
        }
        CHECK(score(d, 7) == best);
    }
    // Side pots: A all in for 100 with the best hand, B and C in for 300, D folded 50
    {
        Game g;
        g.street = Street::River;
        const uint8_t board[5] = {C(2, 0), C(7, 1), C(9, 2), C(12, 3), C(4, 0)};
        memcpy(g.board, board, 5);
        g.board_n = 5;
        int32_t totals[4] = {100, 300, 300, 50};
        const uint8_t cards[4][2] = {{C(12, 0), C(12, 1)}, {C(9, 0), C(9, 1)}, {C(3, 0), C(5, 1)}, {C(13, 0), C(13, 1)}};
        for (int s = 0; s < 4; ++s) {
            g.seat[s].total = totals[s];
            g.seat[s].stack = 0;
            g.seat[s].cards[0] = cards[s][0];
            g.seat[s].cards[1] = cards[s][1];
        }
        g.seat[3].folded = true;
        g.finish();
        CHECK(g.showdown && g.seat[0].won == 350 && g.seat[1].won == 400 && g.seat[2].won == 0 && g.seat[3].won == 0);
    }
    // Whole hands between computer players: chips stay put, every hand ends
    for (int lv = 0; lv < 3; ++lv) {
        Game g;
        g.level = uint8_t(lv);
        long refills = 0, steps = 0;
        for (int h = 0; h < 60; ++h) {
            if (g.seat[0].stack <= 0) { g.seat[0].stack = kStartStack; ++refills; }
            g.new_hand(rng.next());
            while (!g.hand_over() && steps < 200000) { g.act(g.decide(rng)); ++steps; }
            CHECK(g.hand_over());
            long chips = 0, rebuys = 0;
            for (const Seat& s : g.seat) { chips += s.stack; rebuys += s.rebuys; CHECK(s.stack >= 0); }
            CHECK(chips == kStartStack * (kSeats + rebuys + refills));
        }
        uint8_t buf[Game::kSaveBytes];
        Game b;
        CHECK(g.serialize(buf, sizeof buf) == sizeof buf && b.deserialize(buf, sizeof buf));
        CHECK(b.seat[2].stack == g.seat[2].stack && b.hand_no == g.hand_no && b.dealer == g.dealer);
    }
    // Your actions: fold never happens for free; a raise below the minimum is raised to it
    {
        Game g;
        g.new_hand(77);
        while (g.to_act != 0 && !g.hand_over()) g.act(g.decide(rng));
        if (!g.hand_over()) {
            const int32_t before = g.current_bet;
            g.act({Act::Raise, before + 1});
            CHECK(g.current_bet >= before + kBigBlind || g.seat[0].all_in);
        }
    }
    Record r{Result::Won, 140, 1250}, back;
    char line[64] = "12,";
    format_body(line + 3, sizeof line - 3, r);
    CHECK(parse_line(line, back) && back.result == Result::Won && back.net == 140 && back.chips == 1250);
}

// ---- Farkle ----------------------------------------------------------------------------------
static void test_farkle()
{
    using namespace farkle;
    struct Case { const char* dice; int score; };
    const Case cases[] = {
        {"1", 100}, {"5", 50}, {"15", 150}, {"2", -1}, {"12", -1}, {"222", 200}, {"111", 1000},
        {"666", 600}, {"2222", 1000}, {"22222", 2000}, {"222222", 3000}, {"123456", 1500},
        {"223344", 1500}, {"222333", 2500}, {"222255", 1500}, {"1115", 1050}, {"55555", 2000},
        {"111111", 3000}, {"115", 250}, {"", 0},
    };
    for (const Case& c : cases) {
        uint8_t v[6]; int n = 0;
        for (const char* d = c.dice; *d; ++d) v[n++] = uint8_t(*d - '0');
        const int sc = score_exact(v, n);
        if (sc != c.score) printf("farkle %s: %d, expected %d\n", c.dice, sc, c.score);
        CHECK(sc == c.score);
    }
    const uint8_t farkle_roll[6] = {2, 3, 4, 6, 6, 2};
    CHECK(best_set(farkle_roll, 6) == 0);
    uint8_t m;
    const uint8_t mixed[6] = {1, 5, 3, 3, 3, 2};
    CHECK(best_set(mixed, 6, &m) == 450 && m == 0x1F);
    // A turn by hand: roll, pick, bank
    Rng rng(4);
    Game g;
    CHECK(g.can_roll() && !g.can_bank());
    CHECK(g.roll(rng));
    if (g.phase == Phase::Rolled) {
        uint8_t mask;
        best_set(g.dice, 6, &mask);
        for (int i = 0; i < 6; ++i) if ((mask >> i) & 1) g.toggle(i);
        const int s = g.picked_score();
        CHECK(s > 0 && g.can_bank());
        CHECK(g.bank() && g.score[0] == s && g.turn == 1 && g.phase == Phase::Start);
    }
    // Computer against computer: games end, the scores add up, Hard beats Easy
    int hard_wins = 0;
    const int games = 300;
    for (int k = 0; k < games; ++k) {
        Game x;
        const int hard_side = k & 1;
        int steps = 0;
        while (!x.over() && steps < 20000) {
            ++steps;
            if (x.phase == Phase::Start) { x.roll(rng); continue; }
            if (x.phase == Phase::Farkle) { x.next_turn(); continue; }
            const Plan p = plan(x, x.turn == hard_side ? 2 : 0);
            CHECK(p.pick != 0);
            x.picked = p.pick;
            CHECK(x.picked_score() > 0);
            if (p.roll_on) x.roll(rng); else x.bank();
        }
        CHECK(x.over());
        CHECK(x.score[x.winner] >= x.score[x.winner ^ 1]);
        hard_wins += x.winner == hard_side;
        if (k == 0) {
            uint8_t buf[Game::kSaveBytes];
            Game b;
            CHECK(x.serialize(buf, sizeof buf) == sizeof buf && b.deserialize(buf, sizeof buf));
            CHECK(b.score[0] == x.score[0] && b.score[1] == x.score[1] && b.phase == x.phase);
        }
    }
    printf("farkle: Hard beat Easy in %d of %d games\n", hard_wins, games);
    CHECK(hard_wins > games * 55 / 100);
}

// ---- Mancala ---------------------------------------------------------------------------------
static void test_mancala()
{
    using namespace mancala;
    Board b;
    // Pit 2 has 4 seeds: 3, 4, 5, store -> again
    Sowing how;
    CHECK(b.play(2, &how) && how.again && b.side == 0 && b.pit[kStore[0]] == 1 && how.n == 4);
    CHECK(how.path[3] == kStore[0] && b.pit[2] == 0);
    // Then pit 5 (5 seeds): store, 7, 8, 9, 10 -> Blue's turn
    CHECK(b.play(5, &how) && !how.again && b.side == 1 && b.pit[kStore[0]] == 2 && b.pit[7] == 5);
    // Skips the other store: Blue sows 13 from a pit, never into Gold's store
    Board c;
    for (int i = 0; i < 14; ++i) c.pit[i] = 0;
    c.side = 1; c.pit[pit_index(1, 0)] = 13; c.pit[0] = 35;      // 48 seeds in all
    const int gold_store = c.pit[kStore[0]];
    CHECK(c.play(0, &how) && c.pit[kStore[0]] == gold_store);
    for (int k = 0; k < how.n; ++k) CHECK(how.path[k] != kStore[0]);
    // Capture: Gold's last seed in its empty pit 4 takes the opposite pit (8)
    Board d;
    for (int i = 0; i < 14; ++i) d.pit[i] = 0;
    d.pit[3] = 1; d.pit[opposite(4)] = 6; d.pit[1] = 2; d.pit[pit_index(1, 2)] = 39; // 48
    CHECK(d.play(3, &how) && how.captured == 7 && how.captured_from == opposite(4));
    CHECK(d.pit[4] == 0 && d.pit[opposite(4)] == 0 && d.pit[kStore[0]] == 7);
    // The end: Blue empties its side -> Gold sweeps its own seeds
    Board e;
    for (int i = 0; i < 14; ++i) e.pit[i] = 0;
    e.side = 1; e.pit[pit_index(1, 5)] = 1; e.pit[0] = 10; e.pit[kStore[0]] = 17; e.pit[kStore[1]] = 20;
    CHECK(e.play(5, &how) && how.ended && e.over() && e.pit[kStore[0]] == 27 && e.pit[kStore[1]] == 21);
    CHECK(e.result() == 0);
    // Seeds are never lost; computer levels, then the save
    int wins[3] = {0, 0, 0};
    for (int g = 0; g < 30; ++g) {
        Board x;
        const int strong = g % 3 == 1 ? 1 : 2, weak = g % 3 == 2 ? 1 : 0;
        const int strong_side = g & 1;
        int guard = 0;
        while (!x.over() && ++guard < 400) {
            const int m = best_move(x, x.side == strong_side ? strong : weak, uint32_t(g * 31 + guard));
            CHECK(x.can_play(m));
            x.play(m);
            int total = 0;
            for (int i = 0; i < 14; ++i) total += x.pit[i];
            CHECK(total == 48);
        }
        CHECK(x.over());
        if (x.result() == strong_side) ++wins[g % 3];
        if (g == 0) {
            uint8_t buf[Board::kSaveBytes];
            Board y;
            CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
            CHECK(memcmp(x.pit, y.pit, 14) == 0 && y.side == x.side && y.moves == x.moves);
        }
    }
    printf("mancala: Hard beat Easy %d/10, Medium beat Easy %d/10, Hard beat Medium %d/10\n", wins[0], wins[1], wins[2]);
    CHECK(wins[0] >= 8 && wins[2] >= 6);
}

// ---- Nine Men's Morris ------------------------------------------------------------------------
static void test_morris()
{
    using namespace morris;
    // The board: 24 points, every point on exactly two mills, 32 links
    int on[kPoints] = {}, links = 0;
    for (const auto& l : kMills) for (int k = 0; k < 3; ++k) ++on[l[k]];
    for (int p = 0; p < kPoints; ++p) { CHECK(on[p] == 2); links += __builtin_popcount(neighbours(p)); }
    CHECK(links == 64);
    CHECK(neighbours(4) == ((1u << 1) | (1u << 3) | (1u << 5) | (1u << 7)));
    Game g;
    MoveList l;
    g.legal(l);
    CHECK(l.n == 24);
    auto code = [](int from, int to, int remove) { Move m; m.from = int8_t(from); m.to = int8_t(to); m.remove = int8_t(remove); return m.code(); };
    // White 0, Black 9, White 1, Black 10, White 2 = mill: must take one
    CHECK(g.play(code(-1, 0, -1)) && g.play(code(-1, 9, -1)) && g.play(code(-1, 1, -1)) && g.play(code(-1, 10, -1)));
    CHECK(!g.play(code(-1, 2, -1)));                 // a mill without a capture isn't legal
    CHECK(g.play(code(-1, 2, 9)) && g.pos.cell(9) == -1 && g.pos.on_board(1) == 1);
    // Men in a mill are safe while others aren't
    Position p;
    p.hand[0] = p.hand[1] = 0;
    p.men[1] = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 23);
    CHECK(p.removable(1) == (1u << 23));
    p.men[1] = (1u << 0) | (1u << 1) | (1u << 2);
    CHECK(p.removable(1) == p.men[1]);              // all in mills: any
    // Flying with three men; two men left = a loss
    Game f;
    f.pos.hand[0] = f.pos.hand[1] = 0;
    f.pos.men[0] = (1u << 0) | (1u << 4) | (1u << 23);
    f.pos.men[1] = (1u << 9) | (1u << 10) | (1u << 11) | (1u << 12);
    f.legal(l);
    int from0 = 0;
    for (int k = 0; k < l.n; ++k) from0 += l.m[k].from == 0;
    CHECK(from0 == kPoints - 7);                     // to every empty point
    f.pos.men[0] = (1u << 0) | (1u << 4);
    CHECK(f.result() == 1);
    // Blocked = loss
    Game b;
    b.pos.hand[0] = b.pos.hand[1] = 0;
    b.pos.men[0] = (1u << 0) | (1u << 2) | (1u << 21) | (1u << 23);
    b.pos.men[1] = (1u << 1) | (1u << 9) | (1u << 14) | (1u << 22) | (1u << 4) | (1u << 10) | (1u << 13) | (1u << 19);
    CHECK(b.result() == 1);
    // Computer games: always legal, they end, the save round-trips
    int hard_wins = 0;
    for (int k = 0; k < 6; ++k) {
        Game x;
        const int hard_side = k & 1;
        int guard = 0;
        while (x.result() == -1 && ++guard < 700) {
            const int m = best_move(x, x.turn() == hard_side ? 2 : 0, uint32_t(k * 101 + guard));
            CHECK(x.play(m));
        }
        CHECK(x.result() != -1);
        hard_wins += x.result() == hard_side;
        if (k == 0) {
            uint8_t buf[Game::kSaveBytes];
            Game y;
            CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
            CHECK(y.pos.men[0] == x.pos.men[0] && y.pos.men[1] == x.pos.men[1] && y.plies == x.plies && y.result() == x.result());
        }
    }
    printf("morris: Hard beat Easy %d of 6\n", hard_wins);
    CHECK(hard_wins >= 5);
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
    test_mastercyd();
    test_pegs();
    test_memory();
    test_nonogram();
    test_solitaire();
    test_golf();
    test_pyramid();
    test_spider();
    test_freecell();
    test_blackjack();
    test_log_pack();
    test_rpgdice();
    test_vpoker();
    test_holdem();
    test_farkle();
    test_mancala();
    test_morris();
    test_stats();
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
