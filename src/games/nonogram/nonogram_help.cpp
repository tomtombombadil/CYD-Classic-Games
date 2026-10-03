// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Paint a hidden picture. The numbers beside each row and above each column are its clue: the lengths of the runs of filled cells in that line, in order. \"3 1\" means a run of 3, then at least one gap, then a run of 1.\n"
        "Fill the cells so every clue matches."},
    {"Fill And Mark",
        "Fill (key under the grid): a tap fills a cell; tap again to clear it.\n"
        "Mark: a tap puts an X where you know nothing goes, to help you think.\n"
        "A clue turns grey when its line matches. The row and column you last tapped are tinted."},
    {"Solving",
        "Every puzzle has one answer, and you can always find the next cell by looking at one row or column at a time; no guessing.\n"
        "Start with big runs: a run of 4 in 5 cells must cover the middle three.\n"
        "Stats keep your best time for each size."},
};

} // namespace

CYD_HELP(nonogram, kPages)
