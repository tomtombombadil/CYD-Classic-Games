// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Two players take turns dropping discs into the 7 columns. A disc falls to the lowest free spot.\n"
        "The first to get four of their discs in a line wins: across, up and down, or diagonal. A full board with no line is a draw."},
    {"Playing",
        "Tap anywhere in a column to drop your disc there.\n"
        "A dot marks the last disc played. The winning four get a ring.\n"
        "Play Again appears when the game ends."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(fourconnect, kPages)
