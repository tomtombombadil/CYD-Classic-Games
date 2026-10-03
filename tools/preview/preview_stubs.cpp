// PC preview stand-ins for device-only modules.
#include "games/sudoku/sudoku_stock.h"

// No background puzzle stock on the PC: "New game" generates on the spot.
void sudoku_stock_begin() {}
void sudoku_stock_end() {}
bool sudoku_stock_take(sudoku::Difficulty, sudoku::Grid&, sudoku::Grid&) { return false; }
void sudoku_stock_loop() {}
