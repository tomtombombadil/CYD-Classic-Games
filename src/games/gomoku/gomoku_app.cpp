// Gomoku: screen and registry entry. Rules and the computer in
// gomoku_core.*; turns, menus, stats and wireless play come from the shared
// two-player controller (games/common/match.*).
//
// A 15x15 grid of lines on a wooden board, as big as fits (16 px between
// lines on a 240-wide screen); stones sit on the points. A tap places on
// the point nearest where the stylus came down. The last stone has a gold
// ring; the winning five gets a line through it.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "gomoku_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace gomoku;
using namespace ui;

constexpr const char* kId = "gomoku";
const twoplayer::Sides kSides = {"Black", "White"};

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
int step = 16, edge = 8;                 // between lines; from the board's edge to the first line

// ---- Rules for the controller ---------------------------------------------------------------
int  result() { return B->result(); }
int  turn()   { return B->turn(); }
int  moves()  { return B->moves; }
void redraw() { if (board_obj) lv_obj_invalidate(board_obj); }
void play(int p) { B->play(p); redraw(); }
void reset() { *B = Board{}; }
int  think(int level, uint32_t seed, volatile bool* stop) { return best_move(*B, level, seed, stop); }

int list(int* out, int cap)
{
    int n = 0;
    for (int p = 0; p < kPoints && n < cap; ++p) if (B->can_play(p)) out[n++] = p;
    return n;
}

match::Game make_game()
{
    match::Game g{kId, "Gomoku", kSides, result, turn, moves, play, reset, think, redraw};
    g.ai_stack = 8 * 1024;
    g.legal = [](int p) { return B->can_play(p); };
    g.list = list;
    return g;
}

// ---- Board ------------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const lv_color_t wood = P.sq_light, line = lv_color_mix(P.stone_dark, wood, 150);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, wood, 4);
    const int x0 = a.x1 + edge, y0 = a.y1 + edge, len = (kN - 1) * step;
    for (int k = 0; k < kN; ++k) {
        kit::fill_rect(layer, x0, y0 + k * step, x0 + len, y0 + k * step, line);
        kit::fill_rect(layer, x0 + k * step, y0, x0 + k * step, y0 + len, line);
    }
    static const int kStars[5][2] = {{3, 3}, {3, 11}, {11, 3}, {11, 11}, {7, 7}};
    for (const auto& s : kStars) kit::fill_circle(layer, x0 + s[1] * step, y0 + s[0] * step, step >= 20 ? 3 : 2, line);
    const int r = step / 2 - 1;
    for (int p = 0; p < kPoints; ++p) {
        if (!B->stone[p]) continue;
        const int cx = x0 + (p % kN) * step, cy = y0 + (p / kN) * step;
        if (B->stone[p] == 1) {
            kit::fill_circle(layer, cx, cy, r, P.stone_dark);
        } else {
            kit::fill_circle(layer, cx, cy, r, P.stone_dark);            // a dark rim: white shows on a light board
            kit::fill_circle(layer, cx, cy, r - (step >= 20 ? 2 : 1), P.stone_light);
        }
    }
    if (B->last >= 0)
        kit::ring(layer, x0 + (B->last % kN) * step, y0 + (B->last / kN) * step, r / 2, 2, P.piece_b);
    int wa, wb;
    if (B->winning_line(&wa, &wb))
        kit::line(layer, x0 + (wa % kN) * step, y0 + (wa / kN) * step, x0 + (wb % kN) * step, y0 + (wb / kN) * step,
                  step >= 20 ? 5 : 4, P.win);
}

// Taps act on release at the point where the stylus came down
lv_point_t press_at{-1, -1};
void pressed_cb(lv_event_t*)
{
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &press_at);
}

void clicked_cb(lv_event_t*)
{
    if (!B || !board_obj || press_at.x < 0 || !match::human_may_move()) return;
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    const int x = press_at.x - a.x1 - edge + step / 2, y = press_at.y - a.y1 - edge + step / 2;
    if (x < 0 || y < 0) return;
    const int c = x / step, r = y / step;
    if (c >= kN || r >= kN) return;
    const int p = r * kN + c;
    if (!B->can_play(p)) { sound(Sound::Error); return; }
    match::human_move(p);
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
    const int margin = m.large ? 4 : 2;
    const int room_w = m.w - 2 * margin, room_h = bottom - top - 2 * margin;
    const int room = room_w < room_h ? room_w : room_h;
    step = room / kN;
    edge = step / 2;
    const int size = kN * step;
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
    const int s = lv_area_get_width(&a), n = 6, st = s / n;
    const lv_color_t line = lv_color_mix(P.stone_dark, P.sq_light, 150);
    kit::fill_rect(layer, a.x1, a.y1, a.x1 + n * st, a.y1 + n * st, P.sq_light, 4);
    for (int k = 0; k < n; ++k) {
        kit::fill_rect(layer, a.x1 + st / 2, a.y1 + st / 2 + k * st, a.x1 + st / 2 + (n - 1) * st, a.y1 + st / 2 + k * st, line);
        kit::fill_rect(layer, a.x1 + st / 2 + k * st, a.y1 + st / 2, a.x1 + st / 2 + k * st, a.y1 + st / 2 + (n - 1) * st, line);
    }
    for (int k = 0; k < 5; ++k)                         // a five on the diagonal, a few white
        kit::fill_circle(layer, a.x1 + st / 2 + k * st, a.y1 + st / 2 + k * st, st / 2 - 1, P.stone_dark);
    const int wpts[3][2] = {{3, 1}, {4, 2}, {1, 3}};
    for (const auto& w : wpts) {
        kit::fill_circle(layer, a.x1 + st / 2 + w[0] * st, a.y1 + st / 2 + w[1] * st, st / 2 - 1, P.stone_dark);
        kit::fill_circle(layer, a.x1 + st / 2 + w[0] * st, a.y1 + st / 2 + w[1] * st, st / 2 - 2, P.stone_light);
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
extern const GameOps gomoku_ops;
const GameOps gomoku_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace gomoku_preview {
gomoku::Board* board() { return B; }
} // namespace gomoku_preview
#endif
