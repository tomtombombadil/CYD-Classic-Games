// Ultimate Tic-Tac-Toe: screen and registry entry. Rules and the computer
// in ultimate_core.*; turns, menus, stats and wireless play come from the
// shared two-player controller (games/common/match.*).
//
// One square board as big as fits: nine small boards with wider gaps
// between them. The board(s) the player to move may play in are lit; a won
// board shows a big X or O; a full board with no winner is greyed. The
// last mark has a gold square behind it.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "ultimate_core.h"

namespace {

using namespace ultimate;
using namespace ui;

constexpr const char* kId = "ultimate";
const twoplayer::Sides kSides = {"X", "O"};

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
int cs = 24, gap = 6;                    // cell size, gap between small boards

// ---- Rules for the controller ---------------------------------------------------------------
int  result() { return B->result(); }
int  turn()   { return B->turn(); }
int  moves()  { return B->moves; }
void redraw() { if (board_obj) lv_obj_invalidate(board_obj); }
void play(int c) { B->play(c); redraw(); }
void reset() { *B = Board{}; }
int  think(int level, uint32_t seed, volatile bool* stop) { return best_move(*B, level, seed, stop); }

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (B->result() != -1 || !match::human_may_move()) return;
    if (B->moves == 0) snprintf(buf, cap, "X starts: play anywhere");
    else if (B->next < 0) snprintf(buf, cap, "Play in any open board");
    else {
        static const char* const kWhere[9] = {"top left", "top middle", "top right", "middle left", "centre",
                                              "middle right", "bottom left", "bottom middle", "bottom right"};
        snprintf(buf, cap, "Play in the %s board", kWhere[B->next]);
    }
}

void score(char* buf, size_t cap)
{
    int x = 0, o = 0;
    for (int b = 0; b < 9; ++b) { x += B->small[b] == 1; o += B->small[b] == 2; }
    snprintf(buf, cap, "Boards: X %d  O %d", x, o);
}

int list(int* out, int cap)
{
    uint8_t m[kCells];
    const int n = B->legal(m);
    int k = 0;
    for (; k < n && k < cap; ++k) out[k] = m[k];
    return k;
}

match::Game make_game()
{
    match::Game g{kId, "Ultimate Tic-Tac-Toe", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.ai_stack = 12 * 1024;
    g.legal = [](int c) { return B->can_play(c); };
    g.list = list;
    return g;
}

// ---- Board ------------------------------------------------------------------------------------
int cell_x(int c) { const int b = c / 9, k = c % 9; return (b % 3) * (3 * cs + gap) + (k % 3) * cs; }
int cell_y(int c) { const int b = c / 9, k = c % 9; return (b / 3) * (3 * cs + gap) + (k / 3) * cs; }

void draw_x(lv_layer_t* layer, int cx, int cy, int r, int w, lv_color_t c)
{
    kit::line(layer, cx - r, cy - r, cx + r, cy + r, w, c);
    kit::line(layer, cx - r, cy + r, cx + r, cy - r, w, c);
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int side = 3 * cs;
    const bool live_turn = B->result() == -1;
    for (int b = 0; b < 9; ++b) {
        const int x0 = a.x1 + (b % 3) * (side + gap), y0 = a.y1 + (b / 3) * (side + gap);
        const bool lit = live_turn && B->board_live(b);
        lv_color_t bg = P.cell;
        if (lit) bg = lv_color_mix(P.lit, P.cell, 130);
        if (B->small[b] == 3) bg = lv_color_mix(P.absent, P.cell, 120);
        kit::fill_rect(layer, x0 - 2, y0 - 2, x0 + side + 1, y0 + side + 1, lit ? P.lit : P.line_thick, 4);
        kit::fill_rect(layer, x0, y0, x0 + side - 1, y0 + side - 1, bg, 2);
        // the small board's lines
        for (int k = 1; k < 3; ++k) {
            kit::fill_rect(layer, x0 + k * cs, y0 + 3, x0 + k * cs, y0 + side - 4, P.line_thin);
            kit::fill_rect(layer, x0 + 3, y0 + k * cs, x0 + side - 4, y0 + k * cs, P.line_thin);
        }
        const bool won = B->small[b] == 1 || B->small[b] == 2;
        for (int k = 0; k < 9; ++k) {
            const int c = b * 9 + k;
            const int cx = a.x1 + cell_x(c) + cs / 2, cy = a.y1 + cell_y(c) + cs / 2;
            if (c == B->last)
                kit::fill_rect(layer, cx - cs / 2 + 2, cy - cs / 2 + 2, cx + cs / 2 - 2, cy + cs / 2 - 2, P.piece_b, 2);
            const int r = cs * 28 / 100, w = cs >= 30 ? 4 : 3;
            const lv_color_t xc = won ? lv_color_mix(P.entry, bg, 110) : P.entry;
            const lv_color_t oc = won ? lv_color_mix(P.piece_a, bg, 110) : P.piece_a;
            if (B->cell[c] == 1) draw_x(layer, cx, cy, r, w, xc);
            else if (B->cell[c] == 2) kit::ring(layer, cx, cy, r + 1, w, oc);
        }
        // A won board: one big mark over it
        if (won) {
            const int cx = x0 + side / 2, cy = y0 + side / 2, r = side * 34 / 100, w = cs >= 30 ? 8 : 6;
            if (B->small[b] == 1) draw_x(layer, cx, cy, r, w, P.entry);
            else kit::ring(layer, cx, cy, r + w / 2, w, P.piece_a);
        }
    }
    int wa, wb;
    if (B->winning_line(&wa, &wb)) {
        auto mid = [&](int b, int* x, int* y) {
            *x = a.x1 + (b % 3) * (side + gap) + side / 2;
            *y = a.y1 + (b / 3) * (side + gap) + side / 2;
        };
        int ax, ay, bx, by;
        mid(wa, &ax, &ay);
        mid(wb, &bx, &by);
        kit::line(layer, ax, ay, bx, by, cs >= 30 ? 8 : 6, P.win);
    }
}

// Taps act on release at the point where the stylus came down
lv_point_t press_at{-1, -1};
void pressed_cb(lv_event_t*)
{
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &press_at);
}

