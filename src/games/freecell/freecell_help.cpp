// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Every card is face up from the start, and nearly every deal can be won.\n"
        "Move all the cards onto the foundations (top right: Spades, Hearts, Clubs, Diamonds), each from Ace to King.\n"
        "On the columns, cards go down in alternating colors. An empty column takes any card."},
    {"Free Cells",
        "The four free cells (top left) each hold one card while you dig out the one you need.\n"
        "A run of cards moves together if there's room: one card per empty free cell plus one, doubled for each empty column.\n"
        "Cards nothing else needs go up to the foundations by themselves."},
    {"Playing",
        "Tap a card (it turns gold), then tap where it goes: a column, the free-cell row, or the foundation row.\n"
        "Tap the picked card again to send it to its foundation, or else to a column or a free cell.\n"
        "Undo takes back moves; Hint shows one."},
};

} // namespace

CYD_HELP(freecell, kPages)
