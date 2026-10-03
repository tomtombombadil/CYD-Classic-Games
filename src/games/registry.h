// The registry of games: what the picker shows and how to start, save and
// leave each game. The list itself is src/games/games.def.
//
// Only one game is alive at a time. open() builds its screen (loading its
// save), close() saves and frees everything it allocated: screen objects,
// game state, AI tables, background tasks.
#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>

namespace games {

// The picker's first screen lists these, in this order (Tom's list).
enum class Category : uint8_t { Puzzles = 0, Strategy, Word, Dice, Other };
constexpr int kCategories = 5;
const char* category_title(Category c);        // "Puzzle Games", ...
const char* category_short(Category c);        // "Puzzles", ... (page titles)

// Ways a game can be played (or-ed in games.def)
enum Modes : uint8_t {
    kSolo        = 1,
    kVsComputer  = 2,
    kPassAndPlay = 4,
    kNetwork     = 8,      // CYD to CYD over ESP-NOW
};

struct GameOps {
    void (*open)();                            // load save, build the screen
    void (*close)();                           // save, free screen, state, tasks
    void (*save)();                            // save now (before a restart)
    void (*tick)(uint32_t now_ms);             // clock, autosave, task glue
    void (*restyle)();                         // theme changed: rebuild the screen
    // One line for "Continue" on the picker, e.g. "Medium, 4:05".
    // False = nothing saved (the picker then just says the title).
    bool (*summary)(char* buf, size_t cap);
    // Draw the picker icon into a size x size area of `parent`.
    void (*icon)(lv_obj_t* parent, int size);
};

struct GameInfo {
    const char*    id;
    const char*    title;
    Category       category;
    uint8_t        modes;
    const char*    blurb;
    const GameOps* ops;
};

int             count();
const GameInfo& get(int index);
int             find(const char* id);          // -1 if unknown

} // namespace games
