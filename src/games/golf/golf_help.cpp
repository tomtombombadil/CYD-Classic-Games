// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Clear all seven columns onto the waste pile (the face-up card below them).\n"
        "Tap a column to play its top card onto the waste: it must be one rank higher or lower than the waste card, any suit. A 6 goes on a 5 or a 7.\n"
        "Ace and King don't wrap round, and nothing goes on a King."},
    {"Playing",
        "Stuck? Tap the stock (the face-down pile) to turn its next card onto the waste.\n"
        "Undo takes back moves, even after you run out. Hint shows a column that can play, or the stock.\n"
        "The fewer cards left at the end, the better. Clear them all to win."},
};

} // namespace

CYD_HELP(golf, kPages)
