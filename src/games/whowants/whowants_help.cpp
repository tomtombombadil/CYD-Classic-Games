// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Fifteen questions climb a money ladder from $100 to $1,000,000. Questions 1-5 are easy, 6-10 medium, 11-15 hard. Tap an answer: it turns gold, then green if it's right - or red.\n"
        "$1,000 and $32,000 are safe: a wrong answer drops you back to the last safe amount you passed."},
    {"Lifelines",
        "Each once a game:\n"
        "50:50 takes away two wrong answers.\n"
        "Audience: the studio audience votes - their percents show on the answers. They know easy questions best.\n"
        "Phone: a friend says which answer they think it is, and how sure they are. Friends can be wrong!"},
    {"Walking Away",
        "Not sure? Walk Away (top right) and keep the money you've won so far.\n"
        "The menu's Money Ladder shows every prize and where you are.\n"
        "Questions don't repeat until you've played them all."},
    {"The Questions",
        "The questions come from Open Trivia DB (opentdb.com), shared under CC BY-SA 4.0, screened for young players and changed to plain text. Stats keep what you won and how far you got."},
};

} // namespace

CYD_HELP(whowants, kPages)
