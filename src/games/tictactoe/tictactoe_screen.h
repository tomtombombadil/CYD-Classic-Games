// Tic-Tac-Toe screen: a big 3x3 board drawn as one object (X in blue, O in
// red, a line through three in a row) under the shared two-player top bar.
#pragma once

#include <lvgl.h>
#include "tictactoe_core.h"

namespace tictactoe_ui {

void screen_build(tictactoe::Board& b);    // after match::attach
void screen_destroy();
void board_redraw();
void icon(lv_obj_t* parent, int size);

} // namespace tictactoe_ui
