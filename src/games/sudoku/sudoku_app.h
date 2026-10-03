// Sudoku's link to the app: its registry entry (sudoku_ops), its save file
// and its stats, all through the app shell so they work the same on the
// board and in the PC preview.
#pragma once

#include "sudoku_game.h"
#include "sudoku_stats.h"

namespace sudoku_app {

constexpr const char* kId = "sudoku";          // registry id: /games/sudoku.bin, sudoku.csv

void save(const sudoku::Game& g);              // write the save file now
void record_stat(const sudoku::stats::Record& r);
bool load_stats(sudoku::stats::Summary& out);  // false = nothing could be read

} // namespace sudoku_app
