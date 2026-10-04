// Reversi: registry entry, save file and screen. The rules live in
// reversi_core.*, turns / computer / menus in the shared match controller,
// the board in board8. Legal moves for the player are shown as dots; tap
// one to play it.
#include <cstdio>
#include <new>
#include "games/common/board8.h"
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "reversi_core.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using reversi::Board;
constexpr const char* kId = "reversi";
Board* B = nullptr;

// ---- Rules for the match controller ---------------------------------------------
int  result() { return B->result(); }
int  turn()   { return B->side; }
int  moves()  { return B->moves(); }
void reset()  { *B = Board{}; }
void redraw();
void play(int sq) { B->play(sq); redraw(); }
int  think(int level, uint32_t seed, volatile bool* stop) { return reversi::best_move(*B, level, seed, stop); }
void score(char* buf, size_t cap) { snprintf(buf, cap, "Black %d  White %d", B->count(0), B->count(1)); }
void note(char* buf, size_t cap)
{
    if (B->last_was_pass() && !B->over())
        snprintf(buf, cap, "%s had no move", B->side == 0 ? "White" : "Black");
    else buf[0] = 0;
}

match::Game make_game()
{
    match::Game g{kId, "Reversi", {"Black", "White"}, result, turn, moves, play, reset, think, redraw};
    g.score = score;
    g.note = note;
    g.legal = [](int sq) { return B->can_play(sq); };
    return g;
}

// ---- Board -------------------------------------------------------------------------------
void draw_piece(lv_layer_t* layer, int sq, int cx, int cy, int size)
{
    // board8 counts rank 0 at the bottom; Reversi rows count from the top
    const int rsq = (7 - sq / 8) * 8 + sq % 8;
    const int who = B ? B->cell(rsq) : -1;
    if (who < 0) return;
    const ui::Palette& P = ui::pal();
    const int r = size * 40 / 100;
    kit::fill_circle(layer, cx, cy, r + 1, who == 0 ? P.stone_light : P.stone_dark);
    kit::fill_circle(layer, cx, cy, r, who == 0 ? P.stone_dark : P.stone_light);
}

int to_board8(int rsq) { return (7 - rsq / 8) * 8 + rsq % 8; }

void redraw()
{
    if (!B) return;
    board8::Marks m;
    if (match::human_may_move())
        for (uint64_t l = B->legal(); l; l &= l - 1) m.targets |= board8::bit(to_board8(__builtin_ctzll(l)));
    const int last = B->last_move();
    if (last >= 0) m.last = board8::bit(to_board8(last));
    board8::set_marks(m);
}

void on_tap(int sq8)
{
    const int rsq = to_board8(sq8);           // the mapping is its own inverse
    if (B && B->can_play(rsq)) match::human_move(rsq);
}

constexpr size_t kSaveBytes = Board::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_plies = -1;

void save()
{
    if (!B || !ui::shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = B->serialize(buf, sizeof buf);
    match::save_state(buf + n, sizeof buf - n);
    ui::shell().save_game(kId, buf, sizeof buf);
}

bool load(Board& b, match::State& st)
{
    uint8_t buf[kSaveBytes];
    const ui::Shell& H = ui::shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return n == kSaveBytes && b.deserialize(buf, n) && match::read_state(buf + Board::kSaveBytes, match::kStateBytes, st);
}

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const ui::Metrics& m = ui::metrics();
    const int margin = m.large ? 4 : 2;
    board8::Config cfg;
    cfg.style = board8::Style::Felt;
    cfg.draw_piece = draw_piece;
    cfg.on_tap = on_tap;
    board8::create(lv_screen_active(), margin, top, m.w - 2 * margin, bottom - top, cfg);
    match::restart_view();
    redraw();
}

// ---- Registry entry ------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    if (!B) { ui::app_go_home(); return; }
    if (!load(*B, match::state())) { *B = Board{}; match::state() = match::State{}; }
    saved_plies = B->plies;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    save();
    match::closed();
    board8::forget();
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (B && (B->plies != saved_plies || kit::save_due(now, last_save_ms, match::state().seconds))) {
        saved_plies = B->plies;
        last_save_ms = now;
        save();
        redraw();                                  // dots follow whose turn it is
    }
}

void restyle() { if (B) { match::detach(); board8::forget(); build(); } }

bool summary(char* buf, size_t cap)
{
    if (B) { match::summary(buf, cap); return true; }
    Board b;
    match::State st;
    if (!load(b, st)) return false;
    match::describe(st, b.result(), b.moves(), {"Black", "White"}, buf, cap);
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    const int size = lv_area_get_width(&a), c = size / 4;
    const int ox = a.x1 + (size - 4 * c) / 2, oy = a.y1 + (size - 4 * c) / 2;
    kit::fill_rect(layer, ox, oy, ox + 4 * c - 1, oy + 4 * c - 1, P.felt, c / 3);
    static const int8_t art[16] = {-1, -1, -1, -1, -1, 1, 0, -1, -1, 0, 1, 0, -1, -1, -1, -1};
    for (int i = 0; i < 16; ++i) {
        if (art[i] < 0) continue;
        const int cx = ox + (i % 4) * c + c / 2, cy = oy + (i / 4) * c + c / 2, r = c * 40 / 100;
        kit::fill_circle(layer, cx, cy, r + 1, art[i] == 0 ? P.stone_light : P.stone_dark);
        kit::fill_circle(layer, cx, cy, r, art[i] == 0 ? P.stone_dark : P.stone_light);
    }
}

void icon(lv_obj_t* parent, int size)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_center(o);
    lv_obj_set_clickable(o, false);
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
}

} // namespace

namespace games {
extern const GameOps reversi_ops;
const GameOps reversi_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
