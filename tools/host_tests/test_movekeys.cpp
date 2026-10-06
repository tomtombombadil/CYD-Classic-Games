// Wireless move keys must not change while a game's version stays the same
// (src/games/common/net_games.h). Plays fixed pseudo-random games of every
// wireless game with its rules engine, hashes the move keys that would go
// on the air, and compares with tools/host_tests/net_moves.txt.
//
// A failure means the moves a game sends changed: if that was meant, bump
// the game's version in net_games.h and add the printed line to
// net_moves.txt (keep the old lines). Also checks the name lists.
//
// Build: see .github/workflows/build.yml. Run from the repo root.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include "games/checkers/checkers_core.h"
#include "games/chess/chess_core.h"
#include "games/common/net_games.h"
#include "games/fourconnect/fourconnect_core.h"
#include "games/mancala/mancala_core.h"
#include "games/morris/morris_core.h"
#include "games/reversi/reversi_core.h"
#include "games/sank/sank_core.h"
#include "games/ultimate/ultimate_core.h"
#include "games/gomoku/gomoku_core.h"
#include "games/tictactoe/tictactoe_core.h"
#include "net/names.h"

namespace {

int failures = 0;
void check(bool ok, const char* what)
{
    if (!ok) { printf("FAIL %s\n", what); ++failures; }
}

struct Rng {
    uint32_t s;
    uint32_t next() { s = s * 1103515245u + 12345u; return s >> 8; }
};

// A move picked from the keys in key order, so the rules engine's own list
// order doesn't matter (only what the keys mean does)
uint32_t pick(std::vector<uint32_t>& keys, Rng& r)
{
    std::sort(keys.begin(), keys.end());
    return keys[r.next() % uint32_t(keys.size())];
}

struct Hash {
    uint32_t h = 2166136261u;
    int n = 0;
    void add(uint32_t k) { for (int b = 0; b < 4; ++b) h = (h ^ ((k >> (8 * b)) & 0xFF)) * 16777619u; ++n; }
};

// Each game: play `games` games of random legal moves, hash the keys
template <class Fn> uint32_t play_games(Fn one_game)
{
    Hash h;
    Rng r{7};
    for (int g = 0; g < 6; ++g) one_game(r, h);
    return h.h;
}

uint32_t fourconnect_hash()
{
    return play_games([](Rng& r, Hash& h) {
        fourconnect::Board b;
        while (b.result() == -1) {
            int c;
            do c = int(r.next() % fourconnect::kCols); while (!b.can_play(c));
            h.add(uint32_t(c));
            b.play(c);
        }
    });
}

uint32_t tictactoe_hash()
{
    return play_games([](Rng& r, Hash& h) {
        tictactoe::Board b;
        while (b.result() == -1) {
            int c;
            do c = int(r.next() % 9); while (!b.can_play(c));
            h.add(uint32_t(c));
            b.play(c);
        }
    });
}

uint32_t reversi_hash()
{
    return play_games([](Rng& r, Hash& h) {
        reversi::Board b;
        while (b.result() == -1) {
            const uint64_t l = b.legal();
            if (!l) break;
            int k = int(r.next() % uint32_t(__builtin_popcountll(l)));
            uint64_t m = l;
            while (k--) m &= m - 1;
            const int sq = __builtin_ctzll(m);
            h.add(uint32_t(sq));
            b.play(sq);
        }
    });
}

uint32_t checkers_hash()
{
    return play_games([](Rng& r, Hash& h) {
        auto* g = new checkers::Game();
        for (int ply = 0; g->result() == -1 && ply < 200; ++ply) {
            checkers::MoveList l;
            g->legal(l);
            if (!l.n) break;
            std::vector<uint32_t> keys;
            for (int k = 0; k < l.n; ++k) keys.push_back(checkers::move_key(l.m[k]));
            const uint32_t key = pick(keys, r);
            h.add(key);
            check(checkers::find_key(*g, key) >= 0, "checkers: a key finds its move");
            g->play(checkers::find_key(*g, key));
        }
        delete g;
    });
}

uint32_t chess_hash()
{
    return play_games([](Rng& r, Hash& h) {
        auto* g = new chess::Game();
        for (int ply = 0; g->result() == -1 && ply < 160; ++ply) {
            chess::MoveList l;
            g->legal(l);
            if (!l.n) break;
            std::vector<uint32_t> keys;
            for (int k = 0; k < l.n; ++k) keys.push_back(chess::move_key(l.m[k]));
            const uint32_t key = pick(keys, r);
            h.add(key);
            check(chess::find_key(*g, key) >= 0, "chess: a key finds its move");
            g->play(chess::find_key(*g, key));
        }
        delete g;
    });
}

uint32_t mancala_hash()
{
    return play_games([](Rng& r, Hash& h) {
        mancala::Board b;
        while (b.result() == -1) {
            int p;
            do p = int(r.next() % mancala::kPits); while (!b.can_play(p));
            h.add(uint32_t(p));
            b.play(p);
        }
    });
}

uint32_t morris_hash()
{
    return play_games([](Rng& r, Hash& h) {
        morris::Game g;
        for (int ply = 0; g.result() == -1 && ply < 300; ++ply) {
            morris::MoveList l;
            g.legal(l);
            if (!l.n) break;
            std::vector<uint32_t> keys;
            for (int k = 0; k < l.n; ++k) keys.push_back(uint32_t(l.m[k].code()));
            const int code = int(pick(keys, r));
            h.add(uint32_t(code));
            g.play(code);
        }
    });
}

uint32_t sank_hash()
{
    return play_games([](Rng& r, Hash& h) {
        auto* b = new sank::Board();
        // The fleets, a ship a move (any place it fits)
        while (b->setup()) {
            uint32_t places[2 * sank::kCells];
            const int n = sank::ship_places(b->fleet[b->turn()], b->placed(b->turn()), places);
            std::vector<uint32_t> keys(places, places + n);
            const uint32_t key = pick(keys, r);
            h.add(key);
            b->play(key);
        }
        while (b->result() == -1) {
            std::vector<uint32_t> keys;
            for (uint32_t c = 0; c < uint32_t(sank::kCells); ++c) if (b->can_play(c)) keys.push_back(c);
            const uint32_t key = pick(keys, r);
            h.add(key);
            b->play(key);
        }
        h.add(uint32_t(b->result()));
        delete b;
    });
}

uint32_t ultimate_hash()
{
    return play_games([](Rng& r, Hash& h) {
        ultimate::Board b;
        while (b.result() == -1) {
            uint8_t m[ultimate::kCells];
            const int n = b.legal(m);
            std::vector<uint32_t> keys(m, m + n);
            const uint32_t key = pick(keys, r);
            h.add(key);
            b.play(int(key));
        }
        h.add(uint32_t(b.result()));
    });
}

uint32_t gomoku_hash()
{
    return play_games([](Rng& r, Hash& h) {
        auto* b = new gomoku::Board();
        // random moves near the centre (a random game on the whole board rarely ends in a five)
        while (b->result() == -1) {
            std::vector<uint32_t> keys;
            for (int p = 0; p < gomoku::kPoints; ++p)
                if (b->can_play(p) && p / gomoku::kN >= 5 && p / gomoku::kN <= 9 && p % gomoku::kN >= 5 && p % gomoku::kN <= 9)
                    keys.push_back(uint32_t(p));
            if (keys.empty()) for (int p = 0; p < gomoku::kPoints; ++p) if (b->can_play(p)) keys.push_back(uint32_t(p));
            const uint32_t key = pick(keys, r);
            h.add(key);
            b->play(int(key));
        }
        h.add(uint32_t(b->result()));
        delete b;
    });
}

uint32_t hash_for(const char* id)
{
    if (!strcmp(id, "fourconnect")) return fourconnect_hash();
    if (!strcmp(id, "tictactoe"))   return tictactoe_hash();
    if (!strcmp(id, "reversi"))     return reversi_hash();
    if (!strcmp(id, "checkers"))    return checkers_hash();
    if (!strcmp(id, "chess"))       return chess_hash();
    if (!strcmp(id, "mancala"))     return mancala_hash();
    if (!strcmp(id, "morris"))      return morris_hash();
    if (!strcmp(id, "sank"))        return sank_hash();
    if (!strcmp(id, "ultimate"))    return ultimate_hash();
    if (!strcmp(id, "gomoku"))      return gomoku_hash();
    return 0;
}

void test_names()
{
    check(names::first_count() >= 64 && names::second_count() >= 64, "names: both lists are long");
    check(names::first_count() < 65536 && names::second_count() < 65536, "names: numbers fit 16 bits");
    for (int i = 0; i < names::first_count(); ++i) {
        const char* w = names::first(i);
        check(strlen(w) >= 3 && strlen(w) <= names::kWordMax, "names: first-list word length");
        for (int j = 0; j < i; ++j) check(strcmp(w, names::first(j)) != 0, "names: no repeated first word");
    }
    for (int i = 0; i < names::second_count(); ++i) {
        const char* w = names::second(i);
        check(strlen(w) >= 3 && strlen(w) <= names::kWordMax, "names: second-list word length");
        for (int j = 0; j < i; ++j) check(strcmp(w, names::second(j)) != 0, "names: no repeated second word");
    }
    char n[names::kNameMax + 1];
    uint16_t a = 0, b = 0;
    for (uint32_t s = 0; s < 500; ++s) {
        names::random_pair(s, &a, &b);
        check(a < names::first_count() && b < names::second_count(), "names: random pair in range");
        names::format(a, b, n, sizeof n);
        uint16_t a2 = 999, b2 = 999;
        check(names::parse(n, &a2, &b2) && a2 == a && b2 == b, "names: parse(format()) gives the pair back");
    }
    // The first words are frozen forever (append-only lists)
    names::format(0, 0, n, sizeof n);
    check(strcmp(n, "Wobbly Llama") == 0, "names: word 0 of each list never changes");
    names::format(9999, 9999, n, sizeof n);
    check(strcmp(n, "? ?") == 0, "names: a number from a newer board shows as ?");
}

} // namespace

