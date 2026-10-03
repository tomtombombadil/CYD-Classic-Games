// One-time pickup of files left by CYD-Sudoku v1.0.0 when this firmware is
// flashed over it without erasing: its game, stats and puzzle stock move
// to this firmware's per-game names, so the Sudoku game and its history
// carry on. Touch calibration, panel fixes and settings already share
// their names and need nothing.
#pragma once

void legacy_import();   // call once at boot, after storage is available
