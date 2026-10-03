// The Sudoku 9x9 board as one custom-drawn LVGL object (cheaper than 81 widgets).
#pragma once

#include <lvgl.h>
#include "sudoku_game.h"

namespace sudoku_ui {

struct BoardHighlight {
    int selected  = -1;   // selected cell, -1 = none
    int digit     = 0;    // highlight every cell holding this digit (0 = none)
};

// Creates the board. `cell` = cell size in px; the object is 9*cell+2 square.
// on_tap(cell_index) is called when a cell is pressed.
lv_obj_t* board_create(lv_obj_t* parent, const sudoku::Game* g, int cell,
                       void (*on_tap)(int cell_index));

void board_set_highlight(lv_obj_t* board, const BoardHighlight& h);
void board_refresh(lv_obj_t* board);   // redraw after the game changed

} // namespace sudoku_ui
