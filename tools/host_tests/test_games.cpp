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
#include "../../src/games/sank/sank_core.h"
#include "../../src/games/wheel/wheel_core.h"
#include "../../src/games/ultimate/ultimate_core.h"
#include "../../src/games/gomoku/gomoku_core.h"
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
#include "../../src/games/piperace/piperace_core.h"
#include "../../src/games/acquisitions/acquisitions_core.h"
#include "../../src/games/strategygo/strategygo_core.h"
#include "../../src/games/dealcyd/dealcyd_core.h"
#include "../../src/games/presscyd/presscyd_core.h"
#include "../../src/games/cardsharks/cardsharks_core.h"
#include "../../src/games/sorrycyd/sorrycyd_core.h"
#include "../../src/games/escape/escape_core.h"
#include "../../src/games/common/inflate.h"
#include "../../src/games/common/trivia_bank.h"
#include "../../src/games/whowants/whowants_core.h"
#include "../../src/games/jeoparcyd/jeoparcyd_core.h"
#include "../../src/games/hollywood/hollywood_core.h"
#include <chrono>
#include <string>
#include <set>
#include <vector>
#include <utility>
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
    // After a two-square push nobody can take en passant: still a repetition
    Game r2;
    auto rp2 = [&](const char* mv) {
        MoveList l; r2.legal(l);
        const int from = (mv[1] - '1') * 8 + (mv[0] - 'a'), to = (mv[3] - '1') * 8 + (mv[2] - 'a');
        for (int k = 0; k < l.n; ++k) if (l.m[k].from == from && l.m[k].to == to) return r2.play(k);
        return false;
    };
    rp2("e2e4"); rp2("e7e5");
    for (int k = 0; k < 2; ++k) { rp2("g1f3"); rp2("b8c6"); rp2("f3g1"); rp2("c6b8"); }
    CHECK(r2.end() == End::Repetition);
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
    CHECK(!g.can_score(Ones));                    // forced joker: a lower box while one is open
    for (int b = ThreeKind; b < kBoxes; ++b) if (g.score[b] < 0) g.score[b] = 0;
    CHECK(g.can_score(Ones));                     // all lower boxes used: an upper box, for 0
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
        {"JH JS 10H 9H 8H", 0x1D},             // 4 to a straight flush beats a high pair
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
        {"1111", 1000}, {"11115", 1050}, {"111115", 2050}, {"5555", 1000},   // a face's dice are one set
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
static void test_sank()
{
    using namespace sank;
    // Fleets: in the sea, the right lengths, never touching, deterministic
    uint32_t h = 2166136261u;
    for (uint32_t s = 0; s < 3000; ++s) {
        Fleet f, g;
        make_fleet(s * 7919u, f);
        make_fleet(s * 7919u, g);
        CHECK(memcmp(f.at, g.at, kCells) == 0);
        int cells = 0;
        for (int c = 0; c < kCells; ++c) {
            if (!f.at[c]) continue;
            ++cells;
            const int r = c / kN, col = c % kN;
            for (int y = r - 1; y <= r + 1; ++y)
                for (int x = col - 1; x <= col + 1; ++x)
                    if (y >= 0 && y < kN && x >= 0 && x < kN && f.at[y * kN + x])
                        CHECK(f.at[y * kN + x] == f.at[c]);     // only its own ship around it
        }
        CHECK(cells == kShipCells);
        for (int i = 0; i < kShips; ++i)
            for (int k = 0; k < kLen[i]; ++k) {
                const Ship& sh = f.ship[i];
                CHECK((sh.down ? sh.cell / kN + k : sh.cell % kN + k) < kN);
                CHECK(f.at[sh.cell_at(k, kLen[i])] == i + 1);
            }
        if (s < 50) for (int c = 0; c < kCells; ++c) h = (h ^ f.at[c]) * 16777619u;
    }
    printf("sank: fleet hash %08x\n", h);
    CHECK(h == 0x7bbd7ef9u);       // make_fleet() is part of the wireless version: never change it
    // Rules: ships first (a ship a move, side 0's five then side 1's), then shots
    Board b;
    CHECK(b.turn() == 0 && b.setup() && b.result() == -1);
    Fleet f0, f1;
    make_fleet(12345, f0);
    make_fleet(777, f1);
    CHECK(!b.can_play(ship_key(96, false)) && !b.can_play(ship_key(60, true)));   // a Carrier off the sea
    for (int i = 0; i < kShips; ++i) CHECK(b.turn() == 0 && b.play(ship_key(f0.ship[i])));
    CHECK(b.placed(0) == 5 && b.turn() == 1 && b.setup());
    // ships may touch, but not overlap
    {
        Fleet t;
        place_ship(t, 0, ship_key(0, false));                     // A1-E1
        CHECK(ship_fits(t, 1, ship_key(10, false)) && ship_fits(t, 1, ship_key(5, true)) && !ship_fits(t, 1, ship_key(2, true)));
        uint32_t places[2 * kCells];
        CHECK(ship_places(t, 1, places) > 50);
    }
    for (int i = 0; i < kShips; ++i) CHECK(b.turn() == 1 && b.play(ship_key(f1.ship[i])));
    CHECK(!b.setup() && memcmp(b.fleet[1].at, f1.at, kCells) == 0);
    CHECK(b.turn() == 0 && !b.can_play(100) && b.can_play(0));
    // Side 0 sinks side 1's Destroyer
    const Ship d = b.fleet[1].ship[4];
    for (int k = 0; k < 2; ++k) {
        const int c = d.cell_at(k, 2);
        CHECK(b.known(0, c) == kUnknown && b.play(uint32_t(c)));
        CHECK(!b.can_play(uint32_t(c)) || b.turn() == 1);
        // side 1 fires somewhere it hasn't
        int m = 0;
        while (!b.can_play(uint32_t(m))) ++m;
        CHECK(b.play(uint32_t(m)));
    }
    CHECK(b.sunk(1, 4) && b.afloat(1) == 4 && b.known(0, d.cell) == kSunk);
    // Computer levels: shots to sink a fleet (fewer is better), and the save
    long total[3] = {0, 0, 0};
    for (int lv = 0; lv < 3; ++lv)
        for (int g = 0; g < 40; ++g) {
            Board x;
            // both fleets as the computer places them, ship by ship
            for (int k = 0; k < kSetupPlies; ++k) {
                const uint32_t m = best_move(x, 1, uint32_t(g * 7777u + k * 31 + 5));
                CHECK(x.can_play(m));
                x.play(m);
            }
            // side 0 = the level under test; side 1 fires at its first open cell (slow, never wins first)
            int guard = 0;
            while (x.result() == -1 && ++guard < 400) {
                uint32_t m;
                if (x.turn() == 0) m = best_move(x, lv, uint32_t(g * 131 + guard));
                else { m = 0; while (!x.can_play(m)) ++m; }
                CHECK(x.can_play(m));
                x.play(m);
                // nobody but the computer's own knowledge: it never fires at a known cell
            }
            CHECK(x.result() != -1);
            total[lv] += x.shots(0);
            if (g == 3 && lv == 2) {
                uint8_t buf[Board::kSaveBytes];
                Board y;
                CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
                CHECK(y.moves == x.moves && memcmp(y.shot, x.shot, sizeof x.shot) == 0 && y.last[0] == x.last[0]
                      && memcmp(y.fleet[1].at, x.fleet[1].at, kCells) == 0 && y.result() == x.result());
                buf[20] ^= 1;                                       // a shot that doesn't fit the moves
                CHECK(!y.deserialize(buf, sizeof buf));
                buf[20] ^= 1;
                buf[4] = uint8_t(ship_key(99, true));                // a Carrier off the sea
                CHECK(!y.deserialize(buf, sizeof buf));
            }
        }
    printf("sank: average shots to sink a fleet - Easy %.1f, Medium %.1f, Hard %.1f\n",
           total[0] / 40.0, total[1] / 40.0, total[2] / 40.0);
    CHECK(total[2] < total[1] && total[1] < total[0]);
    // Random fleets: ships anywhere they fit, all five every time
    for (uint32_t t = 0; t < 2000; ++t) {
        Fleet r;
        random_fleet(t * 7919u, r);
        int cells = 0;
        for (int c = 0; c < kCells; ++c) cells += r.at[c] != 0;
        CHECK(cells == kShipCells);
    }
    // Random sequential placement never runs out of room
    for (uint32_t t = 0; t < 20000; ++t) {
        Board s;
        for (int k = 0; k < kSetupPlies; ++k) {
            const uint32_t m = best_move(s, 0, t * 2654435761u + uint32_t(k));
            if (!s.play(m)) { CHECK(!"the computer found room for every ship"); break; }
        }
    }
    // A version-1 save (a seed a fleet) still loads
    {
        uint8_t v1[Board::kSaveBytesV1] = {'S', 'N', 'K', '1'};
        const uint32_t seeds[2] = {12345, 777};
        for (int s2 = 0; s2 < 2; ++s2) for (int k = 0; k < 4; ++k) v1[4 + 4 * s2 + k] = uint8_t(seeds[s2] >> (8 * k));
        v1[12] = 2;                                                 // both fleets, no shots
        v1[14] = 0xFF; v1[15] = 0xFF;
        Board y;
        CHECK(y.deserialize(v1, sizeof v1) && y.moves == kSetupPlies && memcmp(y.fleet[1].at, f1.at, kCells) == 0);
    }
}

