// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Score as many points as you can with five dice in 13 turns.\n"
        "Each turn: tap Roll, then tap dice to hold them (they turn gold) and roll the rest again, up to three rolls. Then tap an empty box on the score card to score that roll. Every box is used once; used boxes turn blue."},
    {"Upper Boxes",
        "Ones to Sixes score the total of the matching dice: three 4s in Fours = 12.\n"
        "If the upper boxes add up to 63 or more (three of each number), you earn a 35 point bonus.\n"
        "After each roll, empty boxes show what they would score."},
    {"Lower Boxes",
        "\xE2\x80\xA2  3 / 4 of a Kind: total of all dice\n"
        "\xE2\x80\xA2  Full House (3 + 2 alike): 25\n"
        "\xE2\x80\xA2  Run of 4 (four in a row): 30\n"
        "\xE2\x80\xA2  Run of 5: 40\n"
        "\xE2\x80\xA2  Yaht-CYD (five alike): 50\n"
        "\xE2\x80\xA2  Chance: total of all dice\n"
        "A box that doesn't fit scores 0."},
    {"Extra Yaht-CYDs",
        "Each extra Yaht-CYD after a scored 50 earns 100 more. It must go in its number's upper box if that's empty; otherwise in any box, and Full House and the runs then score in full.\n"
        "Stats keep every game's score, your best and average."},
};

} // namespace

CYD_HELP(yahtcyd, kPages)
