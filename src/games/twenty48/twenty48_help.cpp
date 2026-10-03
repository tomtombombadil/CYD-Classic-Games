// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Slide the tiles to merge them. Two equal tiles that meet join into one worth their sum: 2 + 2 = 4, 4 + 4 = 8, and on up.\n"
        "After every slide a new 2 (sometimes a 4) appears. Make a 2048 tile to win, then keep going for a higher score."},
    {"Sliding",
        "Tap toward the side you want the tiles to go: above the board slides up, below it down, left or right of it sideways.\n"
        "The board's two diagonals split the whole screen into these four zones, so a tap anywhere works. A tap that moves nothing does nothing."},
    {"Score And End",
        "Each merge adds the new tile's value to your score. The newest tile has a dark ring.\n"
        "The game ends when no slide can move a tile. Stats keep games played to the end, and any game that reached 2048."},
};

} // namespace

CYD_HELP(twenty48, kPages)