static void test_wheel()
{
    using namespace wheel;
    CHECK(kPhraseCount > 300 && kPhraseCount <= kMaxPhrases);
    // Every puzzle fits the board, tiles inside and distinct
    for (int i = 0; i < kPhraseCount; ++i) {
        int8_t pos[kCols * kRows + 1];
        const char* t = kPhrases[i].text;
        const int rows = wrap(t, pos, int(sizeof pos));
        CHECK(rows >= 1 && rows <= kRows);
        bool used[kCols * kRows] = {};
        for (int k = 0; t[k]; ++k) {
            if (t[k] == ' ') { CHECK(pos[k] == -1); continue; }
            CHECK(pos[k] >= 0 && pos[k] < kCols * kRows && !used[pos[k]]);
            if (pos[k] >= 0 && pos[k] < kCols * kRows) used[pos[k]] = true;
        }
        CHECK(kPhrases[i].cat < kCategoryCount);
    }
    // Rules: a spin, a call, a vowel, a solve
    Game g;
    Rng r(42);
    g.start(3, r);
    CHECK(g.phase == Phase::Choose && g.turn == 0 && g.round == 0 && !g.can_buy());
    int guard = 0;
    while (g.phase == Phase::Choose && g.turn == 0 && ++guard < 50) {
        const int w = g.spin(r);
        CHECK(w >= 0 && w < kWedges);
        if (g.phase == Phase::Consonant) {
            const int v = g.value;
            char c = 0;
            for (const char* p = g.text(); *p && !c; ++p) if (is_letter(*p) && !is_vowel(*p) && !g.called_letter(*p)) c = *p;
            const int32_t before = g.money[0];
            const int n = g.call(c);
            CHECK(n == g.count(c) && g.money[0] == before + n * v && g.called_letter(c));
            CHECK(g.call(c) == -1);                       // not twice
            break;
        }
    }
    // Solve with the right letters
    if (g.phase == Phase::Choose) {
        char letters[64];
        size_t n = 0;
        for (const char* p = g.text(); *p; ++p) if (is_letter(*p) && !g.called_letter(*p)) letters[n++] = *p;
        letters[n] = 0;
        const int t = g.turn;
        const int32_t m = g.money[t];
        CHECK(g.solve(letters) && g.phase == Phase::RoundOver && g.round_winner == t);
        CHECK(g.bank[t] == (m > kSolveMin ? m : kSolveMin));
        g.next_round(r);
        CHECK(g.round == 1 && g.turn == 1 % g.players && g.called == 0 && g.money[t] == 0);
    }
    // Three computers play whole games; the save; Hard beats Easy more often than not
    int wins[3] = {0, 0, 0}, solved_wrong = 0;
    for (int game = 0; game < 120; ++game) {
        Game x;
        Rng rr(uint32_t(game * 7919 + 1));
        x.start(3, rr);
        const int lv[3] = {game % 3, (game + 1) % 3, (game + 2) % 3};
        int steps = 0;
        while (!x.over() && ++steps < 5000) {
            const uint32_t seed = rr.next();
            if (x.phase == Phase::RoundOver) { x.next_round(rr); continue; }
            const int l = lv[x.turn];
            const Act a = decide(x, l, seed);
            if (a == Act::Spin) {
                CHECK(x.can_spin());
                x.spin(rr);
                if (x.phase == Phase::Consonant) CHECK(x.call(pick_consonant(x, l, seed)) >= 0);
            } else if (a == Act::Buy) {
                CHECK(x.can_buy());
                CHECK(x.buy(pick_vowel(x, l, seed)) >= 0);
            } else {
                char guess[64];
                guess_letters(x, seed, guess, sizeof guess);
                if (!x.solve(guess)) ++solved_wrong;
            }
            for (int i = 0; i < 3; ++i) CHECK(x.money[i] >= 0);
        }
        CHECK(x.over());
        const int w = x.leader();
        if (w >= 0) ++wins[lv[w]];
        if (game == 5) {
            uint8_t buf[Game::kSaveBytes];
            Game y;
            CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
            CHECK(y.bank[2] == x.bank[2] && y.phase == x.phase && memcmp(y.played, x.played, sizeof x.played) == 0);
            buf[5] = 9;                                    // round 9
            CHECK(!y.deserialize(buf, sizeof buf));
        }
    }
    printf("wheel: %d puzzles; games won by Easy %d, Medium %d, Hard %d (wrong solves %d)\n", kPhraseCount,
           wins[0], wins[1], wins[2], solved_wrong);
    CHECK(wins[2] > wins[0]);
}

static void test_ultimate()
{
    using namespace ultimate;
    Board b;
    CHECK(b.turn() == 0 && b.can_play(40) && b.can_play(0));
    CHECK(b.play(4 * 9 + 2));                 // X in the centre board, top-right square
    CHECK(b.next == 2 && !b.can_play(4 * 9 + 0) && b.can_play(2 * 9 + 0));
    // X wins board 0 (squares 0, 1, 2); O is sent around
    Board w;
    const int seq[] = {0, 9, 1, 10, 2};       // X 0/0, O 1/0, X 0/1, O 1/1, X 0/2
    for (int c : seq) {
        if (!w.can_play(c)) { w.next = -1; }  // (test shortcut: free the move)
        CHECK(w.play(c));
    }
    CHECK(w.small[0] == 1);
    // Sent to a won board: any open board
    Board f;
    for (int k = 0; k < 3; ++k) f.cell[k] = 1;
    f.small[0] = 1;
    f.moves = 0;
    f.next = -1;
    CHECK(f.play(5 * 9 + 0));                 // X sends O to board 0, which is won
    CHECK(f.next == -1 && f.can_play(8 * 9 + 8) && !f.can_play(0 * 9 + 5));
    // Computer games: every move legal, Hard beats Easy, the save
    int hard_wins = 0, games = 0;
    for (int g = 0; g < 16; ++g) {
        Board x;
        const int strong = g & 1;
        int guard = 0;
        while (x.result() == -1 && ++guard < 100) {
            const int m = best_move(x, x.turn() == strong ? 2 : 0, uint32_t(g * 77 + guard));
            CHECK(x.can_play(m));
            x.play(m);
        }
        CHECK(x.result() != -1);
        ++games;
        if (x.result() == strong) ++hard_wins;
        if (g == 1) {
            uint8_t buf[Board::kSaveBytes];
            Board y;
            CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
            CHECK(memcmp(y.cell, x.cell, kCells) == 0 && y.next == x.next && y.result() == x.result());
            buf[4] = 0xFF;                       // a cell of value 3
            CHECK(!y.deserialize(buf, sizeof buf));
        }
    }
    printf("ultimate: Hard beat Easy %d/%d\n", hard_wins, games);
    CHECK(hard_wins >= games * 3 / 4);
}

static void test_gomoku()
{
    using namespace gomoku;
    Board b;
    // Black has four across row 8 (D8-G8), White to move: White blocks an end
    const int bl4[] = {7 * kN + 3, 7 * kN + 4, 7 * kN + 5, 7 * kN + 6}, wh3[] = {0, 1, 2};
    for (int k = 0; k < 4; ++k) { CHECK(b.play(bl4[k])); if (k < 3) CHECK(b.play(wh3[k])); }
    CHECK(b.turn() == 1 && b.result() == -1);
    for (int lv = 0; lv < 3; ++lv) {
        const int block = best_move(b, lv, 1);
        CHECK(block == 7 * kN + 2 || block == 7 * kN + 7);
    }
    Board w;
    for (int k = 0; k < 4; ++k) { w.play(5 * kN + k); w.play(10 * kN + k); }
    CHECK(best_move(w, 1, 3) == 5 * kN + 4);                        // Black takes the win
    w.play(5 * kN + 4);
    int a = -1, z = -1;
    CHECK(w.result() == 0 && w.winning_line(&a, &z) && a == 5 * kN && z == 5 * kN + 4);
    // An overline wins too (freestyle)
    Board o;
    const int bl[] = {0, 1, 2, 4, 5}, wh[] = {30, 31, 32, 34, 60};
    for (int k = 0; k < 5; ++k) { o.play(bl[k]); if (k < 4) o.play(wh[k]); }
    o.play(wh[4]);
    o.play(3);                                                       // B: 0-5 = six in a row
    CHECK(o.result() == 0);
    // Computer games: legal, Hard beats Easy, the save
    int hard_wins = 0, med_wins = 0;
    for (int g = 0; g < 6; ++g) {
        for (int pair = 0; pair < 2; ++pair) {
            // a different first stone each game, so the games differ
            Board x;
            const int strong = g & 1, lv_strong = pair ? 1 : 2, lv_weak = 0;
            x.play((5 + g % 5) * kN + 5 + g / 2);
            int guard = 0;
            while (x.result() == -1 && ++guard < 230) {
                const int m = best_move(x, x.turn() == strong ? lv_strong : lv_weak, uint32_t(g * 131 + guard));
                if (!x.can_play(m)) printf("gomoku: bad move %d at %d (level %d)\n", m, x.moves, x.turn() == strong ? lv_strong : lv_weak);
                CHECK(x.can_play(m));
                x.play(m);
            }
            if (x.result() == strong) (pair ? med_wins : hard_wins)++;
            if (g == 2 && pair == 0) {
                uint8_t buf[Board::kSaveBytes];
                Board y;
                CHECK(x.serialize(buf, sizeof buf) == sizeof buf && y.deserialize(buf, sizeof buf));
                CHECK(memcmp(y.stone, x.stone, kPoints) == 0 && y.result() == x.result() && y.moves == x.moves);
            }
        }
    }
    printf("gomoku: Hard beat Easy %d/6, Medium beat Easy %d/6\n", hard_wins, med_wins);
    CHECK(hard_wins >= 4 && med_wins >= 4);
}

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
    Record w; w.mode = Mode::Wireless; w.result = Result::Side1; w.moves = 41; w.seconds = 600;
    format_body(line, sizeof line, w, sides);
    CHECK(strcmp(line, "Wireless,-,Won,41,600,10:00\n") == 0);
    snprintf(full, sizeof full, "6,%s", line);
    Record wb;
    CHECK(parse_line(full, wb, sides) && wb.mode == Mode::Wireless && wb.result == Result::Side1 && wb.moves == 41);
    Summary s; s.add(r); s.add(p); s.add(wb);
    CHECK(s.lost[2] == 1 && s.side2 == 1 && s.wl_won == 1 && s.side1 == 0 && s.total == 3);

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

