// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "You against the dealer. Get closer to 21 than the dealer without going over.\n"
        "Number cards count their number, J Q K count 10, an Ace 1 or 11.\n"
        "An Ace and a 10-card as your first two cards is a blackjack: it pays 3 to 2."},
    {"A Round",
        "Bet with +5, +10, +25 (Clear starts over), then tap Deal.\n"
        "Hit takes a card, Stand keeps your total. Double doubles your bet for exactly one more card. Split makes two hands from a pair.\n"
        "The dealer turns the hidden card over and must draw to 17."},
    {"Chips",
        "You start with 500. A win pays your bet, a tie (push) gives it back.\n"
        "Run out and New Chips gives you another 500. New Game (in the menu) starts over at 500.\n"
        "Stats keep hands won, lost and pushed, blackjacks and your most chips."},
};

} // namespace

CYD_HELP(blackjack, kPages)
