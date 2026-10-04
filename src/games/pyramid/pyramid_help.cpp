// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Take the pyramid apart. Remove two cards that add up to 13: Ace = 1, Jack = 11, Queen = 12. A King is 13 by itself and goes as soon as you tap it.\n"
        "Only uncovered cards can go: a card is uncovered once both cards resting on it from below are gone."},
    {"Playing",
        "Tap a card (amber edge), then its partner. The top card of the waste (next to the stock) can pair too.\n"
        "Tap the stock to turn its next card onto the waste. You can go through the stock three times.\n"
        "Undo takes back moves; Hint shows a pair, a King or the stock."},
};

} // namespace

CYD_HELP(pyramid, kPages)
