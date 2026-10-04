// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Move every card onto the four foundations, one pile per suit, from Ace up to King.\n"
        "On the seven columns, cards go down in alternating colors: a red 6 on a black 7. Only a King goes into an empty column. A face-down card left on top turns up by itself."},
    {"Playing",
        "Tap a face-up card to pick it up (with the cards on it), then tap where it goes. Tap the picked card again to send it to a foundation, or else to a column that takes it.\n"
        "Tap the stock (the face-down pile, in the top corner on your stylus hand's side) to turn 1 or 3 cards; when it's empty, tap it to turn the waste back over."},
    {"Keys And Finish",
        "Undo takes back moves, Hint shows a good one (a green bar marks where it goes).\n"
        "Once every card is face up and the stock is empty, the rest go up by themselves and the cards bounce off the table. Tap to stop the show."},
    {"Options",
        "Draw 1 or Draw 3 (default).\n"
        "Standard scoring: +5 to a column from the waste, +10 to a foundation, +5 for turning a card up, -15 back off a foundation, a time bonus when you win.\n"
        "Vegas: a deal costs $52, each foundation card pays $5, the total carries on. Or no score."},
};

} // namespace

CYD_HELP(solitaire, kPages)
