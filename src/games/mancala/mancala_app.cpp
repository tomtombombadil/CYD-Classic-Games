// Mancala's screen and registry entry. The rules live in mancala_core.*;
// turns, the computer, menus and stats come from the shared two-player
// controller (games/common/match.*).
//
// Portrait board, three columns: your pits down the left (sown top to
// bottom), the two stores in the middle (yours at the bottom), the other
// side's pits up the right. Sowing runs round that loop, so a move is
// shown seed by seed. vs Computer the board turns so your pits are always
// on the left; pass-and-play keeps Gold on the left.
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "mancala_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace mancala;
using namespace ui;

constexpr const char* kId = "mancala";
constexpr uint32_t kSeedMs = 130;          // one seed of a sowing on screen
const twoplayer::Sides kSides = {"Gold", "Blue"};

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
Sowing    last{};                          // the last move, for the note
bool      have_last = false;

// The move being shown seed by seed
struct Anim {
    bool       on = false;
    uint8_t    pit[14];                    // what's on screen
    Sowing     how;
    int        step = 0;                   // seeds placed so far
    lv_timer_t* timer = nullptr;
} A;

// Geometry, in board-object coordinates
int col_w = 0, store_w = 0, row_h = 0, gap = 0;

bool flipped() { return match::state().mode == twoplayer::Mode::Computer && match::state().human_side == 1; }
int  left_side() { return flipped() ? 1 : 0; }

// ---- Rules for the controller ---------------------------------------------------------------
int  result() { return B->result(); }
int  turn()   { return B->side; }
int  moves()  { return B->moves; }
void redraw() { if (board_obj) lv_obj_invalidate(board_obj); }

void anim_stop()
{
    if (A.timer) { lv_timer_delete(A.timer); A.timer = nullptr; }
    A.on = false;
}

void anim_cb(lv_timer_t*)
{
    if (A.step < A.how.n) {
        ++A.pit[A.how.path[A.step++]];
    } else {
        anim_stop();                      // captures and the sweep: show the real board
    }
    redraw();
}

void play(int p)
{
    Board before = *B;
    Sowing how;
    if (!B->play(p, &how)) return;
    last = how;
    have_last = true;
    anim_stop();
    A.on = true;
    memcpy(A.pit, before.pit, sizeof A.pit);
    A.pit[how.from] = 0;
    A.how = how;
    A.step = 0;
    A.timer = lv_timer_create(anim_cb, kSeedMs, nullptr);
    redraw();
}

void reset() { anim_stop(); *B = Board{}; have_last = false; }

int think(int level, uint32_t seed, volatile bool* stop) { return best_move(*B, level, seed, stop); }

void score(char* buf, size_t cap)
{
    snprintf(buf, cap, "Gold %d  Blue %d", B->pit[kStore[0]], B->pit[kStore[1]]);
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (!have_last || B->result() != -1) return;
    const char* who = B->side == 0 ? "Gold" : "Blue";
    if (match::state().mode == twoplayer::Mode::Computer)
        who = B->side == match::state().human_side ? "You" : "The computer";
    if (last.again) snprintf(buf, cap, "%s %s again", who, strcmp(who, "You") == 0 ? "go" : "goes");
    else if (last.captured) snprintf(buf, cap, "Captured %d seeds", last.captured);
}

match::Game make_game()
{
    match::Game g{kId, "Mancala", kSides, result, turn, moves, play, reset, think, redraw};
    g.score = score;
    g.note = note;
    g.busy = [] { return A.on; };
    g.ai_stack = 8 * 1024;
    return g;
}

// ---- Board ------------------------------------------------------------------------------------
// Pit index -> rectangle (board coordinates)
void pit_rect(int i, int* x, int* y, int* w, int* h)
{
    const int ls = left_side();
    const int side = i / 7, p = i % 7;
    if (p == kPits) {                                       // a store: middle column
        const int sh = (6 * row_h + 5 * gap - gap) / 2;
        *x = col_w + gap;
        *w = store_w;
        *h = sh;
        *y = side == ls ? sh + gap : 0;                     // the left side's store at the bottom
        return;
    }
    *w = col_w;
    *h = row_h;
    if (side == ls) { *x = 0; *y = p * (row_h + gap); }                          // down the left
    else            { *x = col_w + gap + store_w + gap; *y = (kPits - 1 - p) * (row_h + gap); }  // up the right
}

