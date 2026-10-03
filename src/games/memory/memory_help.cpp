// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Every picture is on exactly two tiles, face down. Find all the pairs in as few turns as you can.\n"
        "Tap a tile to turn it over, then tap a second one. A matching pair stays face up with a green edge."},
    {"Misses",
        "Two different pictures stay up with a red edge so you can remember them. Your next tap turns them back over and turns the tile you tapped, so there is no waiting.\n"
        "Stats keep your best time and fewest turns for 4x4, 4x5 and 5x6."},
};

} // namespace

CYD_HELP(memory, kPages)
