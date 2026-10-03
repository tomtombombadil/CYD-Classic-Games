// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "No-limit Texas Hold'em: you against three computer players, Ada, Max and Zoe. Everyone starts with 1000 chips; blinds are 5 and 10.\n"
        "You get two cards. Five shared cards come face up in the middle: three (the flop), one (the turn), one (the river). Your best five of those seven cards wins the pot."},
    {"Betting",
        "A betting round comes before the flop and after each new card. On your turn:\n"
        "\xE2\x80\xA2  Fold: give up the hand\n"
        "\xE2\x80\xA2  Check or Call: match the bet\n"
        "\xE2\x80\xA2  Bet / Raise: the smallest raise; Pot: the size of the pot\n"
        "\xE2\x80\xA2  All In: all your chips\n"
        "All-ins make side pots: you win only what you matched."},
    {"Hands",
        "From best: Straight Flush, Four of a Kind, Full House, Flush, Straight, Three of a Kind, Two Pair, Pair, High Card.\n"
        "Your best hand so far shows next to your cards. At a showdown everyone still in shows their cards."},
    {"The Table",
        "The player to act has a gold edge; D is the dealer button, which moves each hand.\n"
        "A computer player who runs out buys back in. Run out yourself and New Chips gives you 1000 more.\n"
        "Levels: the computer weighs its chances more carefully on Medium and Hard, and plays its position."},
};

} // namespace

CYD_HELP(holdem, kPages)
