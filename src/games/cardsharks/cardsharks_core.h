// Card Sharks CYD rules and computer player, written for this project
// (MIT). Plain C++, host-tested. (The classic higher-or-lower card game
// show; Tom's name, 2026-10-06.)
//
// Two players, each with a row of five cards; only the first (the base)
// is face up. On your turn you call the next card in your row Higher or
// Lower than the one before it. Right: it stays and you call again - or
// Freeze, keeping your place, and the turn passes. Wrong (an equal card is
// wrong too): the cards since your last freeze are gone and the turn
// passes. Once a turn, before your first call, you may Change your base
// card for a fresh one. Aces are high. The first to turn over the fifth
// card wins the round; two rounds win the match. Rounds alternate who
// starts. One deck, reshuffled (without the cards on the table) when it
// runs out.
//
// Moves: 0 Higher, 1 Lower, 2 Freeze, 3 Change - for the side to move.
#pragma once

#include <cstddef>
#include <cstdint>

namespace csh {

constexpr int kRow = 5, kWinRounds = 2, kDeck = 52;
enum Move : int { kHigher = 0, kLower = 1, kFreeze = 2, kChange = 3 };
constexpr uint8_t kNoCard = 0xFF;

// Aces high: 2..14 (cards are 0..51 as in games/common/cards.h)
inline int value(uint8_t card) { const int r = card % 13 + 1; return r == 1 ? 14 : r; }

struct Row {
    uint8_t card[kRow] = {kNoCard, kNoCard, kNoCard, kNoCard, kNoCard};
    int8_t  pos = 0;        // the card showing now (0..4)
    int8_t  frozen = 0;     // where a miss sends you back to
};

enum class Last : uint8_t { None, Right, Wrong, Froze, Changed, RoundWon };

struct Board {
    Row      row[2];
    uint8_t  deck[kDeck] = {};
    uint8_t  deck_n = 0;
    uint8_t  turn_side = 0;
    uint8_t  wins[2] = {};
    uint8_t  round = 0;
    bool     changed = false;    // the side to move has changed its base this turn
    bool     called = false;     // ... has made a call this turn
    int8_t   winner = -1;
    Last     last = Last::None;  // what the last move did (for the screen)
    int8_t   last_side = -1;
    uint8_t  last_card = kNoCard;     // the card the last call turned over (shown after a miss)
    uint8_t  missed_at = 0;           // where it was
    uint16_t moves = 0;
    uint32_t rng = 1;

    void reset(uint32_t seed);
    int  turn() const { return turn_side; }
    int  result() const { return winner; }
    bool can_play(int m) const;
    bool play(int m);
    // The chance (0..1) that the next card is higher / lower than `card`,
    // from the cards not on the table (what either side can see)
    float chance(uint8_t card, bool higher) const;

    size_t serialize(uint8_t* buf, size_t cap) const;    // "CSC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 2 * (kRow + 2) + kDeck + 1 + 1 + 2 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 2 + 4;

private:
    uint8_t draw();
    void    new_round();
    void    shuffle_in();
};

// Computer: the move for the side to move. Level 0 calls by the card alone
// (2-7 higher, 9-A lower), freezes after two right; 1 also changes a middle
// base card and freezes on a risky call; 2 counts the cards still unseen and
// takes chances when the other side is about to win.
int best_move(const Board& b, int level, uint32_t seed);

} // namespace csh
