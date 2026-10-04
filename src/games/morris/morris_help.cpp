// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Each side has nine men; White starts. Three of your men in a line - a mill - lets you take one of the other side's men.\n"
        "Take them down to two men, or leave them no move, and you win."},
    {"Placing, Then Moving",
        "First, take turns placing a man on any empty point (tap it).\n"
        "With all men placed, a turn moves one man along a line to the next empty point: tap your man (dots show where it can go), then the point.\n"
        "A side down to three men may fly: move to any empty point."},
    {"Mills",
        "Make a mill and the men you may take get a red ring: tap one. Men in a mill are safe while the other side has men outside mills.\n"
        "Tap your new man again to take that move back.\n"
        "The last move is tinted; a red ring on an empty point shows where a man was taken. 50 moves each with nothing taken is a draw."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(morris, kPages)
