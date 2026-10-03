// The Sudoku game screen: top bar, board, tool row, digit bank, menu and
// stats. Hardware actions go through the app shell (src/ui/shell.h), so this
// file also builds in the PC preview tool (tools/preview).
#pragma once

#include <cstdint>
#include "sudoku_game.h"
#include "ui/theme.h"

namespace sudoku_ui {

// Builds the screen for a game that is already loaded or started.
void screen_create(sudoku::Game& g);
// Removes every object of the screen and forgets the game (does not save).
void screen_destroy();
// Call often while the game is open: runs the clock and debounced autosave.
void screen_tick(uint32_t now_ms);
// Theme changed: rebuild with the new palette.
void screen_restyle();

// ---- Used by the PC preview to stage screenshots ---------------------------
void tap_cell(int idx);
void tap_digit(int d);
void set_notes(bool on);
void set_input_mode(ui::InputMode m);
void open_menu();
void menu_tap_new_game(int difficulty);
void open_stats();
void hint();

} // namespace sudoku_ui
