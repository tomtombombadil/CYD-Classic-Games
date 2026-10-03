// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Put the tiles in order, 1 at the top left, reading across like a book, with the gap last.\n"
        "3x3 is quick, 4x4 is the classic 15-Puzzle, 5x5 is a long haul."},
    {"Moving Tiles",
        "Tap any tile in the gap's row or column: it and every tile between it and the gap slide over by one.\n"
        "Tiles already in their home spot are tinted, so you can see what's done.\n"
        "Stats keep your best time and fewest moves for each size."},
};

} // namespace

CYD_HELP(sliding, kPages)
