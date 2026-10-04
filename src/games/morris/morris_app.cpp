// Nine Men's Morris: screen and registry entry. Rules in morris_core.*;
// turns, the computer, menus and stats come from games/common/match.*.
//
// Taps: placing - tap an empty point. Moving - tap your man (dots show
// where it can go), then the point. A move that makes a mill then asks for
// the man to take: the ones you may take get a red ring; tap one (or your
// man again to change your mind).
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "morris_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace morris;
using namespace ui;

constexpr const char* kId = "morris";
const twoplayer::Sides kSides = {"White", "Black"};

Game*     G = nullptr;
lv_obj_t* board_obj = nullptr;
int       unit = 0;                         // grid step (7 x 7 grid)

int  sel = -1;                              // your man picked to move
bool taking = false;                        // a mill was made: pick the man to take
int  pend_from = -1, pend_to = -1;          // the move waiting for that

void clear_pick() { sel = -1; taking = false; pend_from = pend_to = -1; }

// ---- Rules for the controller ---------------------------------------------------------------
int  result() { return G->result(); }
int  turn()   { return G->turn(); }
int  moves()  { return G->plies; }
void redraw() { if (board_obj) lv_obj_invalidate(board_obj); }
void play(int code) { G->play(code); clear_pick(); redraw(); }
void reset() { kit::renew(*G); clear_pick(); }
int  think(int level, uint32_t seed, volatile bool* stop) { return best_move(*G, level, seed, stop); }

void score(char* buf, size_t cap)
{
    const Position& p = G->pos;
    if (p.hand[0] || p.hand[1])
        snprintf(buf, cap, "To place: White %d  Black %d", p.hand[0], p.hand[1]);
    else
        snprintf(buf, cap, "Men: White %d  Black %d", p.on_board(0), p.on_board(1));
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (G->result() != -1) return;
    if (taking) { snprintf(buf, cap, "A mill! Take a %s man", G->turn() ? "white" : "black"); return; }
    const Position& p = G->pos;
    if (p.hand[p.side] == 0 && p.flying(p.side)) snprintf(buf, cap, "%s has three men: they fly", G->turn() ? "Black" : "White");
}

match::Game make_game()
{
    match::Game g{kId, "Nine Men's Morris", kSides, result, turn, moves, play, reset, think, redraw};
    g.score = score;
    g.note = note;
    g.ai_stack = 24 * 1024;
    g.legal = [](int code) {
        MoveList l;
        G->legal(l);
        for (int k = 0; k < l.n; ++k) if (l.m[k].code() == code) return true;
        return false;
    };
    return g;
}

// ---- Board ------------------------------------------------------------------------------------
int px(const lv_area_t& a, int p) { return a.x1 + unit / 2 + kX[p] * unit; }
int py(const lv_area_t& a, int p) { return a.y1 + unit / 2 + kY[p] * unit; }

// What the board shows: the real men, plus a move waiting for its capture
uint32_t shown_men(int s)
{
    uint32_t m = G->pos.men[s];
    if (taking && s == G->turn()) {
        if (pend_from >= 0) m &= ~(1u << pend_from);
        m |= 1u << pend_to;
    }
    return m;
}

uint32_t targets()
{
    if (sel < 0 || taking) return 0;
    MoveList l;
    G->legal(l);
    uint32_t t = 0;
    for (int k = 0; k < l.n; ++k) if (l.m[k].from == sel) t |= 1u << l.m[k].to;
    return t;
}

void draw_man(lv_layer_t* layer, int cx, int cy, int r, int side)
{
    const Palette& P = pal();
    const lv_color_t body = side == 0 ? P.stone_light : P.stone_dark;
    const lv_color_t rim = side == 0 ? P.stone_dark : P.stone_light;
    kit::fill_circle(layer, cx, cy, r, rim);
    kit::fill_circle(layer, cx, cy, r - 2, body);
    kit::ring(layer, cx, cy, r * 6 / 10, 1, lv_color_mix(rim, body, 90));
}

void draw_cb(lv_event_t* e)
{
    if (!G) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.sq_light, unit / 4);
    const lv_color_t ink = lv_color_darken(P.sq_dark, 40);
    const int lw = unit >= 40 ? 3 : 2;
    for (const auto& l : kMills)
        kit::line(layer, px(a, l[0]), py(a, l[0]), px(a, l[2]), py(a, l[2]), lw, ink);
    const uint32_t tgt = targets();
    const uint32_t take = taking ? G->pos.removable(G->turn() ^ 1) : 0;
    const uint32_t m0 = shown_men(0), m1 = shown_men(1);
    const int r = unit * 40 / 100;
    const Move& last = G->last;
    for (int p = 0; p < kPoints; ++p) {
        const int cx = px(a, p), cy = py(a, p);
        const int who = (m0 >> p & 1) ? 0 : (m1 >> p & 1) ? 1 : -1;
        const bool was_last = !taking && last.to == p && G->plies > 0;
        if (who < 0) {
            kit::fill_circle(layer, cx, cy, unit / 9 + 1, ink);
            if (!taking && last.remove == p && G->plies > 0)            // a man was taken here
                kit::ring(layer, cx, cy, r - 3, 2, P.conflict);
        } else {
            if (p == sel || (taking && p == pend_to))
                kit::fill_circle(layer, cx, cy, r + 3, P.selected);
            else if (was_last)
                kit::fill_circle(layer, cx, cy, r + 3, P.same);
            draw_man(layer, cx, cy, r, who);
        }
        if (take >> p & 1) kit::ring(layer, cx, cy, r + 2, unit >= 40 ? 4 : 3, P.conflict);
        if (tgt >> p & 1) {
            kit::fill_circle(layer, cx, cy, unit / 6 + 1, P.stone_light);
            kit::fill_circle(layer, cx, cy, unit / 6, P.target);
        }
    }
}

