// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Win a wedge in all six colours: Geography (blue), Entertainment (pink), History (yellow), Arts & Literature (brown), Science & Nature (green) and Sports & Leisure (orange).\n"
        "You against Max and Zoe, or 2-4 people (menu: Pass and Play)."},
    {"A Turn",
        "Roll, then tap one of the two framed squares (either way round). Answer a question in that square's colour.\n"
        "Right: roll again. Wrong: the next player's turn.\n"
        "A die square = roll again, no question."},
    {"Wedges",
        "Every sixth square is a colour's headquarters (a wedge in a white circle). Answer right there to win that colour's wedge for your pie in the middle.\n"
        "With all six, your next turn is one question in a random colour: get it right to win!"},
    {"The Computer",
        "Easy knows fewer answers and doesn't aim for wedges; Medium and Hard head for the wedges they need and know more.\n"
        "Questions: Open Trivia DB (CC BY-SA 4.0). Stats keep your place and wedges."},
};

} // namespace

CYD_HELP(trivialcyd, kPages)
