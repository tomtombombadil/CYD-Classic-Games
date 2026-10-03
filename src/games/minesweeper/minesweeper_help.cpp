// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Open every cell that isn't a mine.\n"
        "A number tells how many of the 8 cells around it hide a mine. Use the numbers to work out where the mines are.\n"
        "The first tap always opens an area, and every board can be cleared by logic alone: no guessing needed."},
    {"Dig And Flag",
        "\xE2\x80\xA2  Dig (key under the board): a tap opens a cell.\n"
        "\xE2\x80\xA2  Flag: a tap marks a cell you know is a mine; tap again to remove it.\n"
        "\xE2\x80\xA2  Long-press a hidden cell to flag it in either mode.\n"
        "\xE2\x80\xA2  Tap a number whose mines are all flagged to open the cells around it."},
    {"Winning",
        "Open every safe cell to win. Opening a mine ends the game and shows the rest; a crossed-out flag was wrong.\n"
        "The top bar counts mines minus flags.\n"
        "Easy 8x10 with 10 mines, Medium 9x11 with 15, Hard 10x11 with 20."},
};

} // namespace

CYD_HELP(minesweeper, kPages)
