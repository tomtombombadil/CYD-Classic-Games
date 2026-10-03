// PC preview stand-ins for device-only modules.
#include "games/sudoku/sudoku_stock.h"

// No background puzzle stock on the PC: "New game" generates on the spot.
void sudoku_stock_begin() {}
void sudoku_stock_end() {}
bool sudoku_stock_take(sudoku::Difficulty, sudoku::Grid&, sudoku::Grid&) { return false; }
void sudoku_stock_loop() {}

// Computer moves run straight away on the PC.
#include "games/common/ai_task.h"
namespace { bool stop_never = false; }
bool ai_start(AiJob job, void* ctx) { job(ctx, &stop_never); return true; }
bool ai_busy() { return false; }
void ai_stop() {}
