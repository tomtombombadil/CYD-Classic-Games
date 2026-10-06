// Press Your CYD rules and computer players, written for this project
// (MIT). Plain C++, host-tested. (The classic stop-the-light game show; our
// own name, and our own bad square, the Gremlin - Tom, 2026-10-06.)
//
// 18 squares round the board, each showing one of three things that keep
// changing: an amount of money, money plus one more spin, or a Gremlin.
// A light jumps round the squares; STOP takes whatever square it is on.
// A Gremlin takes all the money you have, and your fourth Gremlin puts you
// out of the game.
//
// Three players (you, Max and Zoe). Two rounds: each player has 3 spins in
// round 1 and 4 in round 2 (round 2's board pays more and has more
// Gremlins). The player with the least money goes first and spins until
// out of spins - or passes the spins left to the leader, who then has to
// take them (spins passed to you can't be passed on; a Gremlin turns any
// passed spins left into your own). The most money at the end wins.
#pragma once

#include <cstddef>
#include <cstdint>

namespace presscyd {

constexpr int kSquares = 18, kSlots = 3, kPlayers = 3, kRounds = 2, kOut = 4;
constexpr int kSpins[kRounds] = {3, 4};

enum Kind : uint8_t { kMoney = 0, kMoneySpin = 1, kGremlin = 2 };
struct Slot { uint8_t kind = kMoney; uint16_t dollars = 0; };

enum class Phase : uint8_t { Ready, Spinning, RoundOver, Over };

struct Player {
    int32_t money = 0;
    uint8_t earned = 0, passed = 0;     // spins: your own / passed to you
    uint8_t gremlins = 0;
    bool    out() const { return gremlins >= kOut; }
    int     spins() const { return earned + passed; }
};

struct Landing { int square = -1; Slot slot; };

struct Game {
    Slot     board[kSquares][kSlots];
    Player   p[kPlayers];
    uint8_t  round = 0;
    uint8_t  turn = 0;                  // the player to spin
    Phase    phase = Phase::Ready;
    Landing  last;                      // what the last stop landed on
    int8_t   last_player = -1;
    uint8_t  passed_to = 0xFF;          // the last pass: to whom (for the screen)
    uint32_t rng = 1;

    void start(uint32_t seed);
    void next_round();                  // RoundOver -> the next round's board and spins
    // Ready: spin (the light starts), or pass the spins left to the leader
    bool can_pass() const;              // own spins left, someone else ahead
    int  pass_target() const;           // the player who would get them
    bool spin();
    bool pass();
    // Spinning: stop with the light on `square` showing slot `slot`
    bool stop(int square, int slot);
    int  leader() const;                // most money (-1: a tie at the top)
    void ranking(uint8_t* order) const;

    // The light and the changing squares, for the screen (and the computer's stop)
    uint32_t rand_next();

    // Computer: pass instead of spinning? (level 0 never; 1 with $3,000 or more and
    // two spins or fewer; 2 when a spin risks more than it brings). The game is
    // mostly luck - in tests level 2 won no more often than level 0 - so the
    // screen's computers all play level 2 and there are no levels to pick.
    bool ai_pass(int level) const;

    size_t serialize(uint8_t* buf, size_t cap) const;    // "PYC1" + state
    bool   deserialize(const uint8_t* buf, size_t len);
    static constexpr size_t kSaveBytes = 4 + kSquares * kSlots * 2 + kPlayers * 7 + 1 + 1 + 1 + 1 + 3 + 1 + 4;

private:
    void make_board();
    void after_spin();
    void next_turn();
};

// Chance of a Gremlin when the light stops anywhere on this board
float gremlin_odds(const Game& g);

// ---- Play history ----------------------------------------------------------------------
//   #,Place,Money,Seconds,Time
//   4,1,12750,402,6:42
struct Record {
    uint8_t  place = 0;            // 1 = won (a tie at the top counts as 1)
    int32_t  money = 0;
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

} // namespace presscyd
