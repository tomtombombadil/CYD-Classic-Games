// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "A dice roller for tabletop role-playing games. Every die: Coin, d4, d6, d8, d10, d12, d20 and d100 (two d10s: tens and ones).\n"
        "The last roll is drawn as dice, each showing its number, with the total. A natural 20 glows gold, a natural 1 red."},
    {"Rolling",
        "Tap dice keys to build a roll: d6 three times and d8 once makes 3d6 + 1d8. -1 and +1 set a modifier.\n"
        "Roll rolls it; Roll again repeats it. After a roll, the next die you tap starts a new one. Clear empties it."},
    {"Presets",
        "A preset rolls up to four lines at once, each a label (Hit, Damage...), dice and a modifier: a fighter's two attacks are Hit d20+7, Damage d8+5, Hit d20+4, Damage d6+5.\n"
        "Presets: tap one to roll it. Edit Presets: change a name, lines, dice and modifiers. + New makes one."},
    {"History",
        "History lists every roll, newest first: the dice, each one's number and the total. < > turn the pages.\n"
        "Clear History empties it. Rolls, presets and history are saved on the board."},
};

} // namespace

CYD_HELP(rpgdice, kPages)
