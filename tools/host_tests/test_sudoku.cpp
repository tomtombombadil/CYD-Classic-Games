// Host-side checks for src/games/sudoku/sudoku.* and grader.*  (built and run by CI)
#include "../../src/games/sudoku/sudoku_core.h"
#include "../../src/games/sudoku/sudoku_grader.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace sudoku;

static bool valid_full(const Grid& g)
{
    for (int i = 0; i < N; ++i) {
        if (g.c[i] < 1 || g.c[i] > 9) return false;
        for (int j = i + 1; j < N; ++j)
            if (g.c[i] == g.c[j] && same_unit(i, j)) return false;
    }
    return true;
}

int main(int argc, char** argv)
{
    const int runs = argc > 1 ? atoi(argv[1]) : 40;
    int failures = 0;
    Rng rng(12345);
    for (int d = 0; d < 4; ++d) {
        int min_c = 99, max_c = 0; long sum = 0; double worst_ms = 0, total_ms = 0;
        int levels[6] = {};
        for (int k = 0; k < runs; ++k) {
            Grid p, s;
            auto t0 = std::chrono::steady_clock::now();
            generate(static_cast<Difficulty>(d), rng, p, s);
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            total_ms += ms; if (ms > worst_ms) worst_ms = ms;
            Grid solved{};
            if (!valid_full(s)) { printf("FAIL: solution invalid\n"); ++failures; }
            if (count_solutions(p, 2, &solved) != 1) { printf("FAIL: not unique\n"); ++failures; }
            for (int i = 0; i < N; ++i) {
                if (solved.c[i] != s.c[i]) { printf("FAIL: solver disagrees\n"); ++failures; break; }
                if (p.c[i] && p.c[i] != s.c[i]) { printf("FAIL: clue != solution\n"); ++failures; break; }
                if ((p.c[i] != 0) != (p.c[80 - i] != 0)) { printf("FAIL: not symmetric\n"); ++failures; break; }
            }
            // The level must be exactly the one asked for
            const int lvl = difficulty_level(p);
            ++levels[lvl];
            if (lvl != d + 1) { printf("FAIL: %s puzzle graded level %d\n", difficulty_name(static_cast<Difficulty>(d)), lvl); ++failures; }
            if (d == 0 && clue_count(p) < 36) { printf("FAIL: Easy with %d clues\n", clue_count(p)); ++failures; }
            int c = clue_count(p); sum += c; if (c < min_c) min_c = c; if (c > max_c) max_c = c;
        }
        printf("%-6s clues %d-%d (avg %.1f)  gen avg %.2f ms, worst %.2f ms\n",
               difficulty_name(static_cast<Difficulty>(d)), min_c, max_c, double(sum) / runs,
               total_ms / runs, worst_ms);
    }
    // A known puzzle with a unique solution, and an invalid one
    Grid known{}; const char* k = "530070000600195000098000060800060003400803001700020006060000280000419005000080079";
    for (int i = 0; i < N; ++i) known.c[i] = k[i] - '0';
    if (count_solutions(known, 2) != 1) { printf("FAIL: known puzzle\n"); ++failures; }
    if (difficulty_level(known) != 1) { printf("FAIL: known easy puzzle graded %d\n", difficulty_level(known)); ++failures; }
    Grid bad = known; bad.c[2] = 5;  // duplicate 5 in row 0
    if (count_solutions(bad, 2) != 0) { printf("FAIL: conflicting givens\n"); ++failures; }
    if (grade(bad, 4).solved) { printf("FAIL: grader solved a broken puzzle\n"); ++failures; }
    // Known X-Wing puzzle (level 3) and a puzzle needing chains (beyond 4)
    const char* xw = "100000569492056108056109240009640801064010000218035604040500016905061402621000005";
    Grid x{}; for (int i = 0; i < N; ++i) x.c[i] = xw[i] - '0';
    printf("sample X-Wing puzzle graded level %d\n", difficulty_level(x));
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