int pit_at(int x, int y)
{
    for (int i = 0; i < 14; ++i) {
        if (i % 7 == kPits) continue;
        int px, py, w, h;
        pit_rect(i, &px, &py, &w, &h);
        if (x >= px - gap / 2 && x < px + w + gap / 2 && y >= py - gap / 2 && y < py + h + gap / 2) return i;
    }
    return -1;
}

// Seeds as small round beads, laid out in rows inside the hollow
void draw_seeds(lv_layer_t* layer, int x, int y, int w, int h, int n, lv_color_t c, lv_color_t rim)
{
    if (n <= 0) return;
    const int shown = n > 24 ? 24 : n;
    const int r = h >= 44 ? 4 : 3;
    const int step = 2 * r + 2;
    const int per_row = (w - 8) / step > 0 ? (w - 8) / step : 1;
    const int rows = (shown + per_row - 1) / per_row;
    const int max_rows = (h - 6) / step > 0 ? (h - 6) / step : 1;
    const int rr = rows > max_rows ? max_rows : rows;
    const int y0 = y + (h - rr * step) / 2 + step / 2;
    int k = 0;
    for (int row = 0; row < rr && k < shown; ++row) {
        const int in_row = shown - k < per_row ? shown - k : per_row;
        const int x0 = x + (w - in_row * step) / 2 + step / 2;
        for (int j = 0; j < in_row; ++j, ++k) {
            const int cx = x0 + j * step + ((row & 1) ? 1 : 0), cy = y0 + row * step;
            kit::fill_circle(layer, cx, cy, r + 1, rim);
            kit::fill_circle(layer, cx, cy, r, c);
        }
    }
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const uint8_t* pit = A.on ? A.pit : B->pit;
    const bool large = metrics().large;
    const lv_font_t* num = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    const lv_font_t* small = large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    const lv_color_t wood = P.sq_dark, hollow = lv_color_darken(P.sq_dark, 60);
    const lv_color_t side_col[2] = {P.piece_b, P.frame};      // Gold, Blue
    kit::fill_rect(layer, a.x1 - 2, a.y1 - 2, a.x2 + 2, a.y2 + 2, wood, 8);
    const bool mine_to_move = match::human_may_move() && !A.on;
    const int highlight = A.on ? -1 : (have_last && !B->over() ? last.from : -1);
    for (int i = 0; i < 14; ++i) {
        int x, y, w, h;
        pit_rect(i, &x, &y, &w, &h);
        x += a.x1;
        y += a.y1;
        const int side = i / 7;
        const bool store = i % 7 == kPits;
        const bool playable = !store && mine_to_move && side == B->side && pit[i] > 0;
        // Rim in the owner's color; the pit you can sow from is lit
        kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, side_col[side], h / 3);
        kit::fill_rect(layer, x + 2, y + 2, x + w - 3, y + h - 3,
                       playable ? lv_color_mix(P.lit, hollow, 90) : hollow, h / 3 - 2);
        if (i == highlight)
            kit::fill_rect(layer, x + 2, y + 2, x + w - 3, y + h - 3, lv_color_mix(P.selected, hollow, 120), h / 3 - 2);
        // Count: big, on the inner end; beads fill the rest
        char t[8];
        snprintf(t, sizeof t, "%d", pit[i]);
        if (store) {
            const int nh = lv_font_get_line_height(num);
            draw_seeds(layer, x + 4, y + 4 + nh, w - 8, h - 8 - nh - lv_font_get_line_height(small), pit[i],
                       P.stone_light, lv_color_darken(P.stone_light, 80));
            kit::text(layer, t, num, P.stone_light, x, y + 4, w, nh);
            const char* name = side == 0 ? "Gold" : "Blue";
            if (match::state().mode == twoplayer::Mode::Computer)
                name = side == match::state().human_side ? "You" : "Computer";
            const int sh = lv_font_get_line_height(small);
            kit::text(layer, name, small, lv_color_mix(P.stone_light, hollow, 170), x, y + h - sh - 4, w, sh);
        } else {
            const int nw = w * 36 / 100;
            const bool left = (side == left_side());
            const int nx = left ? x + w - nw : x;            // number toward the middle
            draw_seeds(layer, left ? x + 3 : x + nw, y + 2, w - nw - 3, h - 4, pit[i],
                       P.stone_light, lv_color_darken(P.stone_light, 80));
            kit::text(layer, t, num, P.stone_light, nx, y, nw, h);
        }
    }
}