int main()
{
    test_names();
    // wplay puts a one-player game aside in a 1 KB stack buffer (kStashMax)
    const size_t biggest[] = {fourconnect::Board::kSaveBytes, tictactoe::Board::kSaveBytes, reversi::Board::kSaveBytes,
                              checkers::Game::kSaveBytes, chess::Game::kSaveBytes, mancala::Board::kSaveBytes,
                              morris::Game::kSaveBytes, sank::Board::kSaveBytes,
                              ultimate::Board::kSaveBytes, gomoku::Board::kSaveBytes};
    for (size_t b : biggest) check(b + 8 < 1024, "a wireless game's save fits wplay's 1 KB stash buffer");
    // net_moves.txt: "<id> <key> <version> <hash>" per line, '#' comments
    std::vector<std::string> lines;
    if (FILE* f = fopen("tools/host_tests/net_moves.txt", "r")) {
        char buf[200];
        while (fgets(buf, sizeof buf, f)) lines.push_back(buf);
        fclose(f);
    } else {
        check(false, "tools/host_tests/net_moves.txt is readable (run from the repo root)");
    }
    for (const netgames::Entry& e : netgames::kGames) {
        const uint32_t h = hash_for(e.id);
        char want[96];
        snprintf(want, sizeof want, "%s %d %d %08x", e.id, e.key, e.version, unsigned(h));
        bool found_version = false, same = false;
        char prefix[48];
        snprintf(prefix, sizeof prefix, "%s %d %d ", e.id, e.key, e.version);
        for (const std::string& l : lines) {
            if (l.compare(0, strlen(prefix), prefix) != 0) continue;
            found_version = true;
            same = l.compare(0, strlen(want), want) == 0;
        }
        if (!found_version) {
            printf("FAIL %s version %d has no line in net_moves.txt; add:\n%s\n", e.id, e.version, want);
            ++failures;
        } else if (!same) {
            printf("FAIL %s: the moves it sends changed but its version (%d) didn't. Bump its version in "
                   "src/games/common/net_games.h and add:\n%s\n", e.id, e.version, want);
            ++failures;
        }
        for (const netgames::Entry& o : netgames::kGames)
            if (&o != &e) check(o.key != e.key, "net_games: every key is different");
        check(e.key != 0 && e.key != 0xFF, "net_games: keys 0 and 255 are reserved");
    }
    printf(failures ? "test_movekeys: %d failure(s)\n" : "test_movekeys: all passed\n", failures);
    return failures ? 1 : 0;
}
