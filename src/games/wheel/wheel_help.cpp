// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "Solve the hidden phrase before the others. You play Max and Zoe (or a friend: Pass and Play). Three rounds, a new puzzle each; the category shows under the board.\n"
        "Most money banked after three rounds wins."},
    {"A Turn",
        "\xE2\x80\xA2  Spin: the wheel stops on a money wedge - pick a consonant; each one in the puzzle pays that much and you go again. Not there: the next player's turn.\n"
        "\xE2\x80\xA2  Vowel: buy a vowel for $250.\n"
        "\xE2\x80\xA2  Solve: fill in the blanks."},
    {"The Wheel",
        "BUST: you lose this round's money and your turn.\n"
        "SKIP: you lose your turn.\n"
        "Solve the puzzle to bank your round money (at least $500). The others lose theirs. Letters already called are grey."},
    {"Solving",
        "Tap Solve, then tap letters: they fill the blanks in reading order (the gold tile is next). Delete takes one back; Cancel goes back to your turn.\n"
        "Right wins the round; wrong passes the turn.\n"
        "Computer: Easy solves late, Hard knows lots of puzzles and solves early."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(wheel, kPages)
