// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "The island is sinking! Each colour has 9 explorers worth 1 to 5 (only you see your values). Get them to the safe islands in the corners - by land, swimming or by boat.\n"
        "When the Volcano erupts the game ends: the most points saved wins."},
    {"A Turn",
        "1. Three moves: an explorer one hex (land, sea, boat or a safe island), or a boat one hex. A swimmer moves once a turn.\n"
        "2. Sink a tile: beaches first, then forests, then mountains.\n"
        "3. The creature die: move one Shark (2 hexes), Whale (3) or Sea Serpent (1) - or Skip."},
    {"Under The Tiles",
        "A sunk tile can hide a Shark (it eats the swimmers there), a Whale, a Boat (swimmers climb in), a Whirlpool (swimmers and boats in and around it are lost) - or the Volcano.\n"
        "Sharks eat swimmers, whales tip boats over, sea serpents eat swimmers and boats."},
    {"Boats",
        "A boat has 3 seats. You may move an empty boat, or one where most of the explorers are yours.\n"
        "No one moves into a creature's hex. At most 3 explorers stand on a tile or swim in a hex."},
    {"On The Screen",
        "Tap a hex with your piece: your boat first, then your explorers, best first - tap again for the next. Then tap a framed hex.\n"
        "Red frames: tiles you may sink. A white ring = swimming.\n"
        "Computer: Easy only runs; Medium also uses the creatures; Hard looks 2 moves ahead."},
};

} // namespace

CYD_HELP(escape, kPages)
