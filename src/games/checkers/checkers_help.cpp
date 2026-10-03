// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "Pieces move diagonally on the dark squares; Black goes first.\n"
        "A man moves one step forward. Jump an enemy piece by hopping over it to the empty square beyond; it's removed.\n"
        "Take all the other side's pieces, or leave them no move, to win."},
    {"Jumps And Kings",
        "Jumping is compulsory: if you can jump, you must. After a jump, the same piece keeps jumping while it can.\n"
        "A man reaching the far row is crowned king and moves both ways. Crowning ends the move.\n"
        "40 moves each with no jump and no man moving is a draw."},
    {"Playing",
        "Tap one of your pieces (its landing squares show as dots), then tap where it goes. When a jump is possible, only pieces that can jump respond.\n"
        "A double jump is tapped one landing at a time.\n"
        "Long-press any piece to see where it could move; the next tap clears that."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(checkers, kPages)
