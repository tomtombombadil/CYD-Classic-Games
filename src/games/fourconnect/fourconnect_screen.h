// FourConnect screen: the board drawn as one object (frame, holes, discs,
// last move, winning four) under the shared two-player top bar. Tap
// anywhere in a column to drop a disc there.
#pragma once

#include <lvgl.h>
#include "fourconnect_core.h"

namespace fourconnect_ui {

void screen_build(fourconnect::Board& b);   // after match::attach
void screen_destroy();
void board_redraw();
void icon(lv_obj_t* parent, int size);

} // namespace fourconnect_ui
