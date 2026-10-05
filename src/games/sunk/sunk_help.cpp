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
        "Biggest ship first: tap where one end goes (it turns gold), then a lit square the way it points (near an edge it slides in). Undo takes one back; Ready when all five are in.\n"
        "Options (menu): Ship Placement Random - Shuffle until you like it.\n"
        "Pass-and-play: the board says who to pass it to, then waits for Ready."},
    {"Firing",
        "Your turn shows Their Waters: tap a square. The shell falls with a whistle - a splash and MISS!, or an explosion and HIT!. Then the view turns to My Fleet while they aim and fire at you.\n"
        "White pegs are misses, red bursts hits; a sunk ship shows. The keys switch the view any time."},
    {"The Computer",
        "Easy fires at random, and after a hit tries the squares around it.\n"
        "Medium fires on a spaced pattern and follows a line of hits.\n"
        "Hard works out every way the ships left could lie and fires where most of them cross."},
    {"New Games", kHelpTwoPlayer},
    {"Wireless", kHelpWireless},
};

} // namespace

CYD_HELP(sunk, kPages)
