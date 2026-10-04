// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "X and O take turns marking the 3x3 grid; X goes first.\n"
        "Three of your marks in a row, column or diagonal wins. A full grid with no line is a draw.\n"
        "Tap a square to mark it."},
    {"The Computer",
        "Easy looks one move ahead, Medium two. Hard plays perfectly: you can't beat it, but you can always hold it to a draw.\n"
        "Play Again appears when the game ends."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(tictactoe, kPages)
