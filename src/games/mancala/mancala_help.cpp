// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "Kalah, the classic Mancala: six pits a side with four seeds each, and a store for each player.\n"
        "Your pits run down the left; your store is at the bottom of the middle. Most seeds in your store at the end wins."},
    {"A Move",
        "Tap one of your pits: its seeds are sown one at a time into the next pits - down your side, into your store, up the other side - skipping the other player's store.\n"
        "Last seed in your store: you go again.\n"
        "Last seed in an empty pit of yours: it and the seeds opposite go to your store."},
    {"The End",
        "When either side's pits are all empty, the game ends: the other player puts the seeds left on their side into their store.\n"
        "The pit last sown from is lit; your pits light up when it's your turn.\n"
        "vs Computer, the board turns so your pits are always on the left."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(mancala, kPages)
