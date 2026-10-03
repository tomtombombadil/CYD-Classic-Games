// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
#include "games/common/help_common.h"

namespace {

using games::HelpPage;
using games::kHelpTwoPlayer;

const HelpPage kPages[] = {
    {"Playing",
        "Tap one of your pieces: dots show where it can go. Then tap the square.\n"
        "A pawn reaching the far side asks what it becomes (Queen, Rook, Bishop, Knight).\n"
        "Long-press any piece, either side's, to see its moves; the next tap clears that."},
    {"The Rules",
        "White moves first. Castling, en passant and promotion all work. A king in check has its square tinted red; you must get it out of check.\n"
        "Checkmate wins. Stalemate, threefold repetition, 50 moves with no capture or pawn move, and too little material to mate are draws."},
    {"The Computer",
        "Easy looks 2 moves ahead. Medium looks 3, thinking up to 2 seconds. Hard looks up to 6, thinking up to 6 seconds a move.\n"
        "The screen stays usable while it thinks. Against the computer you and it take turns playing White."},
    {"New Games", kHelpTwoPlayer},
};

} // namespace

CYD_HELP(chess, kPages)
