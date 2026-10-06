// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "26 cases hide 26 amounts, from 1 cent to $1,000,000. Pick one to be yours - it stays shut until the end.\n"
        "Then open the other cases a few at a time. Every amount you open is one your case can't hold: it turns grey on the boards at the sides."},
    {"The Banker",
        "After each round the Banker calls with an offer for your case. Deal takes the money and ends the game. No Deal plays on.\n"
        "Rounds open 6, 5, 4, 3 and 2 cases, then one at a time. The offers grow as the game goes on - but so does the risk."},
    {"The End",
        "With one other case left, you may keep yours or swap it for that one, and you win what's inside.\n"
        "If you took a deal, you see what your own case held. Stats keep what you won, the round you dealt in and what your case held."},
};

} // namespace

CYD_HELP(dealcyd, kPages)