int cell_at(int x, int y)
{
    const int side = 3 * cs;
    const int bx = x / (side + gap), by = y / (side + gap);
    if (bx < 0 || bx > 2 || by < 0 || by > 2) return -1;
    int ix = x - bx * (side + gap), iy = y - by * (side + gap);
    // a tap in a gap goes to the nearest square
    if (ix >= side) ix = side - 1;
    if (iy >= side) iy = side - 1;
    return (by * 3 + bx) * 9 + (iy / cs) * 3 + ix / cs;
}

void clicked_cb(lv_event_t*)
{
    if (!B || !board_obj || press_at.x < 0 || !match::human_may_move()) return;
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    const int c = cell_at(press_at.x - a.x1, press_at.y - a.y1);
    if (c < 0) return;
    if (!B->can_play(c)) { sound(Sound::Error); return; }
    match::human_move(c);
}

// ---- Save ---------------------------------------------------------------------------------------
constexpr size_t kSaveBytes = Board::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_moves = -1;

void save()
{
    if (!B || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = B->serialize(buf, sizeof buf);
    match::save_state(buf + n, sizeof buf - n);
    shell().save_game(kId, buf, sizeof buf);
}

bool load(Board& b)
{
    uint8_t buf[kSaveBytes];
    const Shell& H = shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return n == kSaveBytes && b.deserialize(buf, n) && match::load_state(buf + Board::kSaveBytes, match::kStateBytes);
}

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const Metrics& m = metrics();
    const int margin = m.large ? 8 : 4;
    gap = m.large ? 8 : 6;
    const int room_w = m.w - 2 * margin, room_h = bottom - top - 2 * margin;
    const int room = room_w < room_h ? room_w : room_h;
    cs = (room - 2 * gap - 4) / 9;
    const int size = 9 * cs + 2 * gap;
    board_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, size, size);
    lv_obj_set_pos(board_obj, (m.w - size) / 2, top + (bottom - top - size) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_set_scrollable(board_obj, false);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(board_obj, clicked_cb, LV_EVENT_CLICKED, nullptr);
    match::restart_view();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    if (!B) { app_go_home(); return; }
    if (!load(*B)) { *B = Board{}; match::state() = match::State{}; }
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    save();
    match::closed();
    board_obj = nullptr;
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (B && (B->moves != saved_moves || kit::save_due(now, last_save_ms, match::state().seconds))) {
        saved_moves = B->moves;
        last_save_ms = now;
        save();
    }
}

void restyle()
{
    if (!B) return;
    match::detach();
    build();
}

bool summary(char* buf, size_t cap)
{
    if (B) { match::summary(buf, cap); return true; }
    uint8_t img[kSaveBytes];
    const Shell& H = shell();
    Board* b = new (std::nothrow) Board();
    if (!b) return false;
    match::State st;
    const bool ok = H.load_game && H.load_game(kId, img, sizeof img) == kSaveBytes && b->deserialize(img, kSaveBytes)
                    && match::read_state(img + Board::kSaveBytes, match::kStateBytes, st);
    if (ok) match::describe(st, b->result(), b->moves, kSides, buf, cap);
    delete b;
    return ok;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int g = s / 14, side = (s - 2 * g) / 3;
    for (int b = 0; b < 9; ++b) {
        const int x0 = a.x1 + (b % 3) * (side + g), y0 = a.y1 + (b / 3) * (side + g);
        kit::fill_rect(layer, x0, y0, x0 + side - 1, y0 + side - 1, b == 4 ? lv_color_mix(P.lit, P.cell, 130) : P.cell, 2);
        const int cx = x0 + side / 2, cy = y0 + side / 2, r = side / 3;
        if (b == 0 || b == 8) draw_x(layer, cx, cy, r, 2, P.entry);
        else if (b == 2 || b == 6) kit::ring(layer, cx, cy, r, 2, P.piece_a);
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
extern const GameOps ultimate_ops;
const GameOps ultimate_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace ultimate_preview {
ultimate::Board* board() { return B; }
} // namespace ultimate_preview
#endif
