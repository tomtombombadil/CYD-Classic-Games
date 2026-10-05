// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Nine small tic-tac-toe boards make one big board. Win a small board with three in a row; win the game with three small boards in a row on the big board.\n"
        "X starts, anywhere."},
    {"Where You Play",
        "The square you pick sends the other player to the small board in the same place: play the top right square of a board, and they must play in the top right board.\n"
        "If that board is already won or full, they may play in any open board.\n"
        "The board(s) you may play in are lit."},
    {"The Board",
        "A won board shows a big X or O; a full board with no winner turns grey and counts for nobody. All boards done with no three in a row is a draw.\n"
        "The last mark has a gold square.\n"
        "Computer: Easy looks 2 moves ahead, Medium 4, Hard thinks about a second."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(ultimate, kPages)
