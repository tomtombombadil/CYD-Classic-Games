// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Two decks in ten columns. Build runs from King down to Ace in one suit: a full run comes off by itself. Take off all eight runs to win.\n"
        "A card goes on any card one rank higher, any suit. But only cards in one suit going down by one move together, so same-suit builds are worth more."},
    {"Playing",
        "Tap a card in a column's top run: it and the cards on it are picked. Then tap the column they go to. Tap the picked card again to send it to the best column.\n"
        "An empty column takes any card or run.\n"
        "Tap the stock (a top corner) to deal one card onto every column; every column must have a card first."},
    {"Levels And Score",
        "1 Suit (all spades) is the gentle start; 2 Suits and 4 Suits get much harder.\n"
        "Score: 500 to start, 1 off for every move or deal, 100 for every run taken off.\n"
        "Undo takes back moves; Hint shows a good move or the stock."},
};

} // namespace

CYD_HELP(spider, kPages)
