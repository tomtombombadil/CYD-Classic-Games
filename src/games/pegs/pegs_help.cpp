// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Jump pegs over each other until only one is left.\n"
        "A peg jumps over a neighbouring peg into the empty hole straight beyond it, and the peg it jumped over is taken off. On the Triangle, jumps go in six directions; on the other boards only across and up or down."},
    {"Playing",
        "Tap a peg: the holes it can jump to show as dots. Then tap one of them.\n"
        "Undo takes back jumps, as many as you like, even after you get stuck. Restart This Game (in the menu) puts every peg back."},
    {"Boards",
        "Triangle: 15 holes, a quick warm-up.\n"
        "English: the classic 33-hole cross, centre empty.\n"
        "European: 37 holes, the hardest; it starts with the hole two above the centre, because the centre start can't be solved.\n"
        "Stats keep wins, losses and best times."},
};

} // namespace

CYD_HELP(pegs, kPages)
