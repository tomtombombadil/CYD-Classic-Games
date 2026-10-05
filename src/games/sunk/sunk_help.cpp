// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Each side hides a fleet in a 10 x 10 sea: Carrier 5 cells, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2. Ships lie across or down and never touch.\n"
        "Take turns firing one shot into the other sea. Sink every ship to win."},
    {"Your Fleet",
        "Your fleet is placed for you: Shuffle until you like it, then Ready.\n"
        "When both fleets are set, the side that sets up first fires first.\n"
        "In pass-and-play the board says who to pass it to and waits for Ready, so nobody sees the other sea."},
    {"Firing",
        "Their Waters is the sea you fire at: tap a cell. A white peg is a miss, red a hit; a sunk ship shows. The cells round a sunk ship must be water, so they get a small dot and can't be picked.\n"
        "My Fleet shows your ships and their shots. The last shot has a gold frame.\n"
        "At the end, tap the sea to see both."},
    {"The Computer",
        "Easy fires at random, and after a hit tries the cells around it.\n"
        "Medium fires on a spaced pattern and follows a line of hits.\n"
        "Hard works out every way the ships left could lie and fires where most of them cross."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(sunk, kPages)
