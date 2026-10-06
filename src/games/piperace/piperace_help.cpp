// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Water is about to pour out of the tank. Lay pipe for it to run through - and stay ahead of it!\n"
        "Each level has a goal: the water must pass through that many pipes before it runs out of pipe. Reach the goal and the next level is faster and longer."},
    {"Laying Pipe",
        "The next five pieces show above the board; the one edged in gold comes first. Tap an empty square to lay it there.\n"
        "Tap a pipe the water hasn't reached to swap it for the next piece (it costs 50 points).\n"
        "The water can't go through rocks, off the board, or into a pipe that doesn't fit."},
    {"The Water",
        "The header counts down until the water starts; Water Now starts it at once.\n"
        "When the water runs out of pipe, the level ends. Fast Flow makes it rush on when you're done laying - every pipe it fills while fast scores double."},
    {"Score",
        "100 points a pipe filled, 200 while flowing fast. Water crossing a cross piece both ways: 400 more. A swapped pipe, or one laid but never reached when the level ends: minus 50.\n"
        "Stats keep every finished game: score, level and pipes."},
};

} // namespace

CYD_HELP(piperace, kPages)
