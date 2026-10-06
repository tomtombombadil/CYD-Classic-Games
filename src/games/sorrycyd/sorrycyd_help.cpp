// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Race your four pawns out of Start, once round the board and into Home. All four Home wins.\n"
        "You are Red, at the bottom; Max, Zoe and Ada are Blue, Yellow and Green. Tap Draw Card (or the deck in the middle), then move as the card says."},
    {"The Cards",
        "1 or 2: start a pawn, or move. A 2 draws again.\n"
        "3, 5, 8, 12: move. 4: move 4 back.\n"
        "7: move 7, or split it over two pawns.\n"
        "10: move 10, or 1 back.\n"
        "11: move 11, or switch places with another pawn on the track.\n"
        "Sorry!: a pawn from Start takes another pawn's square."},
    {"Bumps And Slides",
        "Land on another colour's pawn and it goes back to its Start. You can't land on your own.\n"
        "Land on the first square of another colour's slide and you slide to its end, sending back every pawn on it - yours too.\n"
        "Only your colour goes into your Safety Zone; Home needs the exact count."},
    {"Your Move",
        "The pawns that can move have a gold ring: tap one, then a dot. A 7 split: tap the dot short of 7, then the other pawn goes the rest.\n"
        "Sorry! or a switch: the pawns you can take the place of get rings.\n"
        "No move fits: the turn is lost. A 4 back from near Start gets you close to Home!"},
    {"The Computer",
        "Easy moves its own pawns as far as it can.\n"
        "Medium also sends others back and keeps its pawns out of reach.\n"
        "Hard also thinks about its next card.\n"
        "The cards decide a lot - even Hard loses often. Stats keep your place and pawns Home."},
};

} // namespace

CYD_HELP(sorrycyd, kPages)
