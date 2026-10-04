// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Black and White take turns placing discs; Black goes first.\n"
        "A disc must outflank: it traps a straight line of the other side's discs between itself and one of yours. Every trapped disc flips to your color, in every direction at once."},
    {"Playing",
        "Your legal moves show as dots. Tap one to play it.\n"
        "If you have no legal move your turn passes. The game ends when neither side can move; the most discs wins.\n"
        "The top bar counts both sides' discs."},
    {"The Computer",
        "Easy looks 2 moves ahead, Medium 4, Hard 6. Hard also plays the last 10 empty squares perfectly."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(reversi, kPages)
