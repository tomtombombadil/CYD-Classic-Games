// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "You against Max and Zoe. The board has six categories (one per row) of five clues each, worth $200 to $1,000 - the bigger the value, the harder the question.\n"
        "Two rounds (the second pays double), then a Final clue. Most money wins."},
    {"Buzzing In",
        "Whoever has control picks a value. Then everyone may answer: tapping an answer IS buzzing in, so be quick - Max and Zoe buzz in too.\n"
        "Right: you win the value and pick next. Wrong: you lose the value, and the others may still try. The bar shows the time left."},
    {"Daily Doubles",
        "One value in round 1 and two in round 2 hide a Daily Double: only the picker answers, for a wager - the least, half, all, or the clue's value. You may bet up to your money, or the round's top value if you have less."},
    {"Final",
        "Everyone with money bets (nothing, a quarter, half or all), then answers one hard question. Right adds the bet, wrong takes it off.\n"
        "Computer: Easy knows less and buzzes slower; Hard knows more and is quick. Questions: Open Trivia DB (CC BY-SA 4.0)."},
};

} // namespace

CYD_HELP(jeoparcyd, kPages)
