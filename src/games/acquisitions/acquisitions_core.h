// Acquisitions rules and computer players, written for this project (MIT).
// Plain C++, host-tested in tools/host_tests/test_games.cpp. (The classic
// hotel-chain game; our own name and hotel names - Tom, 2026-10-06.)
//
// A board of 12 x 9 squares, 1A to 12I. Four players (you + three
// computers), $6000 each, six tiles in hand. A turn: lay one tile, buy up
// to three shares in hotel chains on the board, draw a tile.
//   - A tile next to loose tiles (and no chain) founds a chain: the player
//     picks one of the seven not on the board and gets a free share.
//   - A tile next to one chain grows it (loose tiles touching join too).
//   - A tile touching two or more chains merges them: the biggest survives
//     (the player picks on a tie). Each smaller (defunct) chain pays its
//     two biggest shareholders a bonus (10 x and 5 x its share price; ties
//     share), then each holder, starting with the player, sells, trades two
//     for one in the survivor, or keeps its shares.
//   - A chain of 11 or more is safe: it can't be merged away. A tile that
//     would merge two safe chains is dead and is swapped for a new one; a
//     tile that would found an eighth chain waits in the hand.
// Share prices go by the chain's size and tier (cheap, middle, dear).
// The game can be ended by the player to move once a chain reaches 41
// tiles or every chain on the board is safe (or when no tile can be
// played any more): bonuses are paid for every chain, all shares are sold,
// and the most money wins.
#pragma once

#include <cstddef>
#include <cstdint>

namespace acq {

constexpr int kCols = 12, kRows = 9, kTiles = kCols * kRows;
constexpr int kChains = 7, kHand = 6, kPlayers = 4, kShares = 25, kStartCash = 6000;
constexpr int kSafe = 11, kEndSize = 41, kMaxBuy = 3;
constexpr uint8_t kNone = 0xFF;

const char* chain_name(int c);     // "Sunrise", "Oakwood", "Harbor", "Meadow", "Lagoon", "Royal", "Crimson"
char        chain_letter(int c);   // S O H M L R C
int         chain_tier(int c);     // 0 cheap (2), 1 middle (3), 2 dear (2)
// A tile's name: column 1-12 then row A-I ("5C")
void tile_name(int t, char* buf, size_t cap);
int  price_for(int c, int size);   // share price; 0 below 2 tiles

enum class Phase : uint8_t {
    Play,       // the player to move lays a tile
    Found,      // ... picks the chain to found (`pending` is the tile)
    Survivor,   // ... picks the surviving chain of a tied merger
    Dispose,    // `disposer` decides about its shares of the defunct chain
    Buy,        // the player to move buys up to 3 shares
    Over,
};

enum class TileState : uint8_t { Ok, Wait, Dead };   // Wait = would found an eighth chain

struct Player {
    int32_t cash = kStartCash;
    uint8_t shares[kChains] = {};
    uint8_t hand[kHand] = {kNone, kNone, kNone, kNone, kNone, kNone};
};

struct Game {
    uint8_t  board[kTiles] = {};        // 0 empty, 1 loose, 2 + c in chain c
    uint8_t  bag[kTiles] = {};
    uint8_t  bag_n = 0;
    Player   p[kPlayers];
    uint8_t  turn = 0;
    Phase    phase = Phase::Play;
    uint8_t  pending = kNone;           // the tile being laid (Found / Survivor / merging)
    uint8_t  survivor = kNone;
    uint8_t  defunct[4] = {kNone, kNone, kNone, kNone};
    uint8_t  defunct_size[4] = {};
    uint8_t  defunct_n = 0, defunct_i = 0;
    uint8_t  disposer = kNone;          // player deciding about defunct[defunct_i]
    uint8_t  bought = 0;                // shares bought this turn
    bool     end_called = false;
    uint8_t  last_tile = kNone;         // the last tile laid (shown on the board)
    uint16_t turns = 0;
    uint32_t rng = 1;

    void start(uint32_t seed);

    int  size(int c) const;
    bool active(int c) const { return size(c) > 0; }
    bool safe(int c) const { return size(c) >= kSafe; }
    int  price(int c) const { return price_for(c, size(c)); }
    int  bank(int c) const;             // shares left to buy
    int  actor() const;                 // the player who must act now
    TileState tile_state(int t) const;
    bool has_playable(int player) const;
    int32_t worth(int player) const;    // cash + shares at today's prices

    // ---- Actions (false = not allowed now)
    bool play(int hand_slot);           // Play: lay hand[hand_slot]
    bool skip_play();                   // Play with no playable tile: on to Buy
    bool found(int c);                  // Found
    bool choose_survivor(int c);        // Survivor
    bool dispose(int sell, int trade);  // Dispose: trade must be even
    bool buy(int c);                    // Buy: one share
    bool unbuy(int c);                  // Buy: give back a share bought this turn (`c` must be one)
    bool buy_done();                    // Buy: draw a tile, next player (or the end)
    bool can_end() const;               // the end may be called
    bool call_end();                    // ends after this turn's buying
    // Chains a tile would touch (and loose tiles it would join)
    int  touching(int t, uint8_t* chains) const;

    // ---- Computer players (level 0 easy .. 2 hard) - they decide only
    //      from what's on the table, never from other hands or the bag
    int  ai_tile(int level) const;      // a hand slot, or -1 to skip
    int  ai_found(int level) const;
    int  ai_survivor(int level) const;
    void ai_dispose(int level, int& sell, int& trade) const;
    int  ai_buy(int level) const;       // one share's chain, or -1 = done
    bool ai_end(int level) const;
    void ai_act(int level);             // one step for actor() (tests, simulations)

    // Standings at the end (or now): players by money, best first
    void ranking(uint8_t* order) const;
    int32_t final_money(int player) const;   // cash after bonuses and selling (as if it ended now)

    size_t serialize(uint8_t* buf, size_t cap) const;    // "ACQ1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kTiles + kTiles + 1 + kPlayers * (4 + kChains + kHand)
                                       + 1 + 1 + 1 + 1 + 4 + 4 + 1 + 1 + 1 + 1 + 1 + 1 + 2 + 4;

private:
    uint32_t rand_next();
    void draw_to_full(int player);
    void replace_dead(int player);
    void fill_from(int t, uint8_t value);       // loose tiles connected to t become `value`
    void begin_merge();
    void next_defunct();
    uint8_t first_holder(int c) const;
    void end_merge();
    void pay_bonuses(int c, int size);
    void begin_turn();
    void finish();
};

// ---- Play history ----------------------------------------------------------------------
//   #,Place,Money,Level,Seconds,Time
//   5,1,48200,Hard,2410,40:10
struct Record {
    uint8_t  place = 0;            // 1 = won
    int32_t  money = 0;
    uint8_t  level = 0;
    uint32_t seconds = 0;
};
extern const char* const kCsvHeader;
size_t format_body(char* buf, size_t cap, const Record& r);
bool   parse_line(const char* line, Record& out);

struct Summary {
    uint32_t games = 0, wins = 0;
    int32_t  best = 0;
    Record   recent[6];
    int      recent_n = 0, recent_head = 0;
    void add(const Record& r);
    const Record& newest(int i) const;
};

} // namespace acq
