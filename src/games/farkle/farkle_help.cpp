// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "A push-your-luck dice game for two: you against the computer, or pass-and-play. First to 10,000 wins; the other player then gets one last turn to beat it.\n"
        "Roll six dice, set aside the ones that score, then roll the rest again or bank the points."},
    {"A Turn",
        "Tap scoring dice to set them aside (they turn gold); the Bank key shows what you'd bank.\n"
        "Roll throws the dice left. Each roll must score something, or it's a Farkle: the turn's points are lost.\n"
        "Set all six aside and you roll all six again (hot dice)."},
    {"Scoring",
        "\xE2\x80\xA2  A 1 = 100, a 5 = 50\n"
        "\xE2\x80\xA2  Three of a kind = 100 x the number (three 1s = 1000)\n"
        "\xE2\x80\xA2  Four of a kind 1000, five 2000, six 3000\n"
        "\xE2\x80\xA2  1-2-3-4-5-6 = 1500, three pairs = 1500\n"
        "\xE2\x80\xA2  Four of a kind and a pair = 1500, two triples = 2500\n"
        "Dice count only in the roll they came up in."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(farkle, kPages)
