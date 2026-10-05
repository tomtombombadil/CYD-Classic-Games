// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "A 15 x 15 board. Black places a stone first, then the players take turns placing one stone on any empty point (where the lines cross).\n"
        "Five or more of your stones in a row - across, down or diagonal - wins."},
    {"Playing",
        "Tap a point to place your stone there. The last stone has a gold ring; the winning five gets a line through it.\n"
        "Watch out: four in a row with both ends open can't be stopped, and three with both ends open soon becomes four. Block those early!"},
    {"The Computer",
        "Easy plays its own lines and blocks only a five.\n"
        "Medium and Hard look 4 and 6 moves ahead among the most promising points.\n"
        "A full board with no five is a draw."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(gomoku, kPages)
