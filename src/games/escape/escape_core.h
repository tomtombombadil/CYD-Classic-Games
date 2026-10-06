// Escape from CYD rules and computer players, written for this project
// (MIT). Plain C++, host-tested. (Tom's name, 2026-10-06, for the classic
// sinking-island escape game.)
//
// A hex sea, 9 columns x 11 rows (pointy-top hexes, odd rows pushed half a
// hex right). In the middle an island of 37 tiles: an outer ring of 18
// beaches, 12 forests inside, 7 mountains in the centre. A one-hex safe
// island in each corner. Four colours, 9 explorers each, worth 1 1 1 2 2
// 3 3 4 5 (a value only its owner sees). Eight boats (3 seats) wait round
// the island; two sea serpents in the sea.
//
// Setup: the colours take turns putting an explorer on an empty tile, the
// highest values first, until all 36 are on the island.
// A turn:
//  1. Three moves. Each move is one step to a next hex:
//     - an explorer on land: to land (at most 3 on a tile), into the sea
//       (swimming; boarding a boat there if it has a seat), or onto a safe
//       island (saved);
//     - a swimmer: one step a turn - to sea, a boat or a safe island;
//     - an explorer in a boat: off it, to the sea or a safe island;
//     - a boat (empty, or with most of its explorers yours): one sea hex
//       with no boat and no creature; swimmers it reaches climb in.
//     No one moves into a creature's hex.
//  2. Sink one tile (beaches first, then forests, then mountains; it must
//     touch the sea). Explorers on it start swimming. Its underside: a
//     Shark (eats the swimmers there), a Whale, a Boat (swimmers climb in),
//     a Whirlpool (swimmers and boats in it and the sea around are lost),
//     the Volcano (the game ends), or nothing.
//  3. Roll the creature die: Shark (moves up to 2), Whale (3) or Sea
//     Serpent (1). You may move one of that kind through the sea. A shark
//     eats the swimmers where it stops; a whale tips over a boat (its
//     explorers swim); a serpent eats swimmers and a boat with all on it.
// The game ends when the Volcano erupts (or no explorer is left on the
// board). Score = the values of your saved explorers.
#pragma once

#include <cstddef>
#include <cstdint>

namespace escape {

constexpr int kCols = 9, kRows = 11, kHexes = kCols * kRows;
constexpr int kColors = 4, kPer = 9, kExplorers = kColors * kPer;
constexpr int kMaxBoats = 24, kMaxCreatures = 32, kSeats = 3, kStack = 3, kMoves = 3;
constexpr uint8_t kValues[kPer] = {5, 4, 3, 3, 2, 2, 1, 1, 1};   // placing order: best first

enum Terrain : uint8_t { kSea, kBeach, kForest, kMountain, kSafe };
enum Effect : uint8_t { kNothing, kSharkTile, kWhaleTile, kBoatTile, kWhirlpool, kVolcano };
enum Kind : uint8_t { kShark, kWhale, kSerpent };
enum Where : uint8_t { kUnplaced, kLand, kSwim, kAboard, kSaved, kLost };

struct Explorer {
    uint8_t color = 0, value = 1;
    int8_t  hex = -1;
    Where   where = kUnplaced;
    int8_t  boat = -1;
};

struct Creature { Kind kind = kShark; int8_t hex = -1; };

enum class Phase : uint8_t { Place, Move, Sink, Creature, Over };

enum Act : uint8_t { kPlace, kStepExplorer, kStepBoat, kEndMoves, kSinkTile, kMoveCreature, kSkipCreature };
struct Action {
    Act    act = kEndMoves;
    int8_t who = -1;       // explorer / boat / creature index (kPlace: unused)
    int8_t to = -1;        // hex
};

// What the last sink / creature move did (for the screen)
struct News {
    int8_t  color = -1;
    int8_t  sunk = -1;         // the hex sunk
    Effect  effect = kNothing;
    int8_t  creature = -1;     // the creature moved / appeared
    uint8_t lost = 0;          // explorers lost to the tile (shark, whirlpool)
    uint8_t tipped = 0;        // boats lost to the tile
    uint8_t c_lost = 0;        // explorers the creature ate
    uint8_t c_tipped = 0;      // boats it tipped over / sank
};

int  hex_at(int col, int row);            // -1 off the board
int  col_of(int h);
int  row_of(int h);
int  neighbours(int h, int8_t* out);      // up to 6
int  distance(int a, int b);
const char* kind_name(Kind k);
int  kind_range(Kind k);

struct Game {
    Terrain  terrain[kHexes];
    Effect   under[kHexes];           // island tiles' undersides (hidden)
    Explorer ex[kExplorers];
    int8_t   boat[kMaxBoats];         // hex, -1 = none
    uint8_t  boats = 0;
    Creature cr[kMaxCreatures];
    uint8_t  creatures = 0;
    Phase    phase = Phase::Place;
    uint8_t  turn = 0;
    uint8_t  moves_left = 0;
    uint8_t  die = 0;                 // the creature rolled (Kind) in the Creature phase
    uint8_t  placed = 0;              // explorers placed so far
    uint64_t swam = 0;                // swimmers that have taken their one step this turn
    uint16_t turns = 0;
    uint32_t rng = 1;
    News     news;

    Game();
    void start(uint32_t seed);
    uint32_t rand_next();

    // Board questions
    int  land_count(int h) const;      // explorers standing on a tile
    int  swim_count(int h) const;
    int  boat_at(int h) const;         // -1 none
    int  aboard(int b) const;
    int  creature_at(int h) const;     // -1 none
    bool controls(int c, int b) const; // may colour c move boat b
    bool island(int h) const { return terrain[h] >= kBeach && terrain[h] <= kMountain; }
    int  next_to_place() const;        // explorer index, -1 when all placed
    int  score(int c) const;           // saved values
    int  on_board(int c) const;        // explorers still on the board (land, sea, boats)
    int  winner() const;               // -1 tie / not over

    int  actions(Action* out, int cap) const;     // the legal actions now
    bool can(const Action& a) const;
    bool apply(const Action& a);                  // a legal action
    void roll();                                  // Creature phase: roll the die (sets die)

    // Computer: the action for the colour to move. Levels: Easy steps its
    // own explorers toward safety one move at a time; Medium also weighs
    // the creatures and hurts the others; Hard looks two moves ahead.
    Action ai(int level);

    size_t serialize(uint8_t* buf, size_t cap) const;    // "ESC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kHexes * 2 + kExplorers * 5 + kMaxBoats + 1 +
                                         kMaxCreatures * 2 + 1 + 5 + 8 + 2 + 4 + 8;

private:
    void sink(int h);
    void creature_effect(int k);
    void lose(int e, uint8_t* count);
    void end_turn();
    void check_over();
    int  sinkable(int8_t* out) const;
    int  creature_targets(int k, int8_t* out) const;
};

// ---- Stats: "#,Place,Saved,Level,Seconds,Time" -----------------------------------------------
struct Record {
    uint8_t  place = 0;
    uint8_t  saved = 0;
    uint8_t  level = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, wins = 0;
    uint8_t  best = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace escape
