// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Fill the 9x9 grid so every row, every column and every 3x3 box holds the digits 1 to 9 once each.\n"
        "Every puzzle has exactly one answer, and none ever needs guessing: the level says which solving techniques it needs."},
    {"Entering Digits",
        "The third key in the tool row switches the input mode.\n"
        "\xE2\x80\xA2  Digit 1st: tap a digit, then the cells it goes in. Tap a cell holding that digit to clear it.\n"
        "\xE2\x80\xA2  Cell 1st: tap a cell, then a digit. The same digit again clears it.\n"
        "Clashing digits turn red."},
    {"Notes, Undo, Hint",
        "\xE2\x80\xA2  Notes: digits add or remove pencil marks instead. A placed digit clears itself from the notes it sees.\n"
        "\xE2\x80\xA2  Undo steps back one action.\n"
        "\xE2\x80\xA2  Hint: the first tap points at a cell, the second fills it in (green). Hints count in your stats."},
    {"Levels And Stats",
        "Easy: singles only. Medium: adds locked candidates. Hard: pairs, triples, X-Wing. Expert: Swordfish, XY- and XYZ-Wing.\n"
        "The clock stops after 2 minutes without a touch. Stats keep every solve, time and hints used."},
};

} // namespace

CYD_HELP(sudoku, kPages)
