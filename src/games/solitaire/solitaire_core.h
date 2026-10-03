// Solitaire (Klondike) rules, written for this project (MIT). Plain C++,
// host-tested in tools/host_tests/test_games.cpp.
//
// Seven columns (the tableau), dealt 1..7 cards with only the top card face
// up; the rest of the deck is the stock. Build the four foundations up by
// suit from Ace to King. On the tableau, cards go down in alternating
// colors; a run of face-up cards moves together; only a King goes into an
// empty column. Tapping the stock turns 1 or 3 cards onto the waste (an
// option, 3 by default); when the stock is empty the waste turns back over.
// A face-down card left on top of a column turns up by itself.
//
// Scoring (an option, like Windows):
//   Standard  waste->tableau +5, to a foundation +10, a card turned up +5,
//             foundation->tableau -15, turning the waste over -100 (Draw 1)
//             or -20 (Draw 3); never below 0; a time bonus when won.
//   Vegas     each deal costs $52, each card on a foundation pays $5; the
//             waste turns over once in Draw 1, twice in Draw 3. The balance
//             carries from game to game.
//   None      no score.
#pragma once

#include <cstddef>
#include <cstdint>

namespace solitaire {

enum Pile : uint8_t { Stock = 0, Waste = 1, Found0 = 2, Tab0 = 6, kPiles = 13 };
enum class Scoring : uint8_t { Standard = 0, Vegas = 1, None = 2 };

constexpr uint8_t kDown = 0x80;            // card byte: bit 7 = face down, low 6 bits = card
constexpr int kMaxPile = 24, kUndo = 200;

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
    uint32_t next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
};

struct Stack {
    uint8_t n = 0;
    uint8_t c[kMaxPile] = {};
    uint8_t top() const { return n ? c[n - 1] : 0xFF; }
};

struct Step {                               // one undoable step
    uint8_t from, to, count;
    uint8_t kind;                           // 0 move, 1 draw, 2 recycle
    uint8_t flipped;                        // the move turned up a card under it
    uint8_t passes;                         // recycles before this step
    int32_t score;                          // score before this step
};

struct Game {
    Stack    pile[kPiles];
    uint8_t  draw = 3;                      // 1 or 3
    Scoring  scoring = Scoring::Standard;
    uint32_t seed = 0;                      // the deal (for Restart)
    int32_t  score = 0;                     // Standard: points; Vegas: this deal's $ (starts -52)
    uint8_t  passes = 0;                    // times the waste was turned over
    uint16_t moves = 0;
    Step     undo_log[kUndo];
    uint16_t undo_n = 0;

    void deal(uint32_t seed, int draw, Scoring scoring);
    void restart() { deal(seed, draw, scoring); }

    static bool face_up(uint8_t c) { return !(c & kDown); }
    static uint8_t card(uint8_t c)  { return c & 0x3F; }
    int  first_up(int p) const;             // index of the first face-up card (n if none)

    // Stock: turn cards over (or turn the waste back); false if not allowed
    bool can_draw() const;
    bool draw_stock();
    // Move the cards from index `idx` up of pile `from` onto pile `to`
    bool can_move(int from, int idx, int to) const;
    bool move(int from, int idx, int to);
    // Where a tap on a card most usefully sends it: a foundation if it can
    // go, else the first column that takes it; -1 if nowhere
    int  best_target(int from, int idx) const;
    bool undo();
    bool won() const;
    bool can_finish() const;                // stock and waste empty, every card face up
    bool finish_step();                     // put one card on a foundation (auto-finish)
    // A useful move for Hint: from/idx/to, or from = Stock for "turn the stock"
    bool hint(int* from, int* idx, int* to) const;

    size_t serialize(uint8_t* buf, size_t cap) const;   // "SOL1" + piles + settings (no undo)
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kPiles * (1 + kMaxPile) + 1 + 1 + 4 + 4 + 1 + 2;
};

int rank(uint8_t c);                        // 1..13
int suit(uint8_t c);                        // 0..3
bool red(uint8_t c);

} // namespace solitaire
