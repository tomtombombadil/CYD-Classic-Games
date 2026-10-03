// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Turn every light off.\n"
        "Tapping a light flips it and its four neighbours (above, below, left, right): on goes off, off goes on.\n"
        "Every board can be solved. Tapping the same light twice undoes it, so the order of your taps doesn't matter."},
    {"Par And Hints",
        "Par in the top bar is the fewest taps that solve this board. Try to match it.\n"
        "Hint: the first tap outlines a light that is part of a shortest solution; the second tap presses it.\n"
        "Easy, Medium and Hard start from more scrambled boards."},
};

} // namespace

CYD_HELP(lightswitch, kPages)