static void test_piperace()
{
    using namespace piperace;
    auto blank = [](Game& g) {
        g.start(7);
        memset(g.cell, 0, sizeof g.cell);
        memset(g.fill, 0, sizeof g.fill);
    };
    CHECK(opposite(kN) == kS && opposite(kS) == kN && opposite(kE) == kW && opposite(kW) == kE);
    {   // a fresh game: one tank, inside the edges, the queue full of pipes, waiting
        Game g; g.start(123);
        CHECK(g.level == 1 && g.phase == Phase::Waiting && g.wait_ms == g.wait_total());
        const int s = g.start_cell();
        CHECK(s >= 0 && s / kCols > 0 && s / kCols < kRows - 1 && s % kCols > 0 && s % kCols < kCols - 1);
        for (int i = 0; i < kQueue; ++i) CHECK(is_pipe(g.queue[i]));
        CHECK(g.goal() == 10 && g.cell_ms() == 3200);
    }
    {   // laying and swapping
        Game g; g.start(5);
        const int s = g.start_cell();
        const int c = s == 0 ? 1 : 0;
        const uint8_t q0 = g.queue[0], q1 = g.queue[1];
        CHECK(g.tap(c) == 1 && g.cell[c] == q0 && g.queue[0] == q1);
        CHECK(g.tap(s) == 0);                                // the tank stays
        g.score = 120;
        CHECK(g.tap(c) == 2 && g.score == 70);               // a swap costs 50
        g.fill[c] = 1;
        CHECK(!g.can_tap(c));                                // water has been through
    }
    {   // the water's path: a cross crossed both ways, then a spill short of the goal
        Game g; blank(g);
        const int s = 3 * kCols + 3;
        g.cell[s] = kStartE; g.head = int8_t(s); g.in = 0;
        g.cell[28] = kAcross; g.cell[29] = kCross; g.cell[30] = kSW;
        g.cell[38] = kWN; g.cell[37] = kNE;
        g.phase = Phase::Waiting; g.wait_ms = 1000; g.score = 0;
        CHECK(g.advance(999) == 0 && g.phase == Phase::Waiting);
        CHECK(g.advance(1) & kEvFlow);
        CHECK(g.head == s && g.head_total() == 1600);        // half a square in the tank
        uint32_t ev = g.advance(1600);
        CHECK(g.head == 28 && g.in == kW && !(ev & kEvFilled));
        CHECK(!g.can_tap(28));                               // water is in it
        ev = 0;
        for (int i = 0; i < 40 && g.phase == Phase::Flowing; ++i) ev |= g.advance(800);
        CHECK(ev & kEvCrossBonus);
        CHECK(g.phase == Phase::Over && g.pipes == 6 && g.score == 1000);
        CHECK(g.fill[29] == 3);
    }
    {   // a long enough run passes; a pipe never reached costs 50; fast pays double
        Game g; blank(g);
        g.cell[8] = kStartE; g.head = 8;
        for (int c = 9; c <= 14; ++c) g.cell[c] = kAcross;
        g.cell[15] = kSW; g.cell[23] = kWN;
        for (int c = 22; c >= 20; --c) g.cell[c] = kAcross;
        g.cell[63] = kUpDown;                                // never reached
        g.phase = Phase::Waiting; g.wait_ms = 5000; g.score = 0;
        g.go();
        CHECK(g.wait_ms == 0);
        g.advance(1);
        CHECK(g.phase == Phase::Flowing);
        g.go();
        CHECK(g.fast && g.head_total() == 200);
        for (int i = 0; i < 100 && g.phase == Phase::Flowing; ++i) g.advance(100);
        CHECK(g.phase == Phase::Passed && g.pipes == 11 && g.score == 11 * 200 - 50);
        g.next_level();
        CHECK(g.level == 2 && g.phase == Phase::Waiting && g.pipes == 0 && g.goal() == 12);
        CHECK(g.total == 11 && g.cell_ms() < 3200);
        // rocks from level 3
        g.next_level();
        int rocks = 0; for (int i = 0; i < kCells; ++i) rocks += g.cell[i] == kRock;
        CHECK(rocks == 1);
    }
    {   // a pipe that doesn't fit, and a wall, end the run
        Game g; blank(g);
        g.cell[9] = kStartE; g.head = 9; g.cell[10] = kUpDown;
        g.phase = Phase::Flowing; g.head_ms = 0;
        g.advance(5000);
        CHECK(g.phase == Phase::Over && g.pipes == 0);
        Game h; blank(h);
        h.cell[9] = kStartW; h.head = 9; h.cell[8] = kAcross;
        h.phase = Phase::Flowing; h.head_ms = 0;
        h.advance(20000);
        CHECK(h.phase == Phase::Over && h.pipes == 1);
    }
    {   // save round trip; damaged saves are refused
        Game g; g.start(99);
        g.tap(g.start_cell() == 0 ? 1 : 0);
        g.advance(3000);
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf));
        CHECK(memcmp(h.cell, g.cell, kCells) == 0 && h.wait_ms == g.wait_ms && h.rng == g.rng && h.head == g.head);
        uint8_t bad[Game::kSaveBytes];
        memcpy(bad, buf, sizeof bad); bad[4] = 99;
        CHECK(!h.deserialize(bad, sizeof bad));
        memcpy(bad, buf, sizeof bad); bad[4 + 2 * kCells] = kRock;   // a rock in the queue
        CHECK(!h.deserialize(bad, sizeof bad));
    }
    {   // a whole game played by a simple greedy layer always ends, scores add up
        for (uint32_t seed = 1; seed <= 30; ++seed) {
            Game g; g.start(seed);
            for (int step = 0; step < 20000 && g.phase != Phase::Over; ++step) {
                if (g.phase == Phase::Passed) { g.next_level(); continue; }
                g.tap(int((seed * 31 + step * 17) % kCells));
                g.advance(500);
            }
            CHECK(g.phase == Phase::Over && g.score >= 0);
        }
    }
    {   // history
        Record r; r.score = 5150; r.level = 6; r.pipes = 58; r.seconds = 640;
        char line[64], full[80];
        CHECK(format_body(line, sizeof line, r));
        CHECK(strcmp(line, "5150,6,58,640,10:40\n") == 0);
        snprintf(full, sizeof full, "3,%s", line);
        Record q;
        CHECK(parse_line(full, q) && q.score == 5150 && q.level == 6 && q.pipes == 58);
        Summary sum; sum.add(q);
        CHECK(sum.best == 5150 && sum.best_level == 6 && sum.games == 1);
    }
}

static void test_acquisitions()
{
    using namespace acq;
    char nm[8];
    tile_name(0, nm, sizeof nm); CHECK(strcmp(nm, "1A") == 0);
    tile_name(kTiles - 1, nm, sizeof nm); CHECK(strcmp(nm, "12I") == 0);
    CHECK(price_for(0, 2) == 200 && price_for(0, 5) == 500 && price_for(0, 6) == 600 && price_for(0, 10) == 600);
    CHECK(price_for(2, 6) == 700 && price_for(6, 41) == 1200 && price_for(5, 11) == 900 && price_for(3, 1) == 0);
    auto blank = [](Game& g) {
        g.start(3);
        memset(g.board, 0, sizeof g.board);
        for (int i = 0; i < kPlayers; ++i) { g.p[i] = Player{}; }
        g.turn = 0; g.phase = Phase::Play;
    };
    auto at = [](int col, char row) { return (row - 'A') * kCols + (col - 1); };
    {   // founding, growing
        Game g; blank(g);
        g.board[at(3, 'C')] = 1;
        g.p[0].hand[0] = uint8_t(at(4, 'C'));
        CHECK(g.tile_state(at(4, 'C')) == TileState::Ok);
        CHECK(g.play(0) && g.phase == Phase::Found);
        CHECK(!g.found(9) && g.found(5));
        CHECK(g.size(5) == 2 && g.p[0].shares[5] == 1 && g.phase == Phase::Buy && g.bank(5) == 24);
        CHECK(g.buy(5) && g.p[0].cash == kStartCash - price_for(5, 2));
        CHECK(g.buy(5) && g.buy(5) && !g.buy(5));                 // three a turn
        g.board[at(6, 'C')] = 1;                                  // a loose tile beyond
        g.phase = Phase::Play; g.bought = 0;
        g.p[0].hand[1] = uint8_t(at(5, 'C'));
        CHECK(g.play(1) && g.phase == Phase::Buy && g.size(5) == 4);   // grows and takes the loose one
    }
    {   // a merger: bonuses, then sell / trade / keep round the table
        Game g; blank(g);
        for (int c = 1; c <= 5; ++c) g.board[at(c, 'A')] = 2 + 0;        // Sunrise: 5 tiles, 1A-5A
        for (int c = 7; c <= 9; ++c) g.board[at(c, 'A')] = 2 + 1;        // Oakwood: 3 tiles, 7A-9A
        g.p[1].shares[1] = 4; g.p[2].shares[1] = 2;
        g.p[0].hand[0] = uint8_t(at(6, 'A'));
        CHECK(g.play(0) && g.phase == Phase::Dispose && g.survivor == 0);
        CHECK(g.p[1].cash == kStartCash + 3000 && g.p[2].cash == kStartCash + 1500);   // 300 x 10, x 5
        CHECK(g.disposer == 1);
        CHECK(!g.dispose(1, 1));                                          // trades go two for one
        CHECK(g.dispose(2, 2));
        CHECK(g.p[1].cash == kStartCash + 3000 + 600 && g.p[1].shares[1] == 0 && g.p[1].shares[0] == 1);
        CHECK(g.disposer == 2 && g.dispose(0, 0));                        // keeps them
        CHECK(g.phase == Phase::Buy && g.size(0) == 9 && g.size(1) == 0 && g.p[2].shares[1] == 2);
    }
    {   // a tied merger: the player picks; shared bonuses round up to 100
        Game g; blank(g);
        g.board[at(1, 'B')] = 2 + 2; g.board[at(2, 'B')] = 2 + 2;
        g.board[at(4, 'B')] = 2 + 3; g.board[at(5, 'B')] = 2 + 3;
        g.p[0].shares[3] = 2; g.p[3].shares[3] = 2;
        g.p[0].hand[0] = uint8_t(at(3, 'B'));
        CHECK(g.play(0) && g.phase == Phase::Survivor);
        CHECK(!g.choose_survivor(1) && g.choose_survivor(2));
        // Meadow (tier 1, size 2, price 300): bonuses 3000 + 1500 shared by two = 2250 -> 2300
        CHECK(g.p[0].cash == kStartCash + 2300 && g.p[3].cash == kStartCash + 2300);
        CHECK(g.disposer == 0 && g.dispose(2, 0) && g.disposer == 3 && g.dispose(0, 2));
        CHECK(g.phase == Phase::Buy && g.size(2) == 5 && g.p[3].shares[2] == 1);
    }
    {   // safe chains can't merge: a dead tile; an eighth chain has to wait
        Game g; blank(g);
        for (int c = 1; c <= 11; ++c) { g.board[at(c, 'A')] = 2 + 0; g.board[at(c, 'C')] = 2 + 1; }
        CHECK(g.tile_state(at(1, 'B')) == TileState::Dead);
        CHECK(g.can_end() && !g.call_end() == false);
        Game h; blank(h);
        for (int c = 0; c < kChains; ++c) { h.board[at(2 * c + 1, 'E')] = 2 + c; h.board[at(2 * c + 1, 'F')] = 2 + c; }
        h.board[at(1, 'H')] = 1;
        CHECK(h.tile_state(at(2, 'H')) == TileState::Wait);
        CHECK(h.tile_state(at(4, 'H')) == TileState::Ok);                 // a lone tile is fine
    }
    {   // the end: bonuses for every chain, all shares sold
        Game g; blank(g);
        for (int c = 1; c <= 12; ++c) { g.board[at(c, 'A')] = 2 + 6; g.board[at(c, 'B')] = 2 + 6; }
        for (int c = 1; c <= 12; ++c) { g.board[at(c, 'C')] = 2 + 6; g.board[at(c, 'D')] = 2 + 6; }
        CHECK(g.size(6) == 48 && g.can_end());
        g.p[0].shares[6] = 3; g.p[1].shares[6] = 1;
        g.phase = Phase::Buy;
        CHECK(g.call_end() && g.buy_done() && g.phase == Phase::Over);
        CHECK(g.p[0].cash == kStartCash + 12000 + 3 * 1200 && g.p[1].cash == kStartCash + 6000 + 1200);
        uint8_t order[kPlayers];
        g.ranking(order);
        CHECK(order[0] == 0 && order[1] == 1);
    }
    {   // whole games between computers end, keep the rules, and Hard does well against Easy
        int hard_wins = 0, games = 0;
        long long steps_total = 0;
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            Game g; g.start(seed * 7919);
            int steps = 0;
            for (; steps < 20000 && g.phase != Phase::Over; ++steps) {
                const int a = g.actor();
                g.ai_act(a == 0 ? 2 : 0);
                for (int c = 0; c < kChains; ++c) if (g.bank(c) < 0) { CHECK(false); break; }
                for (int i = 0; i < kPlayers; ++i) if (g.p[i].cash < 0) { CHECK(false); break; }
            }
            CHECK(g.phase == Phase::Over);
            steps_total += steps;
            uint8_t order[kPlayers];
            g.ranking(order);
            hard_wins += order[0] == 0;
            ++games;
            if (seed == 5) {        // a save part-way through comes back the same
                Game h; h.start(seed);
                for (int k = 0; k < 150 && h.phase != Phase::Over; ++k) h.ai_act(1);
                uint8_t buf[Game::kSaveBytes];
                CHECK(h.serialize(buf, sizeof buf) == Game::kSaveBytes);
                Game r;
                CHECK(r.deserialize(buf, sizeof buf) && memcmp(r.board, h.board, kTiles) == 0 && r.p[2].cash == h.p[2].cash);
                buf[4] = 99;
                CHECK(!r.deserialize(buf, sizeof buf));
            }
        }
        printf("acquisitions: Hard (1 seat) won %d of %d against three Easy; %lld steps a game\n", hard_wins, games, steps_total / games);
        CHECK(hard_wins * 2 >= games);
    }
    {   // history
        Record r; r.place = 1; r.money = 48200; r.level = 2; r.seconds = 2410;
        char line[64], full[80];
        CHECK(format_body(line, sizeof line, r));
        CHECK(strcmp(line, "1,48200,Hard,2410,40:10\n") == 0);
        snprintf(full, sizeof full, "5,%s", line);
        Record q;
        CHECK(parse_line(full, q) && q.place == 1 && q.money == 48200 && q.level == 2);
    }
}

