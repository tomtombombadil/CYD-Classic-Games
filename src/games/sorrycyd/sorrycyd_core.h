// Sorry-CYD! rules and computer players, written for this project (MIT).
// Plain C++, host-tested. (Tom's name, 2026-10-06, for the classic
// draw-a-card pawn race.)
//
// Four colours, four pawns each, a 60-square track round the board. Each
// colour starts in its Start circle, runs once round the track clockwise,
// turns into its own 5-square Safety Zone and finishes in Home. All four
// pawns Home wins. A turn: draw a card and move as it says:
//   1  start a pawn, or move 1      2  start a pawn, or move 2; draw again
//   3  move 3        4  move 4 BACK  5  move 5
//   7  move 7, or split it between two pawns     8  move 8
//   10 move 10, or 1 back
//   11 move 11, or switch places with another colour's pawn on the track
//      (if you can't move 11 you may also pass)
//   12 move 12      Sorry!  a pawn from Start takes another colour's pawn's
//                           square on the track; that pawn goes to its Start
// Landing on another colour's pawn sends it to its Start; you may not land
// on your own. Home needs the exact count. A slide of another colour:
// land on its first square and slide to its end, sending every pawn on it
// (yours too) to Start. If no move fits the card, the turn is lost.
//
// Deck: 45 cards (five 1s, four of each other card, four Sorry!),
// reshuffled when it runs out.
//
// Board: track square 0 is the top-left corner, numbered clockwise; side s
// (0 top, 1 right, 2 bottom, 3 left) is squares 15s..15s+14 and belongs to
// colour s (0 Red, 1 Blue, 2 Yellow, 3 Green). On its own side (r = 0..14)
// a colour's short slide runs r 1 -> 4, its long slide r 9 -> 13; its pawns
// leave Start onto r 4 and turn into Safety at r 2.
//
// A pawn's place is its progress: kStart, -1 (r 3, just behind its start
// square), 0..58 on the track from its start square, 59..63 Safety,
// kHome. Track square of progress p for colour c: (15c + 4 + p) mod 60.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sorry {

constexpr int kColors = 4, kPawns = 4, kTrack = 60, kDeck = 45;
constexpr int8_t kStart = -2, kHome = 64, kSafe0 = 59;
constexpr uint8_t kSorry = 13;     // card values: 1..12 as numbered, 13 = Sorry!

enum class Kind : uint8_t { Move, Split, Switch, Sorry, Pass };

struct Move {
    Kind   kind = Kind::Pass;
    int8_t pawn = -1, to = 0;      // Move: pawn -> progress `to`; Split: first part
    int8_t pawn2 = -1, to2 = 0;    // Split: the second pawn -> `to2`
    int8_t oc = -1, op = -1;       // Switch / Sorry: the other colour's pawn
};

enum class Phase : uint8_t { Draw, Play, Over };

struct Last {
    int8_t  color = -1;
    uint8_t card = 0;
    Kind    kind = Kind::Pass;
    int8_t  pawn = -1, pawn2 = -1;     // the pawns that moved
    uint16_t bumped = 0;               // pawns sent to Start (bit c*4+p)
    uint8_t slid = 0;                  // took a slide
    uint8_t home = 0;                  // a pawn reached Home
};

int  track_square(int color, int progress);     // -1 off the track
bool is_track(int progress);
int  slide_len(int square, int color);          // length if `square` starts another colour's slide, else 0
const char* card_text(uint8_t card);            // "Move 1 or start a pawn" ...

struct Game {
    int8_t   pos[kColors][kPawns];
    uint8_t  deck[kDeck] = {};
    uint8_t  deck_n = 0;
    uint8_t  card = 0;                 // the card drawn (0 = none yet)
    Phase    phase = Phase::Draw;
    uint8_t  turn = 0;
    int8_t   winner = -1;
    uint16_t turns = 0;
    uint32_t rng = 1;
    Last     last;

    Game();
    void start(uint32_t seed);
    uint32_t rand_next();
    uint8_t  draw();                   // Draw phase -> Play; returns the card
    // The moves the card allows (Play phase); 0 = none (the turn is lost)
    int  moves(Move* out, int cap) const;
    bool may_pass() const;             // an 11 with no 11 forward
    bool play(const Move& m);          // a listed move, or Pass when allowed / no move
    void lose_turn();                  // no move: next player
    int  home_count(int color) const;
    int  progress_sum(int color) const;
    bool occupied(int color, int pawn, int* oc, int* op) const;  // another pawn on its square

    // Computer: the move for the side to move (Play phase, at least one move).
    // Easy: the most progress for its own pawns. Medium: also sends the
    // others back and keeps its pawns out of reach. Hard: Medium, choosing
    // the move that leaves the best chances for its next card. (The cards
    // decide most games: Hard wins ~31 % against three Easy, not 25 %.)
    Move ai_move(int level);
    // Apply a move from moves() without checking it (the computer's look-ahead)
    bool apply(int c, const Move& m, Last* l);

    size_t serialize(uint8_t* buf, size_t cap) const;    // "SRY1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + 16 + kDeck + 1 + 1 + 1 + 1 + 1 + 2 + 4 + 9;

private:
    void shuffle();
    void next_turn();
    bool step(int c, int p, int to, Last* l);
};

// ---- Stats: "#,Place,Home,Level,Seconds,Time" ------------------------------------------------
struct Record {
    uint8_t  place = 0;     // 1 = won
    uint8_t  home = 0;      // your pawns Home
    uint8_t  level = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, wins = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace sorry
