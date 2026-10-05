// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;
using games::kHelpWireless;

const HelpPage kPages[] = {
    {"The Idea",
        "Each side hides a fleet in a 10 x 10 sea: Carrier 5 squares, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2. Ships lie across or down; they may touch but not overlap.\n"
        "Take turns firing one shot into the other sea. Sink every ship to win."},
    {"Your Fleet",
        "Biggest ship first: tap the square where one end goes (it turns gold), then one of the lit squares - the way the ship points. Undo takes back the last ship. When all five are in, tap Ready.\n"
        "Options in the menu: Ship Placement Random instead - Shuffle until you like it.\n"
        "In pass-and-play the board says who to pass it to and waits for Ready."},
    {"Firing",
        "Their Waters is the sea you fire at: tap a square. A white peg is a miss, red a hit; a sunk ship shows.\n"
        "My Fleet shows your ships and their shots. The last shot has a gold frame.\n"
        "The side that placed its fleet first fires first. At the end, tap the sea to see both."},
    {"The Computer",
        "Easy fires at random, and after a hit tries the squares around it.\n"
        "Medium fires on a spaced pattern and follows a line of hits.\n"
        "Hard works out every way the ships left could lie and fires where most of them cross."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(sunk, kPages)