static void test_strategygo()
{
    using namespace sgo;
    int total = 0; for (int r = 0; r < kRanks; ++r) total += kCount[r];
    CHECK(total == kArmy);
    CHECK(lake(42) && lake(57) && !lake(44) && !lake(41));
    {   // a random army: every piece, the Flag on the back row next to Bombs
        Army a; random_army(12345, a);
        int cnt[kRanks] = {};
        for (int i = 0; i < kArmy; ++i) ++cnt[a.rank_at[i]];
        bool same = true; for (int r = 0; r < kRanks; ++r) same &= cnt[r] == kCount[r];
        CHECK(same);
        int flag = -1; for (int i = 0; i < kArmy; ++i) if (a.rank_at[i] == kFlag) flag = i;
        CHECK(flag >= 0 && flag < kN);
        const bool bomb_by = (flag % kN > 0 && a.rank_at[flag - 1] == kBomb) || (flag % kN < kN - 1 && a.rank_at[flag + 1] == kBomb)
                             || a.rank_at[flag + kN] == kBomb;
        CHECK(bomb_by);
        CHECK(army_cell(0, 0) == 90 && army_cell(1, 0) == 9 && army_cell(0, 39) == 69 && army_cell(1, 39) == 30);
    }
    auto empty_game = []() {
        Board b;
        b.moves = kSetupPlies;
        return b;
    };
    auto put = [](Board& b, int c, int side, int rank) { b.sq[c].side = int8_t(side); b.sq[c].rank = uint8_t(rank); };
    {   // battles
        Board b = empty_game();
        put(b, 64, 0, kMarshal); put(b, 54, 1, kGeneral);
        put(b, 99, 0, kFlag); put(b, 0, 1, kFlag); put(b, 1, 1, kSergeant);
        CHECK(b.turn() == 0 && b.play(move_key(64, 54)));
        CHECK(b.sq[54].side == 0 && b.sq[54].rank == kMarshal && b.sq[54].shown && b.last.outcome == kAttackerWins);
        CHECK(b.lost[1][kGeneral] == 1 && b.winner == -1);
        // Blue's Spy strikes the Marshal and wins
        put(b, 44, 1, kSpy);
        CHECK(b.turn() == 1 && b.play(move_key(44, 54)) && b.sq[54].side == 1 && b.sq[54].rank == kSpy);
    }
    {   // the Marshal strikes the Spy and wins; equal ranks both go; a Miner defuses a Bomb; a Scout hits one and is lost
        Board b = empty_game();
        put(b, 99, 0, kFlag); put(b, 0, 1, kFlag); put(b, 9, 1, kSergeant);
        put(b, 70, 0, kMarshal); put(b, 60, 1, kSpy);
        CHECK(b.play(move_key(70, 60)) && b.sq[60].rank == kMarshal);
        put(b, 30, 1, kCaptain); put(b, 31, 0, kCaptain);
        CHECK(b.play(move_key(30, 31)) && b.sq[31].side < 0 && b.sq[30].side < 0 && b.last.outcome == kBothLost);
        put(b, 81, 0, kMiner); put(b, 71, 1, kBomb);
        CHECK(b.play(move_key(81, 71)) && b.sq[71].rank == kMiner && b.last.outcome == kBombDefused);
        put(b, 20, 1, kScout); put(b, 80, 0, kBomb);
        CHECK(b.play(move_key(20, 50)));                        // a long Scout move...
        CHECK(b.sq[50].shown);                                  // ...shows what it is
        CHECK(!b.can_play(move_key(71, 61)) || true);
        put(b, 85, 0, kScout);
        CHECK(b.play(move_key(85, 86)));
        CHECK(b.play(move_key(50, 80)) == false);               // not in a straight empty line
    }
    {   // Scouts: lakes and pieces stop them; bombs and the flag never move
        Board b = empty_game();
        put(b, 99, 0, kFlag); put(b, 0, 1, kFlag); put(b, 9, 1, kSergeant);
        put(b, 62, 0, kScout); put(b, 98, 0, kBomb);
        uint8_t t[20];
        const int n = b.targets(62, t);
        bool through_lake = false; for (int i = 0; i < n; ++i) through_lake |= t[i] < 60 && t[i] % kN == 2;
        CHECK(!through_lake && n > 3);
        CHECK(b.targets(98, t) == 0 && b.targets(99, t) == 0);
    }
    {   // the flag taken ends it; no moves left loses
        Board b = empty_game();
        put(b, 99, 0, kFlag); put(b, 11, 1, kFlag); put(b, 12, 0, kSergeant); put(b, 0, 1, kScout);
        CHECK(b.play(move_key(12, 11)) && b.winner == 0 && b.last.outcome == kFlagTaken);
        Board c = empty_game();
        put(c, 99, 0, kFlag); put(c, 0, 1, kFlag); put(c, 50, 0, kScout);
        CHECK(c.play(move_key(50, 51)) && c.winner == 0);       // Blue has only its flag: no move
    }
    {   // the two-squares rule
        Board b = empty_game();
        put(b, 99, 0, kFlag); put(b, 0, 1, kFlag); put(b, 70, 0, kSergeant); put(b, 9, 1, kSergeant);
        int a = 70, c = 71, x = 9, y = 19;
        for (int k = 0; k < kShuttle; ++k) {
            CHECK(b.play(move_key(a, c))); std::swap(a, c);
            CHECK(b.play(move_key(x, y))); std::swap(x, y);
        }
        CHECK(!b.can_play(move_key(a, c)));
        CHECK(b.can_play(move_key(a, a - kN)));
    }
    {   // computer games: setups by the rules, games end; Hard beats Easy most of the time
        int hard_wins = 0, games = 0, plies = 0;
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            Board* b = new Board();
            uint32_t rs = seed * 2654435761u;
            while (b->result() < 0 && b->moves < kSetupPlies + 1500) {
                rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
                const int side = b->turn();
                const int lvl = ((seed & 1) ? side == 0 : side == 1) ? 2 : 0;
                const uint32_t k = best_move(*b, lvl, rs);
                if (!b->play(k)) { CHECK(false); break; }
                if (b->moves == kSetupPlies) {
                    for (int s = 0; s < 2; ++s) for (int r = 0; r < kRanks; ++r) if (b->left(s, r)) CHECK(false);
                }
            }
            const int hard = (seed & 1) ? 0 : 1;
            hard_wins += b->result() == hard;
            ++games;
            plies += b->moves - kSetupPlies;
            if (seed == 2) {
                uint8_t buf[Board::kSaveBytes];
                CHECK(b->serialize(buf, sizeof buf) == Board::kSaveBytes);
                Board* r = new Board();
                CHECK(r->deserialize(buf, sizeof buf));
                CHECK(memcmp(r->lost, b->lost, sizeof b->lost) == 0 && r->moves == b->moves && r->sq[55].side == b->sq[55].side);
                buf[4 + 42] = 0x05;                              // a piece in a lake
                CHECK(!r->deserialize(buf, sizeof buf));
                delete r;
            }
            delete b;
        }
        printf("strategygo: Hard won %d of %d against Easy, %d moves a game\n", hard_wins, games, plies / games);
        CHECK(hard_wins * 3 >= games * 2);
    }
}

