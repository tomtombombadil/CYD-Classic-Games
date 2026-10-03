// Ready-made puzzles, so "New game" is instant.
//
// Grading by solving technique (sudoku_grader.*) means Hard and Expert can
// take a few seconds to find on the ESP32. While Sudoku is open, a
// background task on the second CPU core keeps two puzzles per difficulty
// ready and saves them to LittleFS (/games/sudoku_stock.bin) so they
// survive power-off. Leaving Sudoku stops the task and frees its stack.
//
// Device only (FreeRTOS + LittleFS); the PC preview links no-op stubs.
#pragma once

#include "sudoku_core.h"

void sudoku_stock_begin();            // load saved stock, start the background task
void sudoku_stock_end();              // save the stock, stop the task
// Take a ready puzzle; false if none is ready (caller generates one itself).
bool sudoku_stock_take(sudoku::Difficulty d, sudoku::Grid& puzzle, sudoku::Grid& solution);
void sudoku_stock_loop();             // call while open: saves the stock when it changed
