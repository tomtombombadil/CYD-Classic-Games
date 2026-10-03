// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "The CYD hides a code of colored pegs. Crack it in 10 guesses.\n"
        "After each guess, small marks next to it tell you:\n"
        "\xE2\x80\xA2  a filled dot for each peg that is the right color in the right place\n"
        "\xE2\x80\xA2  a ring for each right color in the wrong place\n"
        "The marks don't say which pegs they mean."},
    {"Guessing",
        "Tap a color at the bottom to put it in the next empty slot. Tap a slot in your row to take its peg out.\n"
        "When the row is full, tap Check.\n"
        "The code shows at the top when the game ends."},
    {"Levels",
        "Easy: 4 pegs, no color used twice.\n"
        "Normal: 4 pegs, colors may repeat.\n"
        "Hard: 5 pegs, colors may repeat.\n"
        "There are always 6 colors. Stats keep wins, losses, guesses and best times for each level."},
};

} // namespace

CYD_HELP(mastercyd, kPages)