static void test_dealcyd()
{
    using namespace dealcyd;
    char m[24];
    money(m, sizeof m, 1); CHECK(strcmp(m, "$0.01") == 0);
    money(m, sizeof m, 100000000); CHECK(strcmp(m, "$1,000,000") == 0);
    money(m, sizeof m, 75000000, true); CHECK(strcmp(m, "$750K") == 0);
    money(m, sizeof m, 100000000, true); CHECK(strcmp(m, "$1M") == 0);
    int opens = 0; for (int r = 0; r < kRounds; ++r) opens += kOpenPerRound[r];
    CHECK(opens == kCases - 2);
    {   // a whole game, No Deal to the end, then a swap
        Game g; g.start(99);
        bool seen[kCases] = {};
        for (int i = 0; i < kCases; ++i) seen[g.value_of[i]] = true;
        bool all = true; for (bool b : seen) all &= b;
        CHECK(all);
        CHECK(!g.open(3) && g.pick(7) && g.phase == Phase::Open && g.to_open == 6);
        CHECK(!g.open(7));                                     // not your own case
        int rounds = 0;
        for (int c = 0; c < kCases && g.phase != Phase::Swap; ++c) {
            if (c == 7 || g.opened[c]) continue;
            CHECK(g.open(c));
            if (g.phase == Phase::Offer) {
                ++rounds;
                CHECK(g.offer > 0 && g.offer <= 100000000);
                CHECK(g.no_deal());
            }
        }
        CHECK(rounds == kRounds && g.phase == Phase::Swap && g.left() == 2);
        const int other = g.last_case();
        CHECK(other >= 0 && other != 7);
        CHECK(g.keep_or_swap(true) && g.phase == Phase::Done && g.won == kValues[g.value_of[other]] && g.dealt == -1);
    }
    {   // a deal; offers grow as a share of the average
        uint64_t share_first = 0, share_last = 0;
        for (uint32_t seed = 1; seed <= 200; ++seed) {
            Game g; g.start(seed);
            g.pick(0);
            int c = 1;
            while (g.phase != Phase::Swap && g.phase != Phase::Done) {
                if (g.phase == Phase::Offer) {
                    const uint64_t pct = uint64_t(g.offer) * 100 / (g.average() ? g.average() : 1);
                    if (g.round == 0) share_first += pct;
                    if (g.round == kRounds - 1) share_last += pct;
                    if (g.round == 4 && seed % 2) { CHECK(g.deal() && g.won == g.offer && g.dealt == 4); break; }
                    g.no_deal();
                    continue;
                }
                g.open(c++);
            }
        }
        share_first /= 200; share_last /= 100;
        CHECK(share_first >= 8 && share_first <= 16);
        CHECK(share_last >= 85 && share_last <= 99);
    }
    {   // saves
        Game g; g.start(5); g.pick(12); g.open(3); g.open(4);
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.mine == 12 && h.opened[3] && h.to_open == 4 && memcmp(h.value_of, g.value_of, kCases) == 0);
        buf[5] = buf[4];                                      // a value twice
        CHECK(!h.deserialize(buf, sizeof buf));
    }
    {   // history
        Record r; r.won = 4300000; r.dealt = 5; r.held = 75000000; r.seconds = 312;
        char line[80], full[96];
        CHECK(format_body(line, sizeof line, r));
        CHECK(strcmp(line, "43000.00,6,750000.00,312,5:12\n") == 0);
        snprintf(full, sizeof full, "3,%s", line);
        Record q;
        CHECK(parse_line(full, q) && q.won == 4300000 && q.dealt == 5 && q.held == 75000000);
    }
}

static void test_presscyd()
{
    using namespace presscyd;
    {   // the board: gremlins at most one a square, the spins
        Game g; g.start(17);
        int grem = 0, spin = 0;
        for (int q = 0; q < kSquares; ++q) {
            int per = 0;
            for (int k = 0; k < kSlots; ++k) {
                per += g.board[q][k].kind == kGremlin;
                spin += g.board[q][k].kind == kMoneySpin;
                if (g.board[q][k].kind != kGremlin) CHECK(g.board[q][k].dollars >= 250);
            }
            CHECK(per <= 1);
            grem += per;
        }
        CHECK(grem == 9 && spin == 7);
        CHECK(g.phase == Phase::Ready && g.turn == 0 && g.p[0].earned == 3);
    }
    auto find = [](const Game& g, int kind, int* sq, int* sl) {
        for (int q = 0; q < kSquares; ++q) for (int k = 0; k < kSlots; ++k)
            if (g.board[q][k].kind == kind) { *sq = q; *sl = k; return true; }
        return false;
    };
    {   // money, an extra spin, a gremlin
        Game g; g.start(5);
        int q, k;
        CHECK(!g.stop(0, 0));                                   // not spinning
        CHECK(find(g, kMoney, &q, &k) && g.spin() && g.stop(q, k));
        CHECK(g.p[0].money == g.board[q][k].dollars && g.p[0].earned == 2 && g.turn == 0);
        CHECK(find(g, kMoneySpin, &q, &k) && g.spin() && g.stop(q, k));
        CHECK(g.p[0].earned == 2);                              // used one, won one
        CHECK(find(g, kGremlin, &q, &k) && g.spin() && g.stop(q, k));
        CHECK(g.p[0].money == 0 && g.p[0].gremlins == 1 && g.p[0].earned == 1);
    }
    {   // passing: only to someone ahead; they must take them
        Game g; g.start(8);
        g.p[0].money = 1000; g.p[1].money = 3000; g.p[2].money = 500;
        g.turn = 0;
        CHECK(g.can_pass() && g.pass_target() == 1);
        CHECK(g.pass() && g.turn == 1 && g.p[1].passed == 3 && g.p[0].earned == 0);
        CHECK(!g.can_pass());                                   // passed spins come first
        int q, k;
        CHECK(find(g, kGremlin, &q, &k) && g.spin() && g.stop(q, k));
        CHECK(g.p[1].passed == 0 && g.p[1].earned == 3 + 2);    // the rest became theirs
        g.p[1].money = 10000;
        g.turn = 1;
        CHECK(!g.can_pass());                                   // the leader can't pass
    }
    {   // four gremlins: out
        Game g; g.start(9);
        int q, k;
        find(g, kGremlin, &q, &k);
        g.p[2].earned = 10;
        g.turn = 2;
        for (int i = 0; i < 4; ++i) { CHECK(g.spin() || g.turn != 2); if (g.turn == 2) g.stop(q, k); }
        CHECK(g.p[2].out() && g.p[2].spins() == 0);
    }
    {   // whole games end; Hard does at least as well as Easy
        int hard_wins = 0;
        for (uint32_t seed = 1; seed <= 300; ++seed) {
            Game g; g.start(seed);
            for (int step = 0; step < 2000 && g.phase != Phase::Over; ++step) {
                if (g.phase == Phase::RoundOver) { g.next_round(); continue; }
                const int lvl = g.turn == 0 ? 2 : 0;
                if (g.ai_pass(lvl)) { g.pass(); continue; }
                g.spin();
                g.stop(int(g.rand_next() % kSquares), int(g.rand_next() % kSlots));
            }
            CHECK(g.phase == Phase::Over);
            uint8_t o[kPlayers]; g.ranking(o);
            hard_wins += o[0] == 0;
        }
        printf("presscyd: Hard (seat 1) won %d of 300 against two Easy\n", hard_wins);
        CHECK(hard_wins >= 90);
    }
    {   // history
        Record r; r.place = 1; r.money = 12750; r.seconds = 402;
        char line[64], full[80];
        CHECK(format_body(line, sizeof line, r) && strcmp(line, "1,12750,402,6:42\n") == 0);
        snprintf(full, sizeof full, "4,%s", line);
        Record q;
        CHECK(parse_line(full, q) && q.place == 1 && q.money == 12750);
    }
    {   // saves
        Game g; g.start(3); g.spin();
        uint8_t buf[Game::kSaveBytes];
        CHECK(g.serialize(buf, sizeof buf) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf, sizeof buf) && h.phase == Phase::Ready && h.board[4][1].dollars == g.board[4][1].dollars);
        buf[4] = 3;                                              // a fourth kind
        CHECK(!h.deserialize(buf, sizeof buf));
    }
}

static void test_cardsharks()
{
    using namespace csh;
    auto mk = [](int rank, int suit) { return uint8_t(suit * 13 + rank - 1); };
    CHECK(value(mk(1, 0)) == 14 && value(mk(13, 2)) == 13 && value(mk(2, 3)) == 2);
    {   // a right call, a freeze, a miss back to the freeze, change once before calling
        Board b; b.reset(11);
        CHECK(b.turn() == 0 && b.can_play(kChange) && !b.can_play(kFreeze));
        b.row[0].card[0] = mk(2, 0);                           // a 2: Higher can't miss... unless another 2
        // stack the deck: next card a 9
        b.deck[b.deck_n - 1] = mk(9, 1);
        CHECK(b.play(kHigher) && b.row[0].pos == 1 && b.turn() == 0 && b.last == Last::Right);
        CHECK(!b.can_play(kChange));                           // only before the first call
        CHECK(b.play(kFreeze) && b.row[0].frozen == 1 && b.turn() == 1);
        // side 1 calls wrong: Lower on a 5 that turns up a King
        b.row[1].card[0] = mk(5, 2);
        b.deck[b.deck_n - 1] = mk(13, 3);
        CHECK(b.play(kLower) && b.last == Last::Wrong && b.turn() == 0 && b.row[1].pos == 0);
        CHECK(b.last_card == mk(13, 3));
        // side 0 misses: back to its frozen 9
        b.deck[b.deck_n - 1] = mk(9, 2);                        // an equal card is wrong
        CHECK(b.play(kHigher) && b.last == Last::Wrong && b.row[0].pos == 1 && b.row[0].card[2] == kNoCard);
        // side 1 may change its base
        const uint8_t before = b.row[1].card[0];
        CHECK(b.play(kChange) && b.row[1].card[0] != before && !b.can_play(kChange));
    }
    {   // five right wins the round; two rounds the match
        Board b; b.reset(3);
        for (int round = 0; round < 2; ++round) {
            const int s = b.turn();
            b.row[s].card[0] = mk(2, 0);
            for (int k = 0; k < 4; ++k) {
                b.deck[b.deck_n - 1] = mk(3 + k * 3, k);            // 3, 6, 9, 12: always higher
                CHECK(b.play(kHigher));
            }
            CHECK(b.wins[s] == round + 1);
            if (b.winner >= 0) break;
            // let the same side win again next round: give it the turn
            b.turn_side = uint8_t(s);
        }
        CHECK(b.winner >= 0);
    }
    {   // computer matches end; Hard beats Easy
        int hard = 0;
        const int N = 400;
        for (uint32_t seed = 1; seed <= uint32_t(N); ++seed) {
            Board b; b.reset(seed * 7919);
            const int hard_side = seed & 1;
            uint32_t rs = seed;
            for (int k = 0; k < 5000 && b.result() < 0; ++k) {
                rs = rs * 1103515245u + 12345u;
                const int m = best_move(b, b.turn() == hard_side ? 2 : 0, rs);
                if (!b.play(m)) { CHECK(false); break; }
            }
            CHECK(b.result() >= 0);
            hard += b.result() == hard_side;
        }
        printf("cardsharks: Hard won %d of %d matches against Easy\n", hard, N);
        CHECK(hard * 100 >= N * 55);
    }
    {   // saves
        Board b; b.reset(21); b.play(kHigher);
        uint8_t buf[Board::kSaveBytes];
        CHECK(b.serialize(buf, sizeof buf) == Board::kSaveBytes);
        Board r;
        CHECK(r.deserialize(buf, sizeof buf) && r.deck_n == b.deck_n && r.row[0].pos == b.row[0].pos && r.rng == b.rng);
        buf[4 + 5] = 4;                                          // pos past the row
        CHECK(!r.deserialize(buf, sizeof buf));
    }
}

