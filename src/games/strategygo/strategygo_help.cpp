// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Two armies of 40 hidden pieces. Capture the other side's Flag - or leave them with no piece that can move - to win.\n"
        "You see your own ranks; theirs show only after a battle. A dot on one of theirs means it has moved, so it's no Bomb or Flag. Red moves first."},
    {"Setting Up",
        "Your army starts laid out for you in your four rows. Tap two of your pieces to swap them; Shuffle deals a new layout. Tap Ready when you like it.\n"
        "Keep the Flag safe, often on the back row behind Bombs."},
    {"Moving",
        "Tap one of your pieces, then a lit square: one square across or up and down, never into the lakes.\n"
        "Scouts (2) go any distance in a straight line. Bombs and the Flag never move.\n"
        "A piece may not go back and forth between the same two squares more than five times in a row."},
    {"Battles",
        "Move onto an enemy piece to fight: both are shown, the higher rank wins, equal ranks both go.\n"
        "\xE2\x80\xA2  The Spy (S) beats the Marshal (10) - only when the Spy strikes.\n"
        "\xE2\x80\xA2  A Miner (3) clears a Bomb; anything else that hits a Bomb is lost.\n"
        "The Pieces key lists what each side has lost."},
    {"The Pieces",
        "Marshal 10 (1), General 9 (1), Colonel 8 (2), Major 7 (3), Captain 6 (4), Lieutenant 5 (4), Sergeant 4 (4), Miner 3 (5), Scout 2 (8), Spy S (1), Bomb B (6), Flag F (1).\n"
        "The computer sees only what you would: pieces that have fought, and which ones have moved."},
};

} // namespace

CYD_HELP(strategygo, kPages)
