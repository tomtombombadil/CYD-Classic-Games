// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "18 squares ring the board. Each keeps changing between money, money plus one more spin, and a Gremlin.\n"
        "Tap Spin and a light jumps round the squares. Tap STOP! and you win whatever the lit square shows. The most money after two rounds wins."},
    {"Gremlins",
        "A Gremlin takes all the money you have. A fourth Gremlin puts you out of the game.\n"
        "The board in round 2 pays more - and has more Gremlins."},
    {"Spins And Passing",
        "Everyone has 3 spins in round 1 and 4 in round 2; money with +1 Spin gives you one more. The player with the least money goes first.\n"
        "Pass gives the spins you have left to the leader, who then has to take them - a way to keep your money safe. Spins passed to you can't be passed on."},
    {"Max And Zoe",
        "The computer players spin, stop after a moment and pass when it suits them.\n"
        "Stats keep your place and money for every game."},
};

} // namespace

CYD_HELP(presscyd, kPages)