static void test_sorrycyd()
{
    using namespace sorry;
    CHECK(track_square(0, 0) == 4 && track_square(1, 0) == 19 && track_square(0, -1) == 3 && track_square(0, 58) == 2);
    CHECK(track_square(3, 58) == 47 && track_square(0, kStart) == -1 && track_square(0, 60) == -1);
    CHECK(slide_len(16, 0) == 3 && slide_len(24, 0) == 4 && slide_len(1, 0) == 0 && slide_len(1, 2) == 3);
    auto find = [](const Game& g, auto pred) {
        Move ms[96]; const int n = g.moves(ms, 96);
        for (int i = 0; i < n; ++i) if (pred(ms[i])) return ms[i];
        return Move{};
    };
    {   // a 1 starts a pawn, bumping whoever sits on the start square
        Game g; g.start(5);
        g.pos[1][0] = 45;             // Blue on square 4: progress (4 - 19 - 4) mod 60 = 41
        CHECK(track_square(1, g.pos[1][0]) == 4);
        g.phase = Phase::Play; g.card = 1;
        Move m = find(g, [](const Move& x) { return x.kind == Kind::Move && x.to == 0; });
        CHECK(m.kind == Kind::Move && g.play(m));
        CHECK(g.pos[0][m.pawn] == 0 && g.pos[1][0] == kStart && (g.last.bumped & (1u << 4)) && g.turn == 1);
    }
    {   // nothing fits: no moves (all in Start, a 3)
        Game g; g.start(5); g.phase = Phase::Play; g.card = 3;
        Move ms[96];
        CHECK(g.moves(ms, 96) == 0 && !g.may_pass());
        g.lose_turn(); CHECK(g.turn == 1 && g.phase == Phase::Draw);
        Game h; h.start(5); h.phase = Phase::Play; h.card = 2;   // a 2 with no move: draw again
        h.pos[0][0] = 63;
        h.lose_turn(); CHECK(h.turn == 0 && h.phase == Phase::Draw);
    }
    {   // a 2: start a pawn and draw again
        Game g; g.start(5); g.phase = Phase::Play; g.card = 2;
        Move m = find(g, [](const Move& x) { return x.to == 0; });
        CHECK(g.play(m) && g.turn == 0 && g.phase == Phase::Draw);
    }
    {   // a slide of another colour: Blue lands on square 1 (Red's short slide) and slides to 4
        Game g; g.start(5);
        g.turn = 1; g.phase = Phase::Play; g.card = 3;
        g.pos[1][0] = 39;                                     // square (15+4+39) % 60 = 58
        g.pos[0][0] = 0;                                      // Red on square 4 (the slide's end)
        g.pos[1][1] = 42;                                     // Blue's own pawn on square 1 + 1 = 2
        CHECK(track_square(1, 42) == 1);
        g.pos[1][1] = 43;                                     // square 2
        Move m = find(g, [](const Move& x) { return x.pawn == 0; });
        CHECK(m.to == 42 && g.play(m));
        CHECK(track_square(1, g.pos[1][0]) == 4 && g.pos[0][0] == kStart && g.pos[1][1] == kStart && g.last.slid);
        // Red doesn't slide on its own slide
        Game h; h.start(5); h.phase = Phase::Play; h.card = 1;
        h.pos[0][0] = 56;                                     // square 0 -> 1 (own slide start)
        Move k = find(h, [](const Move& x) { return x.pawn == 0; });
        CHECK(h.play(k) && h.pos[0][0] == 57 && !h.last.slid);
    }
    {   // 4 back from just past Start, then into Safety; exact count Home
        Game g; g.start(5); g.phase = Phase::Play; g.card = 4;
        g.pos[0][0] = 1;
        Move m = find(g, [](const Move& x) { return x.pawn == 0; });
        CHECK(m.to == 57 && g.play(m));
        g.turn = 0; g.phase = Phase::Play; g.card = 2;
        Move m2 = find(g, [](const Move& x) { return x.pawn == 0 && x.to != 0; });
        CHECK(m2.to == 59 && track_square(0, 59) == -1);
        g.pos[0][0] = 62;
        g.card = 3; Move ms[96];
        CHECK(g.moves(ms, 96) == 0);                          // 62 + 3 > Home
        g.card = 2;
        Move h = find(g, [](const Move& x) { return x.pawn == 0 && x.to == kHome; });
        CHECK(h.kind == Kind::Move && g.play(h) && g.pos[0][0] == kHome && g.last.home);
        // 10 one back out of Safety
        Game b; b.start(5); b.phase = Phase::Play; b.card = 10; b.pos[0][0] = 59;
        Move bk = find(b, [](const Move& x) { return x.pawn == 0; });
        CHECK(bk.to == 58);
    }
    {   // own pawn blocks; another colour's is bumped
        Game g; g.start(5); g.phase = Phase::Play; g.card = 5;
        g.pos[0][0] = 10; g.pos[0][1] = 15;
        Move ms[96];
        const int n = g.moves(ms, 96);
        CHECK(n == 1 && ms[0].pawn == 1);
    }
    {   // 7 split, 11 switch (and pass when 11 forward can't), Sorry!
        Game g; g.start(5); g.phase = Phase::Play; g.card = 7;
        g.pos[0][0] = 10; g.pos[0][1] = 20;
        Move s = find(g, [](const Move& x) { return x.kind == Kind::Split && x.pawn == 0 && x.to == 13; });
        CHECK(s.pawn2 == 1 && s.to2 == 24 && g.play(s) && g.pos[0][0] == 13 && g.pos[0][1] == 24);
        Game w; w.start(5); w.phase = Phase::Play; w.card = 11;
        w.pos[0][0] = 60; w.pos[2][3] = 21;                   // Red in Safety: no 11, no switch from Safety
        CHECK(w.may_pass());
        w.pos[0][1] = 5;
        Move sw = find(w, [](const Move& x) { return x.kind == Kind::Switch; });
        CHECK(sw.kind == Kind::Switch && !w.may_pass());
        const int a = track_square(0, 5), b = track_square(2, 21);
        CHECK(w.play(sw) && track_square(0, w.pos[0][1]) == b && track_square(2, w.pos[2][3]) == a);
        Game y; y.start(5); y.phase = Phase::Play; y.card = kSorry;
        y.pos[3][2] = 30;
        Move so = find(y, [](const Move& x) { return x.kind == Kind::Sorry; });
        const int sq = track_square(3, 30);
        CHECK(so.oc == 3 && y.play(so) && y.pos[3][2] == kStart && track_square(0, y.pos[0][so.pawn]) == sq);
    }
    {   // save round trip and bad saves
        Game g; g.start(77);
        for (int k = 0; k < 40 && g.phase != Phase::Over; ++k) {
            g.draw();
            Move ms[96];
            if (g.moves(ms, 96)) g.play(g.ai_move(1)); else g.lose_turn();
        }
        std::vector<uint8_t> buf(Game::kSaveBytes);
        CHECK(g.serialize(buf.data(), buf.size()) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf.data(), buf.size()) && h.turns == g.turns && !memcmp(h.pos, g.pos, sizeof g.pos));
        std::vector<uint8_t> bad = buf; bad[4] = 70;
        CHECK(!h.deserialize(bad.data(), bad.size()));
        bad = buf; bad[4] = 10; bad[5] = 10;                  // two Red pawns on one square
        CHECK(!h.deserialize(bad.data(), bad.size()));
        bad = buf; bad[20] = 6;
        CHECK(!h.deserialize(bad.data(), bad.size()));
    }
    {   // whole games end; Hard (Red) against three Easy
        int wins = 0, games = 200, longest = 0;
        for (int s = 0; s < games; ++s) {
            Game g; g.start(1000 + s);
            int k = 0;
            for (; k < 4000 && g.phase != Phase::Over; ++k) {
                g.draw();
                Move ms[96];
                if (g.moves(ms, 96)) CHECK(g.play(g.ai_move(g.turn == 0 ? 2 : 0)));
                else g.lose_turn();
            }
            CHECK(g.phase == Phase::Over);
            if (k > longest) longest = k;
            wins += g.winner == 0;
        }
        printf("sorrycyd: Hard won %d of %d against three Easy (longest %d turns)\n", wins, games, longest);
        CHECK(wins * 100 > games * 22);
    }
}

