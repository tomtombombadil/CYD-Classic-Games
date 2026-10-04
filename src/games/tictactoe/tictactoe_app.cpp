// Tic-Tac-Toe's registry entry: save file, the rules hooked into the shared
// two-player controller (src/games/common/match.*), and the screen.
#include <cstdio>
#include <new>
#include "tictactoe_core.h"
#include "tictactoe_screen.h"
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "ui/shell.h"

namespace {

using tictactoe::Board;

constexpr const char* kId = "tictactoe";
Board* B = nullptr;                    // allocated while the game is open

int  result()           { return B->result(); }
int  turn()             { return B->turn(); }
int  moves()            { return B->moves; }
void play(int c)        { B->play(c); }
void reset()            { *B = Board{}; }
int  think(int level, uint32_t seed, volatile bool* stop)
{
    (void)stop;                          // small search (alpha-beta), nothing to stop
    return tictactoe::best_move(*B, tictactoe::depth_for_level(level), seed);
}

const match::Game kGame = {
    kId, "Tic-Tac-Toe", {"X", "O"},
    result, turn, moves, play, reset, think, tictactoe_ui::board_redraw,
};

constexpr size_t kSaveBytes = Board::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_moves = -1;

void save()
{
    if (!B || !ui::shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = B->serialize(buf, sizeof buf);
    match::save_state(buf + n, sizeof buf - n);
    ui::shell().save_game(kId, buf, sizeof buf);
}

bool load(Board& b)
{
    uint8_t buf[kSaveBytes];
    const ui::Shell& H = ui::shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return n == kSaveBytes && b.deserialize(buf, n) && match::load_state(buf + Board::kSaveBytes, match::kStateBytes);
}

void build()
{
    match::attach(kGame);
    tictactoe_ui::screen_build(*B);
    match::restart_view();
}

void open()
{
    B = new (std::nothrow) Board();
    if (!B) { ui::app_go_home(); return; }
    if (!load(*B)) { *B = Board{}; match::state() = match::State{}; }
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();                    // stops the computer first
    save();
    tictactoe_ui::screen_destroy();
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    // Save after every move, and every 30 s for the clock
    if (B && (B->moves != saved_moves || kit::save_due(now, last_save_ms, match::state().seconds))) {
        saved_moves = B->moves;
        last_save_ms = now;
        save();
    }
}

void restyle()
{
    match::detach();
    build();
}

bool summary(char* buf, size_t cap)
{
    if (B) { match::summary(buf, cap); return true; }
    uint8_t img[kSaveBytes];
    const ui::Shell& H = ui::shell();
    Board b;
    match::State st;
    if (!H.load_game || H.load_game(kId, img, sizeof img) != kSaveBytes || !b.deserialize(img, kSaveBytes)
        || !match::read_state(img + Board::kSaveBytes, match::kStateBytes, st))
        return false;
    match::describe(st, b.result(), b.moves, kGame.sides, buf, cap);
    return true;
}

void save_now() { save(); }

} // namespace

namespace games {
extern const GameOps tictactoe_ops;
const GameOps tictactoe_ops = {open, close, save_now, tick, restyle, summary, tictactoe_ui::icon};
} // namespace games
