// The shared controller for two-player board games (FourConnect,
// Tic-Tac-Toe, and later Reversi, Checkers, Chess). The game supplies its
// rules through `Game` callbacks and draws its own board; this file runs
// the rest: whose turn it is, the computer opponent on its background task,
// pass-and-play, the top bar and status line, sounds, the win flash, the
// ☰ menu, stats, and the part of the save file that says how the game is
// being played.
//
// vs Computer: the player's side alternates each new game (whoever moves
// first in one game moves second in the next). Leaving a vs-computer game
// that has started for a new one counts as a loss.
//
// Wireless (CYD to CYD, a game that supplies `legal`): Play Nearby
// (nearby.*) pairs two boards; then each board plays its own copy of the
// game and the moves travel over the radio (net::Link in src/net/wireless.*).
// A move from the other board is checked with `legal` before it is played;
// one that isn't, or boards whose games differ, end the game unrecorded.
// No moves while the other board isn't heard ("Waiting for Bob..."). Play
// Again starts the next game once both players tapped it; the first mover
// alternates. Starting any other game ends the wireless one (the other
// board is told); Exit Game only pauses it - opening the game again on both
// boards carries on. The link's state is saved as "wl_<id>" beside the game.
#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>
#include "two_player.h"

namespace match {

struct Game {
    const char*      id;           // registry id (stats file)
    const char*      title;        // menu title
    twoplayer::Sides sides;        // e.g. {"Red", "Yellow"}; side 0 moves first
    int  (*result)();              // -1 game on, 0/1 that side won, 2 draw
    int  (*turn)();                // side to move
    int  (*moves)();               // moves played so far
    void (*play)(int move);        // make a legal move
    void (*reset)();               // empty board, side 0 to move
    // Computer's move for the side to move (runs on the AI task, must only
    // read the game's state). level 0..2.
    int  (*think)(int level, uint32_t seed, volatile bool* stop);
    void (*redraw)();              // board changed: invalidate the board view
    // Optional: a score for the info line, e.g. "Black 12  White 9"
    void (*score)(char* buf, size_t cap) = nullptr;
    // Optional: a note after the last move, e.g. "White had no move" (or "")
    void (*note)(char* buf, size_t cap) = nullptr;
    // Optional: true while the board is still showing the last move (an
    // animation); the computer waits for it before thinking or moving
    bool (*busy)() = nullptr;
    uint32_t ai_stack = 8192;      // the computer's task stack (bytes)
    // Wireless: is `move` legal for the side to move? nullptr = this game
    // has no wireless play.
    bool (*legal)(int move) = nullptr;
};

// Call when the game opens (after its board was loaded) and on restyle.
void attach(const Game& g);
// Top bar + bottom strip (status, play again). Returns the free area for
// the board: [top, bottom) in screen pixels.
void build_chrome(int* top, int* bottom);
void detach();                     // leaving or restyling: stop the computer, forget widgets
void closed();                     // the game closed (after its last save): pause a wireless game

// The player tapped a legal move on the board. Ignored unless a human may move.
void human_move(int move);
bool human_may_move();
void tick(uint32_t now_ms);
void open_menu();
void summary(char* buf, size_t cap);         // the attached game, for "Continue"
void restart_view();               // after attach + build: show state, maybe start the computer
void refresh();                    // redraw the status and info lines (e.g. a new note)
// The side this board's player has: vs Computer and Wireless 0/1, -1 in
// pass-and-play (both sides are at this board)
int  my_side();
// "Computer", the other board's player name, or nullptr (pass-and-play)
const char* opponent_name();

// How the game is being played (saved after the board)
struct State {
    twoplayer::Mode  mode       = twoplayer::Mode::Computer;
    twoplayer::Level level      = twoplayer::Level::Medium;
    uint8_t          human_side = 0;      // vs Computer: which side the player has
    uint8_t          recorded   = 0;      // finished game already in the stats
    uint8_t          human_moved = 0;     // vs Computer: the player has moved (else leaving isn't a loss)
    uint32_t         seconds    = 0;
};
State& state();
constexpr size_t kStateBytes = 8;
size_t save_state(uint8_t* buf, size_t cap);
bool   load_state(const uint8_t* buf, size_t len);
// Load just the state from a save image tail into `out` (for summaries of
// a game that isn't open), and describe a game from its parts.
bool   read_state(const uint8_t* buf, size_t len, State& out);
void   describe(const State& s, int result, int moves, const twoplayer::Sides& sides,
                char* buf, size_t cap);

} // namespace match
