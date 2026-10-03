// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "The classic Jacks or Better machine. Bet 1 to 5 credits and get five cards. Tap the cards you want to keep: they say HELD. Then Draw replaces the rest, once.\n"
        "Your final hand pays by the table at the top, times your bet. The hand you hold lights up."},
    {"Paying Hands",
        "\xE2\x80\xA2  Jacks or Better: a pair of Jacks, Queens, Kings or Aces\n"
        "\xE2\x80\xA2  Two Pair, Three of a Kind\n"
        "\xE2\x80\xA2  Straight: five in a row (A-2-3-4-5 and 10-J-Q-K-A count)\n"
        "\xE2\x80\xA2  Flush: five of one suit\n"
        "\xE2\x80\xA2  Full House, Four of a Kind, Straight Flush\n"
        "\xE2\x80\xA2  Royal Flush: 10 to Ace of one suit, 4000 at a 5 credit bet"},
    {"Keys",
        "Bet One adds a credit to the bet (1 to 5, then back to 1). Bet Max bets 5 and deals at once.\n"
        "Hint holds the cards the standard Jacks or Better strategy keeps: about the best play there is.\n"
        "You start with 500 credits; run out and New Credits gives you 500 more."},
};

} // namespace

CYD_HELP(vpoker, kPages)
