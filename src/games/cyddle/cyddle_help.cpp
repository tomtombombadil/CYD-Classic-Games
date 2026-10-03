// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Find the hidden five-letter word.\n"
        "Type a guess on the keyboard and tap " "\xEF\x80\x8C" " (the check key). Each letter then turns:\n"
        "\xE2\x80\xA2  green: right letter, right spot\n"
        "\xE2\x80\xA2  gold: in the word, but elsewhere\n"
        "\xE2\x80\xA2  grey: not in the word"},
    {"Guessing",
        "Guesses must be real words. The keyboard colors each letter by what you've learned so far. The " "\xEF\x95\x9A" " key removes the last letter.\n"
        "When the game is over, " "\xEF\x80\x8C" " starts a new word."},
    {"Levels",
        "Easy: 7 guesses. Normal: 6 guesses.\n"
        "Hard: 6 guesses, and every green letter must stay in place and every gold letter must be used in later guesses.\n"
        "Stats keep wins, losses and guess counts for each level."},
};

} // namespace

CYD_HELP(cyddle, kPages)