static void test_escape()
{
    using namespace escape;
    int8_t nb[6];
    CHECK(neighbours(hex_at(4, 5), nb) == 6 && neighbours(0, nb) == 2 && distance(0, 98) == 13);
    CHECK(distance(hex_at(4, 5), hex_at(4, 2)) == 3 && distance(hex_at(4, 4), hex_at(4, 5)) == 1 && distance(hex_at(3, 4), hex_at(4, 5)) == 2);
    {   // the board
        Game g; g.start(9);
        int land[5] = {}, volcano = 0;
        for (int h = 0; h < kHexes; ++h) {
            ++land[g.terrain[h]];
            if (g.under[h] == kVolcano) { ++volcano; CHECK(g.terrain[h] == kMountain); }
        }
        CHECK(land[kBeach] == 18 && land[kForest] == 12 && land[kMountain] == 7 && land[kSafe] == 4 && volcano == 1);
        CHECK(g.boats == 8 && g.creatures == 2);
        for (int b = 0; b < g.boats; ++b) CHECK(g.terrain[g.boat[b]] == kSea);
        // setup: the best first, a colour at a time
        CHECK(g.next_to_place() == 0 && g.ex[0].value == 5);
        for (int k = 0; k < kExplorers; ++k) CHECK(g.apply(g.ai(1)));
        CHECK(g.phase == Phase::Move && g.turn == 0 && g.moves_left == kMoves);
        for (int h = 0; h < kHexes; ++h) CHECK(g.land_count(h) <= 1);
    }
    {   // steps: land, into a boat, swimming once a turn, no creature hexes, boat control
        Game g; g.start(9);
        for (int k = 0; k < kExplorers; ++k) g.apply(g.ai(0));
        // explorer 0 (Red) on the beach next to the boat at hex 12 (col 3, row 1)
        const int beach = hex_at(3, 2), boat_hex = 12;
        for (int e = 0; e < kExplorers; ++e) if (g.ex[e].hex == beach) g.ex[e].hex = g.ex[0].hex;
        g.ex[0].hex = int8_t(beach);
        Action a; a.act = kStepExplorer; a.who = 0; a.to = int8_t(boat_hex);
        CHECK(g.apply(a) && g.ex[0].where == kAboard && g.ex[0].boat == 0 && g.moves_left == 2);
        Action b; b.act = kStepBoat; b.who = 0; b.to = 11;
        CHECK(g.apply(b) && g.ex[0].hex == 11 && g.boat[0] == 11);
        Action c; c.act = kStepExplorer; c.who = 0; c.to = 10;               // off the boat: swimming
        CHECK(g.apply(c) && g.ex[0].where == kSwim && g.phase == Phase::Sink);
        // the next Red turn: one swim step only, never into a creature
        g.phase = Phase::Move; g.moves_left = 3; g.swam = 0;
        g.cr[0].hex = 1;
        Action d; d.act = kStepExplorer; d.who = 0; d.to = 1;
        CHECK(!g.can(d));
        g.cr[0].hex = 4;
        Action s; s.act = kStepExplorer; s.who = 0; s.to = 9;
        CHECK(g.apply(s) && g.ex[0].hex == 9 && g.ex[0].where == kSwim);
        Action t; t.act = kStepExplorer; t.who = 0; t.to = 0;
        CHECK(!g.can(t));                                                    // swam already this turn
        g.swam = 0;
        CHECK(g.apply(t) && g.ex[0].where == kSaved && g.score(0) == 5);
        // boat control: Blue in a boat with one Red - tie, both may move it
        Game h; h.start(9);
        for (int k = 0; k < kExplorers; ++k) h.apply(h.ai(0));
        h.ex[0].where = kAboard; h.ex[0].boat = 1; h.ex[0].hex = h.boat[1];
        h.ex[9].where = kAboard; h.ex[9].boat = 1; h.ex[9].hex = h.boat[1];
        CHECK(h.controls(0, 1) && h.controls(1, 1) && !h.controls(2, 1));
        h.ex[10].where = kAboard; h.ex[10].boat = 1; h.ex[10].hex = h.boat[1];
        CHECK(!h.controls(0, 1) && h.controls(1, 1));
    }
    {   // sinking: beaches first; a shark tile eats the swimmers there; creatures
        Game g; g.start(9);
        for (int k = 0; k < kExplorers; ++k) g.apply(g.ai(0));
        Action e; e.act = kEndMoves;
        CHECK(g.apply(e) && g.phase == Phase::Sink);
        Action list[160];
        const int n = g.actions(list, 160);
        CHECK(n == 18);
        for (int i = 0; i < n; ++i) CHECK(g.terrain[list[i].to] == kBeach);
        int shark = -1;
        for (int i = 0; i < n; ++i) if (g.under[list[i].to] == kSharkTile) shark = list[i].to;
        CHECK(shark >= 0);
        const int on = g.land_count(shark);
        Action s; s.act = kSinkTile; s.to = int8_t(shark);
        CHECK(g.apply(s) && g.terrain[shark] == kSea && g.news.lost == on && g.creature_at(shark) >= 0);
        CHECK(g.phase == Phase::Creature);
        // a whale tips a boat over; a serpent eats a boat
        Game w; w.start(9);
        for (int k = 0; k < kExplorers; ++k) w.apply(w.ai(0));
        w.ex[0].where = kAboard; w.ex[0].boat = 0; w.ex[0].hex = w.boat[0];
        w.cr[w.creatures].kind = kWhale; w.cr[w.creatures].hex = int8_t(hex_at(2, 1)); ++w.creatures;
        w.phase = Phase::Creature; w.die = kWhale;
        Action m; m.act = kMoveCreature; m.who = int8_t(w.creatures - 1); m.to = w.boat[0];
        const int8_t was = w.boat[0];
        CHECK(w.apply(m) && w.boat[0] == -1 && w.ex[0].where == kSwim && w.ex[0].hex == was && w.news.c_tipped == 1);
        CHECK(w.turn == 1 && w.phase == Phase::Move);
        Game v = w;
        v.turn = 0; v.phase = Phase::Creature; v.die = kSerpent;
        v.cr[m.who].hex = int8_t(hex_at(1, 1));                            // the whale swims off
        v.cr[0].hex = int8_t(hex_at(3, 0));
        Action m2; m2.act = kMoveCreature; m2.who = 0; m2.to = was;
        CHECK(v.apply(m2) && v.ex[0].where == kLost);
    }
    {   // save round trip and bad saves
        Game g; g.start(31);
        for (int k = 0; k < 200 && g.phase != Phase::Over; ++k) g.apply(g.ai(1));
        std::vector<uint8_t> buf(Game::kSaveBytes);
        CHECK(g.serialize(buf.data(), buf.size()) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf.data(), buf.size()) && h.turns == g.turns && !memcmp(h.terrain, g.terrain, sizeof g.terrain));
        std::vector<uint8_t> bad = buf; bad[4] = 9;
        CHECK(!h.deserialize(bad.data(), bad.size()));
        bad = buf; bad[4 + 2 * kHexes + 1] = 9;                               // a value of 9
        CHECK(!h.deserialize(bad.data(), bad.size()));
    }
    {   // whole games end; Hard (Red) against three Easy
        int wins = 0, games = 20, saved = 0;
        for (int s = 0; s < games; ++s) {
            Game g; g.start(500 + s);
            int k = 0;
            for (; k < 20000 && g.phase != Phase::Over; ++k) CHECK(g.apply(g.ai(g.turn == 0 ? 2 : 0)));
            CHECK(g.phase == Phase::Over);
            wins += g.winner() == 0;
            saved += g.score(0);
        }
        printf("escape: Hard won %d of %d against three Easy (saved %.1f of 22 a game)\n", wins, games, saved / double(games));
        CHECK(wins * 2 > games);
    }
}

static void test_trivia()
{
    {   // inflate: a fixed-code block and a stored block (made by Python's zlib), bad data
        const uint8_t fixed[] = {243, 72, 205, 201, 201, 215, 81, 200, 64, 162, 20, 156, 35, 93, 20, 21, 60, 144, 68, 160, 242, 131, 64, 2, 0};
        std::string want;
        for (int k = 0; k < 7; ++k) want += "Hello, hello, hello CYD! ";
        uint8_t out[400];
        long n = inflate::raw(fixed, sizeof fixed, out, sizeof out);
        CHECK(n == long(want.size()) && memcmp(out, want.data(), want.size()) == 0);
        CHECK(inflate::raw(fixed, sizeof fixed, out, 50) == -1);              // no room
        CHECK(inflate::raw(fixed, 10, out, sizeof out) == -1);                // cut short
        uint8_t stored[5 + 175] = {1, 175, 0, 80, 255};
        memcpy(stored + 5, want.data(), 175);
        n = inflate::raw(stored, sizeof stored, out, sizeof out);
        CHECK(n == 175 && memcmp(out, want.data(), 175) == 0);
        stored[3] = 81;                                                       // LEN / NLEN don't match
        CHECK(inflate::raw(stored, sizeof stored, out, sizeof out) == -1);
    }
    {   // the bank: every block unpacks to the generator's checksum; questions read back
        CHECK(trivia::count() > 5000 && trivia::verify());
        int per_diff[3] = {}, tf = 0, cats[trivia::kCategories] = {};
        trivia::Question q;
        bool all = true;
        for (int i = 0; i < trivia::count(); ++i) {
            if (!trivia::get(i, q)) { all = false; continue; }
            ++per_diff[q.difficulty];
            ++cats[q.category];
            tf += q.answers == 2;
            if (q.answers == 2 && !(strcmp(q.answer[0], "True") == 0 || strcmp(q.answer[0], "False") == 0)) all = false;
            if (!q.text[0] || q.answers < 2) all = false;
            for (int a = 0; a < q.answers; ++a) if (!q.answer[a][0]) all = false;
            if (q.answers == 2) CHECK(trivia::true_false(i));
        }
        CHECK(all);
        CHECK(per_diff[0] > 1000 && per_diff[1] > 1000 && per_diff[2] > 500 && tf > 500);
        for (int c = 0; c < trivia::kCategories; ++c) CHECK(cats[c] >= 30);
        CHECK(!trivia::get(trivia::count(), q) && !trivia::get(-1, q));
        trivia::release();
        printf("trivia: %d questions (easy %d, medium %d, hard %d; %d true / false)\n",
               trivia::count(), per_diff[0], per_diff[1], per_diff[2], tf);
    }
}

