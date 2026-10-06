// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"The Idea",
        "Each player has a row of five cards; only the first is face up. On your turn, call the next card Higher or Lower than the one before it.\n"
        "Turn over all five of your cards first to win the round. Two rounds win the game. Aces are high."},
    {"Calls And Misses",
        "Right: the card stays and you call again - or Freeze to keep your place and pass the turn.\n"
        "Wrong (the same rank is wrong too): every card since your last freeze goes and the turn passes. The card that beat you shows crossed out; a gold bar marks where you froze."},
    {"Change",
        "Once a turn, before your first call, Change swaps the card in play for a fresh one - handy on a 7, 8 or 9.\n"
        "The line in the middle says what the card in play is. The player who didn't start the last round starts the next one."},
    {"The Computer",
        "Easy calls by the card alone and freezes after two right.\n"
        "Medium also changes middle cards and freezes before a risky call.\n"
        "Hard counts the cards still unseen and takes chances when you are close to winning."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(cardsharks, kPages)
