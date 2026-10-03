// Calibration: grade puzzles from the Sudoku Exchange puzzle bank (public
// domain; https://github.com/grantm/sudoku-exchange-puzzle-bank) and compare
// with their Sukaku Explainer ratings. Run on a PC:
//   calibrate <bank dir> [samples per file]
#include "../../src/games/sudoku/sudoku_grader.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace sudoku;

int main(int argc, char** argv)
{
    const std::string dir = argv[1];
    const int samples = argc > 2 ? atoi(argv[2]) : 2000;
    const char* files[4] = {"easy", "medium", "hard", "diabolical"};
    int wrong = 0;
    printf("%-11s  %7s %7s %7s %7s %9s   (our level; 'stuck' = needs more than level 4)\n",
           "bank file", "L1", "L2", "L3", "L4", "stuck");
    for (int f = 0; f < 4; ++f) {
        FILE* fp = fopen((dir + "/" + files[f] + ".txt").c_str(), "r");
        fseek(fp, 0, SEEK_END);
        const long recs = ftell(fp) / 100;
        int count[6] = {};
        std::map<std::string, std::vector<int>> by_rating;   // SE rating -> our levels
        double ms = 0;
        for (int k = 0; k < samples; ++k) {
            const long rec = (long)((double)k / samples * recs);
            char line[101] = {};
            fseek(fp, rec * 100, SEEK_SET);
            if (fread(line, 1, 100, fp) != 100) break;
            Grid g{};
            for (int i = 0; i < 81; ++i) g.c[i] = line[13 + i] - '0';
            const std::string rating(line + 95, 4);
            auto t0 = std::chrono::steady_clock::now();
            GradeResult r = grade(g, 4);
            ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const int lvl = r.solved ? r.level : 5;
            ++count[lvl];
            by_rating[rating].push_back(lvl);
            if (r.solved) {
                Grid sol{};
                if (count_solutions(g, 2, &sol) != 1 || memcmp(sol.c, r.grid.c, 81) != 0) ++wrong;
            }
        }
        printf("%-11s  %7d %7d %7d %7d %9d   avg %.3f ms\n", files[f], count[1], count[2], count[3], count[4],
               count[5], ms / samples);
        for (auto& kv : by_rating) {
            int c[6] = {};
            for (int l : kv.second) ++c[l];
            printf("    SE %s n=%-5zu L1 %3d  L2 %3d  L3 %3d  L4 %3d  stuck %3d\n", kv.first.c_str(),
                   kv.second.size(), c[1], c[2], c[3], c[4], c[5]);
        }
        fclose(fp);
    }
    printf(wrong ? "WRONG SOLUTIONS: %d\n" : "all logical solutions match the true solution\n", wrong);
    return wrong ? 1 : 0;
}