int point_at(int x, int y)
{
    int best = -1, best_d = unit * unit / 4;
    for (int p = 0; p < kPoints; ++p) {
        const int dx = x - (unit / 2 + kX[p] * unit), dy = y - (unit / 2 + kY[p] * unit);
        const int d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}

void try_move(int from, int to)
{
    const Position& p = G->pos;
    if (p.makes_mill(p.side, from, to) && p.removable(p.side ^ 1)) {
        taking = true;
        pend_from = from;
        pend_to = to;
        sel = -1;
        match::refresh();
        redraw();
        return;
    }
    Move m;
    m.from = int8_t(from);
    m.to = int8_t(to);
    match::human_move(m.code());
}

void tap(int pt)
{
    if (!G || !match::human_may_move()) return;
    const Position& p = G->pos;
    const int s = p.side;
    if (taking) {
        if (G->pos.removable(s ^ 1) >> pt & 1) {
            Move m;
            m.from = int8_t(pend_from);
            m.to = int8_t(pend_to);
            m.remove = int8_t(pt);
            match::human_move(m.code());
        } else if (pt == pend_to) {                       // changed your mind
            clear_pick();
            match::refresh();
            redraw();
        } else {
            sound(Sound::Error);
        }
        return;
    }
    if (p.hand[s] > 0) {                                  // placing
        if (p.cell(pt) < 0) try_move(-1, pt);
        else sound(Sound::Error);
        return;
    }
    if (p.cell(pt) == s) {                                // pick a man
        sel = pt;
        if (!targets()) { sel = -1; sound(Sound::Error); }
        redraw();
        return;
    }
    if (sel >= 0 && (targets() >> pt & 1)) { const int from = sel; sel = -1; try_move(from, pt); return; }
    sel = -1;
    redraw();
}

void press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t q;
    lv_indev_get_point(indev, &q);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int pt = point_at(q.x - a.x1, q.y - a.y1);
    if (pt >= 0) tap(pt);
}

// ---- Save ---------------------------------------------------------------------------------------
constexpr size_t kSaveBytes = Game::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_plies = -1;

void save()
{
    if (!G || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = G->serialize(buf, sizeof buf);
    match::save_state(buf + n, sizeof buf - n);
    shell().save_game(kId, buf, sizeof buf);
}

bool load(Game& g)
{
    uint8_t buf[kSaveBytes];
    const Shell& H = shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return n == kSaveBytes && g.deserialize(buf, n) && match::load_state(buf + Game::kSaveBytes, match::kStateBytes);
}

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const Metrics& m = metrics();
    const int margin = m.large ? 6 : 3;
    const int side = (m.w - 2 * margin) < (bottom - top) ? (m.w - 2 * margin) : (bottom - top);
    unit = side / 7;
    board_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, unit * 7, unit * 7);
    lv_obj_set_pos(board_obj, (m.w - unit * 7) / 2, top + (bottom - top - unit * 7) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_set_scrollable(board_obj, false);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, press_cb, LV_EVENT_PRESSED, nullptr);
    match::restart_view();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { app_go_home(); return; }
    if (!load(*G)) { kit::renew(*G); match::state() = match::State{}; }
    clear_pick();
    saved_plies = G->plies;
    build();
}

void close()
{
    if (!G) return;
    match::detach();
    save();
    match::closed();
    board_obj = nullptr;
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (G && (G->plies != saved_plies || kit::save_due(now, last_save_ms, match::state().seconds))) {
        saved_plies = G->plies;
        last_save_ms = now;
        save();
    }
}

void restyle()
{
    if (!G) return;
    match::detach();
    build();
}

bool summary(char* buf, size_t cap)
{
    if (G) { match::summary(buf, cap); return true; }
    uint8_t img[kSaveBytes];
    const Shell& H = shell();
    Game g;
    match::State st;
    if (!H.load_game || H.load_game(kId, img, sizeof img) != kSaveBytes || !g.deserialize(img, kSaveBytes)
        || !match::read_state(img + Game::kSaveBytes, match::kStateBytes, st))
        return false;
    match::describe(st, g.result(), g.plies, kSides, buf, cap);
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a), u = s / 7;
    const int ox = a.x1 + (s - 7 * u) / 2 + u / 2, oy = a.y1 + (s - 7 * u) / 2 + u / 2;
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.sq_light, s / 10);
    const lv_color_t ink = lv_color_darken(P.sq_dark, 40);
    for (const auto& l : kMills)
        kit::line(layer, ox + kX[l[0]] * u, oy + kY[l[0]] * u, ox + kX[l[2]] * u, oy + kY[l[2]] * u, 2, ink);
    static const int8_t white[] = {0, 1, 2, 10, 16}, black[] = {4, 13, 20, 22};
    for (int p : white) draw_man(layer, ox + kX[p] * u, oy + kY[p] * u, u * 4 / 10 + 1, 0);
    for (int p : black) draw_man(layer, ox + kX[p] * u, oy + kY[p] * u, u * 4 / 10 + 1, 1);
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
extern const GameOps morris_ops;
const GameOps morris_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace morris_preview {
morris::Game* game() { return G; }
void tap_point(int p) { tap(p); }
} // namespace morris_preview
#endif