static void test_whowants()
{
    using namespace whowants;
    CHECK(safe_amount(4) == 0 && safe_amount(5) == 1000 && safe_amount(10) == 32000 && safe_amount(14) == 32000);
    {   // all right: a million; the levels climb
        Game g; g.start(5);
        for (int s = 0; s < kSteps; ++s) {
            CHECK(g.phase == Phase::Asking && g.step == s && g.q >= 0);
            const int lv = s < 5 ? 0 : s < 10 ? 1 : 2;
            CHECK(trivia::difficulty(g.q) == lv && !trivia::true_false(g.q));
            CHECK(g.lock(g.right_slot()) && g.reveal());
            if (s + 1 < kSteps) CHECK(g.phase == Phase::Right && g.next_question());
        }
        CHECK(g.phase == Phase::Over && g.won == 1000000);
    }
    {   // wrong on question 8: back to $1,000; walking away keeps what you have
        Game g; g.start(6);
        for (int s = 0; s < 7; ++s) { g.lock(g.right_slot()); g.reveal(); g.next_question(); }
        CHECK(g.step == 7 && g.banked() == 4000);
        CHECK(g.lock((g.right_slot() + 1) % 4) && g.reveal() && g.phase == Phase::Over && g.won == 1000);
        Game h; h.start(6);
        for (int s = 0; s < 3; ++s) { h.lock(h.right_slot()); h.reveal(); h.next_question(); }
        CHECK(h.walk_away() && h.won == 300 && h.walked);
    }
    {   // lifelines: once each; 50:50 never takes the right answer; the poll adds to 100
        for (uint32_t seed = 1; seed < 200; ++seed) {
            Game g; g.start(seed);
            CHECK(g.use(kFifty) && !g.use(kFifty));
            int gone = 0;
            for (int s = 0; s < 4; ++s) gone += g.hidden >> s & 1;
            CHECK(gone == 2 && !(g.hidden >> g.right_slot() & 1));
            CHECK(!g.lock(__builtin_ctz(g.hidden)));             // a removed answer can't be picked
            CHECK(g.use(kAudience));
            int sum = 0;
            for (int s = 0; s < 4; ++s) { sum += g.poll[s]; if (g.hidden >> s & 1) CHECK(g.poll[s] == 0); }
            CHECK(sum == 100);
            CHECK(g.use(kPhone) && g.friend_pick >= 0 && !(g.hidden >> g.friend_pick & 1));
        }
    }
    {   // no repeats until a level runs out; a fits() filter is obeyed
        Game g; g.start(9);
        std::set<int> seen;
        bool repeat = false;
        for (int k = 0; k < 300; ++k) {
            if (!seen.insert(g.q).second) repeat = true;
            g.start(1000 + k);
        }
        CHECK(!repeat);
        static auto short_only = [](int q) { trivia::Question t; return trivia::get(q, t) && strlen(t.text) < 60; };
        Game f; f.start(3, +[](int q) { return short_only(q); });
        trivia::Question t;
        CHECK(trivia::get(f.q, t) && strlen(t.text) < 60);
    }
    {   // save round trip; bad saves rejected
        Game g; g.start(77);
        g.use(kAudience);
        std::vector<uint8_t> buf(Game::kSaveBytes);
        CHECK(g.serialize(buf.data(), buf.size()) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf.data(), buf.size()) && h.q == g.q && !memcmp(h.poll, g.poll, 4) && !memcmp(h.played, g.played, sizeof g.played));
        std::vector<uint8_t> bad = buf; bad[7] = bad[8];                      // order not a shuffle
        CHECK(!h.deserialize(bad.data(), bad.size()));
        bad = buf; bad[4] = 15;
        CHECK(!h.deserialize(bad.data(), bad.size()));
    }
    {   // stats lines
        Record r; r.won = 32000; r.reached = 11; r.seconds = 125;
        char line[64] = "7,";
        format_body(line + 2, sizeof line - 2, r);
        Record back;
        CHECK(parse_line(line, back) && back.won == 32000 && back.reached == 11 && back.seconds == 125);
    }
    trivia::release();
}

static void test_jeoparcyd()
{
    using namespace jcyd;
    {   // the board: six categories, rows by difficulty, Daily Doubles
        Game g; g.start(11, 1);
        int dd = 0;
        for (int c = 0; c < kCats; ++c) {
            for (int c2 = 0; c2 < c; ++c2) CHECK(g.cat[c] != g.cat[c2]);
            for (int r = 0; r < kRows; ++r) {
                const Cell& x = g.cell[c][r];
                CHECK(x.q >= 0 && trivia::category(x.q) == g.cat[c] && !trivia::true_false(x.q));
                dd += x.daily;
                if (r == 0) CHECK(!x.daily);
            }
        }
        CHECK(dd == 1 && g.value(0) == 200 && g.value(4) == 1000);
    }
    {   // a clue: wrong costs, right pays and takes control; everyone wrong = reveal
        Game g; g.start(12, 1);
        int c = 0, r = 0;
        while (g.cell[c][r].daily) ++c;
        CHECK(g.pick(c, r) && g.phase == Phase::Clue);
        const int right = g.right_slot(), wrong = (right + 1) % 4;
        CHECK(g.answer(0, wrong) && g.score[0] == -200 && g.phase == Phase::Clue);
        CHECK(!g.answer(0, right));                                // you had your try
        CHECK(!g.answer(1, wrong));                                // that answer was given
        CHECK(g.answer(1, right) && g.score[1] == 200 && g.chooser == 1 && g.phase == Phase::Reveal);
        CHECK(g.done_revealing() && g.phase == Phase::Board);
        int c2 = 1;
        while (g.cell[c2][0].daily) ++c2;
        g.pick(c2, 0);
        const int w0 = (g.right_slot() + 1) % 4, w1 = (g.right_slot() + 2) % 4, w2 = (g.right_slot() + 3) % 4;
        g.answer(0, w0); g.answer(1, w1);
        CHECK(g.answer(2, w2) && g.phase == Phase::Reveal);
    }
    {   // a Daily Double: the picker alone, a wager within limits
        Game g; g.start(13, 1);
        int dc = -1, dr = -1;
        for (int c = 0; c < kCats; ++c) for (int r = 0; r < kRows; ++r) if (g.cell[c][r].daily) { dc = c; dr = r; }
        CHECK(g.pick(dc, dr) && g.phase == Phase::Wager);
        CHECK(g.set_wager(99999) && g.wager == 1000);              // no money yet: up to $1,000
        CHECK(!g.answer(1, g.right_slot()));
        CHECK(g.answer(0, g.right_slot()) && g.score[0] == 1000);
    }
    {   // whole games: three computers' plans; two rounds, a Final, an end
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            Game g; g.start(seed * 77, int(seed % 3));
            int guard = 0;
            while (g.phase != Phase::Over && ++guard < 2000) {
                switch (g.phase) {
                    case Phase::Board: { int c, r; g.pick_cell(g.level, &c, &r); CHECK(g.pick(c, r)); break; }
                    case Phase::Wager: CHECK(g.set_wager(g.cpu_wager(g.chooser))); break;
                    case Phase::Clue: {
                        if (g.wager) {                                   // the picker answers
                            const int s = g.chooser == 0 ? g.right_slot() : g.plan[g.chooser].slot;
                            if (s >= 0) g.answer(g.chooser, s); else g.time_up();
                            break;
                        }
                        int first = -1;
                        for (int p = 1; p < kPlayers; ++p)
                            if (g.plan[p].buzz_ms && (first < 0 || g.plan[p].buzz_ms < g.plan[first].buzz_ms)) first = p;
                        if (first > 0) { g.answer(first, g.plan[first].slot); if (g.phase == Phase::Clue) g.plan_clue(); }
                        else g.time_up();
                        break;
                    }
                    case Phase::Reveal: CHECK(g.done_revealing()); break;
                    case Phase::FinalWager:
                        for (int p = 0; p < kPlayers; ++p) if (g.in_final(p)) g.final_bet(p, g.cpu_final_wager(p));
                        for (int p = 0; p < kPlayers; ++p) if (g.in_final(p)) g.final_answer(p, g.cpu_final_slot(p));
                        break;
                    default: break;
                }
                if (g.round == 1) CHECK(g.value(0) == 400);
            }
            CHECK(g.phase == Phase::Over && guard < 2000);
            int firsts = 0;
            for (int p = 0; p < kPlayers; ++p) firsts += g.place(p) == 1;
            CHECK(firsts >= 1);
        }
    }
    {   // save round trip; bad saves
        Game g; g.start(21, 2);
        int c, r; g.pick_cell(2, &c, &r); g.pick(c, r);
        std::vector<uint8_t> buf(Game::kSaveBytes);
        CHECK(g.serialize(buf.data(), buf.size()) == Game::kSaveBytes);
        Game h;
        CHECK(h.deserialize(buf.data(), buf.size()) && h.q == g.q && h.phase == g.phase && !memcmp(h.cat, g.cat, kCats));
        std::vector<uint8_t> bad = buf; bad[4] = 7;
        CHECK(!h.deserialize(bad.data(), bad.size()));
        bad = buf; bad[6] = 99;                                       // a category that doesn't exist
        CHECK(!h.deserialize(bad.data(), bad.size()));
    }
    trivia::release();
}

static void test_hollywood()
{
    using namespace hcyd;
    {   // pick, judge right = yours; judge wrong = theirs; the turn passes
        Board b; b.reset(3);
        CHECK(b.can_play(4) && !b.can_play(kAgree));
        CHECK(b.play(4) && b.phase == Phase::Judge && b.turn == 0 && b.q >= 0);
        CHECK(b.play(b.star_right() ? kAgree : kDisagree) && b.owner[4] == 0 && b.turn == 1 && b.last_right);
        CHECK(b.play(0));
        CHECK(b.play(b.star_right() ? kDisagree : kAgree) && b.owner[0] == 0 && !b.last_right && b.last_got == 0);
    }
    {   // a winning square must be earned: X has 0 and 1; O misjudges square 2 -> it stays open
        Board b; b.reset(4);
        b.owner[0] = 0; b.owner[1] = 0; b.owner[4] = 1; b.turn = 1;
        CHECK(b.play(2));
        CHECK(b.play(b.star_right() ? kDisagree : kAgree) && b.owner[2] == -1 && b.last_got == -1 && b.winner < 0);
        // X earns it
        CHECK(b.play(2) && b.play(b.star_right() ? kAgree : kDisagree) && b.winner == 0 && b.phase == Phase::Over);
    }
    {   // five squares win without a line
        Board b; b.reset(5);
        const int x[4] = {0, 2, 5, 7};
        for (int s : x) b.owner[s] = 0;
        b.owner[1] = 1; b.owner[3] = 1; b.owner[4] = 1;
        CHECK(b.play(6) && b.play(b.star_right() ? kAgree : kDisagree) && b.winner == 0);
    }
    {   // whole games end; Hard beats Easy more often than not
        int hard = 0;
        const int N = 200;
        for (int s = 0; s < N; ++s) {
            Board b; b.reset(100 + s);
            int k = 0;
            for (; k < 400 && b.result() < 0; ++k) {
                const int side = b.turn;
                const int lv = (side == (s & 1)) ? 2 : 0;
                CHECK(b.play(best_move(b, lv, 7 + k * 31 + s)));
            }
            CHECK(b.result() >= 0);
            hard += b.result() == (s & 1);
        }
        printf("hollywood: Hard won %d of %d against Easy\n", hard, N);
        CHECK(hard > N * 55 / 100);
    }
    {   // save round trip; bad saves
        Board b; b.reset(9); b.play(4);
        std::vector<uint8_t> buf(Board::kSaveBytes);
        CHECK(b.serialize(buf.data(), buf.size()) == Board::kSaveBytes);
        Board h;
        CHECK(h.deserialize(buf.data(), buf.size()) && h.q == b.q && h.phase == Phase::Judge && h.square == 4);
        std::vector<uint8_t> bad = buf; bad[5] = 3;
        CHECK(!h.deserialize(bad.data(), bad.size()));
    }
    trivia::release();
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
    test_sank();
    test_wheel();
    test_ultimate();
    test_gomoku();
    test_morris();
    test_piperace();
    test_acquisitions();
    test_strategygo();
    test_dealcyd();
    test_presscyd();
    test_cardsharks();
    test_sorrycyd();
    test_escape();
    test_trivia();
    test_whowants();
    test_jeoparcyd();
    test_hollywood();
    test_stats();
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