void press_cb(lv_event_t* e)
{
    if (!B || A.on || !match::human_may_move()) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int i = pit_at(p.x - a.x1, p.y - a.y1);
    if (i < 0) return;
    if (i / 7 != B->side || B->pit[i] == 0) { sound(Sound::Error); return; }
    match::human_move(i % 7);
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
    const int margin = m.large ? 8 : 5;
    gap = m.large ? 6 : 4;
    const int W = m.w - 2 * margin, H = bottom - top - 2 * margin;
    row_h = (H - 5 * gap) / kPits;
    if (row_h > (m.large ? 56 : 40)) row_h = m.large ? 56 : 40;
    store_w = W * 26 / 100;
    col_w = (W - store_w - 2 * gap) / 2;
    const int bw = 2 * col_w + store_w + 2 * gap, bh = kPits * row_h + 5 * gap;
    board_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, bw, bh);
    lv_obj_set_pos(board_obj, (m.w - bw) / 2, top + (bottom - top - bh) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_set_scrollable(board_obj, false);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, press_cb, LV_EVENT_PRESSED, nullptr);
    match::restart_view();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    if (!B) { app_go_home(); return; }
    if (!load(*B)) { *B = Board{}; match::state() = match::State{}; }
    have_last = false;
    A = Anim{};
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    anim_stop();
    save();
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
    anim_stop();
    build();
}

bool summary(char* buf, size_t cap)
{
    if (B) { match::summary(buf, cap); return true; }
    uint8_t img[kSaveBytes];
    const Shell& H = shell();
    Board b;
    match::State st;
    if (!H.load_game || H.load_game(kId, img, sizeof img) != kSaveBytes || !b.deserialize(img, kSaveBytes)
        || !match::read_state(img + Board::kSaveBytes, match::kStateBytes, st))
        return false;
    match::describe(st, b.result(), b.moves, kSides, buf, cap);
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const lv_color_t hollow = lv_color_darken(P.sq_dark, 60);
    kit::fill_rect(layer, a.x1, a.y1 + s / 8, a.x2, a.y2 - s / 8, P.sq_dark, s / 8);
    const int cw = s * 30 / 100, ph = s * 22 / 100, g = s / 25;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 2; ++c) {
            const int x = c ? a.x2 - g - cw : a.x1 + g, y = a.y1 + s / 8 + g + r * (ph + g);
            kit::fill_rect(layer, x, y, x + cw - 1, y + ph - 1, hollow, ph / 3);
            for (int k = 0; k < 2 + (r + c) % 3; ++k)
                kit::fill_circle(layer, x + cw / 4 + k * cw / 5, y + ph / 2 + ((k & 1) ? 2 : -2), s / 22, P.stone_light);
        }
    const int sx = a.x1 + s / 2 - s * 15 / 100;
    kit::fill_rect(layer, sx, a.y1 + s / 8 + g, sx + s * 30 / 100 - 1, a.y2 - s / 8 - g, hollow, s / 10);
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
extern const GameOps mancala_ops;
const GameOps mancala_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace mancala_preview {
mancala::Board* board() { return B; }
void finish() { anim_stop(); redraw(); }
} // namespace mancala_preview
#endif
