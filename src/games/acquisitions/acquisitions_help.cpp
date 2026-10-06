// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"

namespace {

using games::HelpPage;

const HelpPage kPages[] = {
    {"The Idea",
        "Build hotel chains and buy their shares. The richest player when the game ends wins.\n"
        "You play Ada, Max and Zoe. Each turn: lay one of your six tiles on the board, buy up to three shares, then draw a new tile. Everyone starts with $6,000."},
    {"Laying Tiles",
        "Your tiles are the keys at the bottom; their squares are edged in gold on the board.\n"
        "A tile next to a loose tile founds a hotel: pick one of the seven, and get a free share.\n"
        "A tile next to a hotel makes it bigger. A grey key's tile can't go down now."},
    {"Mergers",
        "A tile that joins two hotels merges them: the bigger one takes over the smaller.\n"
        "The two biggest shareholders of the smaller hotel get bonuses (10 and 5 times its share price). Then each holder sells those shares, trades two for one share of the bigger hotel, or keeps them.\n"
        "A hotel of 11 or more tiles is safe: it can't be taken over."},
    {"Shares And Money",
        "After your tile, tap the hotel chips to buy shares (up to three); Undo gives one back. Done ends your turn.\n"
        "Share prices grow with a hotel's size. Sunrise and Oakwood are cheap, Royal and Crimson dear.\n"
        "Tap a chip at other times to see the Stocks page: who holds what."},
    {"The End",
        "Once a hotel has 41 tiles, or every hotel is safe, the player to move may end the game (End Game).\n"
        "Then every hotel pays its bonuses, all shares are sold, and the most money wins.\n"
        "Easy, Medium and Hard set how well Ada, Max and Zoe play. Stats keep your place and money."},
};

} // namespace

CYD_HELP(acquisitions, kPages)
