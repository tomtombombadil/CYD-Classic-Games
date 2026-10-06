// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "Tic-tac-toe with nine stars. On your turn tap a star: they're asked a question and give an answer - but stars sometimes bluff!\n"
        "Agree or Disagree. Judge right and the square is yours. Judge wrong and it goes to the other player."},
    {"Winning",
        "Three in a row wins - or any five squares.\n"
        "But a winning square must be earned: if judging wrong would hand the other player the win, the square stays open instead.\n"
        "X goes first."},
    {"The Stars",
        "Stars give the right answer a bit more than half the time. When the question is hard, think twice before agreeing!\n"
        "Computer: Easy picks any star and knows less; Medium and Hard play tic-tac-toe well and know more. Questions: Open Trivia DB (CC BY-SA 4.0)."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(hollywood, kPages)
