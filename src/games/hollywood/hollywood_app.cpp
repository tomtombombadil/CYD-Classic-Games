// Hollywood CYDs: screen and registry entry. Rules and the computer in
// hollywood_core.*; turns, the computer's task, menus, stats and
// pass-and-play come from the shared two-player controller (match.*).
// Questions: the shared trivia bank (Open Trivia DB, CC BY-SA 4.0).
//
// The board: nine stars (a face and a name each) in a 3 x 3 grid; a won
// square shows a big X or O. Tap a star on your turn. The star's
// question then fills the board: the question, and what the star says in
// a gold box; Agree | Disagree along the bottom. After judging, the right
// answer shows for a moment (green if you judged right, red if not), then
// the board with the square's new mark.
//
// Sounds: your right judgment (Place), a wrong one ("aww"), the other
// side's turn (Turn), the end (Win / Lose / Draw by match).
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/common/trivia_bank.h"
#include "games/registry.h"
#include "hollywood_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace hcyd;
using namespace ui;

constexpr const char* kId = "hollywood";
const twoplayer::Sides kSides = {"X", "O"};
constexpr uint32_t kAskMs = 2600, kRevealMs = 2200;

Board*            B = nullptr;
trivia::Question* Q = nullptr;
lv_obj_t*         area = nullptr;
lv_obj_t*         key_yes = nullptr;
lv_obj_t*         key_no = nullptr;
int               area_h = 0;
uint32_t          now_ms = 0, hold_until = 0;
bool              revealing = false;

bool pnp() { return match::state().mode == twoplayer::Mode::PassAndPlay; }
bool busy() { return int32_t(hold_until - now_ms) > 0; }
void redraw() { if (area) lv_obj_invalidate(area); }
void update_keys();

const char* side_name(int s)
{
    if (pnp()) return s == 0 ? kSides.side1 : kSides.side2;
    return s == match::my_side() ? "You" : (match::opponent_name() ? match::opponent_name() : "They");
}

// ---- Rules for the controller ---------------------------------------------------------------
int result() { return B->result(); }
int turn()   { return B->turn; }
int moves()  { return B->moves; }

void play(int m)
{
    const bool judging = B->phase == Phase::Judge;
    if (!B->play(m)) return;
    if (judging) { revealing = true; hold_until = now_ms + kRevealMs; }
    else {
        if (Q) trivia::get(B->q, *Q);
        const bool cpu = !pnp() && B->turn != match::my_side();
        hold_until = cpu ? now_ms + kAskMs : now_ms;          // let the player read the computer's question
    }
    update_keys();
    redraw();
}

void reset()
{
    B->reset(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1);
    hold_until = 0;
    revealing = false;
    update_keys();
}

int think(int level, uint32_t seed, volatile bool*)
{
    return best_move(*B, level, seed);
}

void move_sound(bool by_other)
{
    if (B->phase == Phase::Judge) return;                    // a pick: silent
    if (B->last_side < 0) return;
    const bool mine = !pnp() && B->last_side == match::my_side();
    if (by_other && !mine) sound(Sound::Turn);
    else sound(B->last_right ? Sound::Place : Sound::Error);
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (B->phase == Phase::Judge) {
        snprintf(buf, cap, "%s asked %s", side_name(B->turn), kStars[B->square]);
        return;
    }
    if (B->last_side < 0) { snprintf(buf, cap, "Pick a star"); return; }
    const char* who = side_name(B->last_side);
    const char* was = (!pnp() && B->last_side == match::my_side()) ? "were" : "was";
    if (B->last_got < 0) snprintf(buf, cap, "%s %s wrong - the square stays open", who, was);
    else if (B->last_got == B->last_side) snprintf(buf, cap, "%s %s right!", who, was);
    else snprintf(buf, cap, "%s %s wrong: square to %s", who, was, side_name(B->last_got));
}

void score(char* buf, size_t cap)
{
    snprintf(buf, cap, "%s %d  %s %d", side_name(0), B->count(0), side_name(1), B->count(1));
}

match::Game make_game()
{
    match::Game g{kId, "Hollywood CYDs", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.move_sound = move_sound;
    g.busy = busy;
    g.ai_stack = 4096;
    return g;
}

// ---- Keys / taps ------------------------------------------------------------------------------
bool my_turn_now() { return B && B->result() < 0 && match::human_may_move() && !busy(); }

void key_cb(lv_event_t* e)
{
    if (!B || overlay_open() || !my_turn_now() || B->phase != Phase::Judge) return;
    const int m = int(intptr_t(lv_event_get_user_data(e)));
    match::human_move(m);
}

void update_keys()
{
    if (!key_yes) return;
    const bool show = B->phase == Phase::Judge && !revealing && B->result() < 0 && match::human_may_move();
    lv_obj_set_hidden(key_yes, !show);
    lv_obj_set_hidden(key_no, !show);
}

lv_point_t press_pt{0, 0};
void press_cb(lv_event_t*) { lv_indev_get_point(lv_indev_active(), &press_pt); }

void area_cb(lv_event_t*)
{
    if (!B || overlay_open() || !my_turn_now() || B->phase != Phase::Pick || revealing) return;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int cw = lv_area_get_width(&a) / 3, ch = area_h / 3;
    const int c = (press_pt.x - a.x1) / cw, r = (press_pt.y - a.y1) / ch;
    if (c < 0 || c > 2 || r < 0 || r > 2) return;
    const int sq = r * 3 + c;
    if (B->can_play(sq)) match::human_move(sq);
}

// ---- Drawing ----------------------------------------------------------------------------------
lv_color_t star_color(int i)
{
    const Palette& P = pal();
    const lv_color_t c[9] = {P.piece_b, P.lit, lv_color_mix(P.piece_a, P.lit, 120), P.felt, P.sq_light,
                             lv_color_mix(P.frame, P.piece_a, 130), P.win, lv_color_mix(P.frame, P.lit, 90), P.piece_b};
    return c[i % 9];
}

// A friendly face: a round head, eyes and a smile, and a hat or hair by star
void draw_star(lv_layer_t* layer, int cx, int cy, int r, int i)
{
    const Palette& P = pal();
    const lv_color_t skin = lv_color_mix(P.sq_light, P.lit, 170);
    // hats / hair
    if (i % 3 == 0) kit::fill_rect(layer, cx - r, cy - r - r / 3, cx + r, cy - r / 2, star_color(i), r / 4);
    else if (i % 3 == 1) kit::fill_circle(layer, cx, cy - r / 3, r + r / 6, star_color(i));
    kit::fill_circle(layer, cx, cy, r, P.stone_dark);
    kit::fill_circle(layer, cx, cy, r - 1, skin);
    if (i % 3 == 2) {                                      // a crown of three points
        lv_draw_triangle_dsc_t d;
        lv_draw_triangle_dsc_init(&d);
        d.color = star_color(i);
        d.opa = LV_OPA_COVER;
        for (int k = -1; k <= 1; ++k) {
            d.p[0].x = cx + k * r / 2 - r / 3; d.p[0].y = cy - r + 2;
            d.p[1].x = cx + k * r / 2 + r / 3; d.p[1].y = cy - r + 2;
            d.p[2].x = cx + k * r / 2;         d.p[2].y = cy - r - r / 2;
            lv_draw_triangle(layer, &d);
        }
    }
    const int er = r / 6 > 1 ? r / 6 : 1;
    kit::fill_circle(layer, cx - r / 3, cy - r / 6, er, P.stone_dark);
    kit::fill_circle(layer, cx + r / 3, cy - r / 6, er, P.stone_dark);
    kit::line(layer, cx - r / 3, cy + r / 3, cx, cy + r / 2, 2, P.stone_dark);
    kit::line(layer, cx, cy + r / 2, cx + r / 3, cy + r / 3, 2, P.stone_dark);
}

void draw_mark(lv_layer_t* layer, int cx, int cy, int r, int side)
{
    const Palette& P = pal();
    const int w = r / 4 > 3 ? r / 4 : 3;
    if (side == 0) {
        kit::line(layer, cx - r, cy - r, cx + r, cy + r, w, P.piece_a);
        kit::line(layer, cx + r, cy - r, cx - r, cy + r, w, P.piece_a);
    } else {
        kit::ring(layer, cx, cy, r, w, P.frame);
    }
}

void draw_wrapped(lv_layer_t* layer, const char* s, const lv_font_t* f, lv_color_t c, int x1, int y1, int w, int h, bool center)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = f;
    d.color = c;
    d.align = center ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
    lv_area_t a{x1, y1, x1 + w - 1, y1 + h - 1};
    lv_draw_label(layer, &d, &a);
}

int text_h(const char* s, const lv_font_t* f, int w)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return p.y;
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int w = lv_area_get_width(&a);
    const bool asking = (B->phase == Phase::Judge || revealing) && B->q >= 0 && Q;
    if (!asking) {
        const int cw = w / 3, ch = area_h / 3;
        const lv_font_t* nf = large ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
        for (int i = 0; i < kSquares; ++i) {
            const int x = a.x1 + (i % 3) * cw, y = a.y1 + (i / 3) * ch;
            const bool open = B->owner[i] < 0;
            kit::fill_rect(layer, x + 2, y + 2, x + cw - 3, y + ch - 3, open ? lv_color_mix(P.frame, P.cell, 60) : P.cell, 6);
            const int r = (ch - 2 * lv_font_get_line_height(nf) - 10) / 2 < cw / 4 ? (ch - 2 * lv_font_get_line_height(nf) - 10) / 2 : cw / 4;
            draw_star(layer, x + cw / 2, y + 6 + r + r / 3, r, i);
            draw_wrapped(layer, kStars[i], nf, P.ink, x + 4, y + ch - 2 * lv_font_get_line_height(nf) - 4, cw - 8, 2 * lv_font_get_line_height(nf), true);
            if (!open) draw_mark(layer, x + cw / 2, y + ch / 2, (cw < ch ? cw : ch) * 34 / 100, B->owner[i]);
            if (i == B->last_square && !open) kit::ring(layer, x + cw / 2, y + ch / 2, 3, 2, P.lit);
        }
        return;
    }
    // The question: the star, the question, what the star says
    const int sq = revealing ? B->last_square : B->square;
    const int pad = large ? 8 : 4;
    const lv_font_t* qf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const lv_font_t* sf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int r = large ? 22 : 15;
    int y = a.y1 + pad;
    draw_star(layer, a.x1 + pad + r, y + r + r / 2, r, sq >= 0 ? sq : 0);
    char t[160];
    snprintf(t, sizeof t, "%s", sq >= 0 ? kStars[sq] : "");
    kit::text_left(layer, t, sf, P.muted, a.x1 + 2 * pad + 2 * r, y + r / 2, w - 3 * pad - 2 * r, lv_font_get_line_height(sf));
    snprintf(t, sizeof t, "%s", trivia::category_name(Q->category));
    kit::text_left(layer, t, sf, P.muted, a.x1 + 2 * pad + 2 * r, y + r / 2 + lv_font_get_line_height(sf), w - 3 * pad - 2 * r, lv_font_get_line_height(sf));
    y += 2 * r + r / 2 + pad;
    const int qh = text_h(Q->text, qf, w - 2 * pad);
    draw_wrapped(layer, Q->text, qf, P.ink, a.x1 + pad, y, w - 2 * pad, qh + 2, false);
    y += qh + 2 * pad;
    // the star's answer in a gold box; once judged, green (right) or red (bluff) with the true answer
    const char* says = Q->answer[B->star_says < Q->answers ? B->star_says : 0];
    snprintf(t, sizeof t, "\"%s\"", says);
    const int bh = text_h(t, qf, w - 4 * pad) + 2 * pad + lv_font_get_line_height(sf);
    lv_color_t box = P.lit, ink = P.stone_dark;
    if (revealing) { box = B->star_right() ? P.win : P.piece_a; ink = P.stone_light; }
    kit::fill_rect(layer, a.x1 + pad, y, a.x2 - pad, y + bh, box, 8);
    kit::text_left(layer, revealing ? (B->star_right() ? "Right answer:" : "A bluff! It said:") : "The star says:", sf, ink, a.x1 + 2 * pad, y + pad / 2, w - 4 * pad, lv_font_get_line_height(sf));
    draw_wrapped(layer, t, qf, ink, a.x1 + 2 * pad, y + pad / 2 + lv_font_get_line_height(sf), w - 4 * pad, bh, false);
    y += bh + pad;
    if (revealing && !B->star_right()) {
        snprintf(t, sizeof t, "The answer: %s", Q->answer[0]);
        draw_wrapped(layer, t, sf, P.ink, a.x1 + pad, y, w - 2 * pad, 3 * lv_font_get_line_height(sf), false);
    }
}

// ---- Save ---------------------------------------------------------------------------------------
constexpr size_t kSaveBytes = Board::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_moves = -1;

void save()
{
    if (!B || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = B->serialize(buf, kSaveBytes);
    match::save_state(buf + n, kSaveBytes - n);
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load_image(const uint8_t* buf, size_t n, Board& b, match::State* st)
{
    if (n != kSaveBytes || !b.deserialize(buf, Board::kSaveBytes)) return false;
    return st ? match::read_state(buf + Board::kSaveBytes, match::kStateBytes, *st)
              : match::load_state(buf + Board::kSaveBytes, match::kStateBytes);
}

bool load(Board& b)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = load_image(buf, n, b, nullptr);
    delete[] buf;
    return ok;
}

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int half = (m.w - 3 * pad) / 2;
    const int ky = bottom + pad;
    key_yes = make_key(scr, half, kh, key_cb, kAgree);
    key_label(key_yes, "Agree", menu_font());
    lv_obj_set_pos(key_yes, pad, ky);
    set_checked(key_yes, true);
    key_no = make_key(scr, half, kh, key_cb, kDisagree);
    key_label(key_no, "Disagree", menu_font());
    lv_obj_set_pos(key_no, 2 * pad + half, ky);
    set_checked(key_no, true);
    area_h = bottom - top - pad;
    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w - 2 * pad, area_h);
    lv_obj_set_pos(area, pad, top + pad / 2);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(area, area_cb, LV_EVENT_CLICKED, nullptr);
    update_keys();
    match::restart_view();
    update_keys();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    Q = new (std::nothrow) trivia::Question();
    if (!B || !Q) { delete B; delete Q; B = nullptr; Q = nullptr; app_go_home(); return; }
    if (!load(*B)) {
        *B = Board{};
        B->reset(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1);
        match::state() = match::State{};
    }
    if (B->q >= 0) trivia::get(B->q, *Q);
    revealing = false;
    hold_until = 0;
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    save();
    match::closed();
    trivia::release();
    area = key_yes = key_no = nullptr;
    delete B;
    delete Q;
    B = nullptr;
    Q = nullptr;
}

void tick(uint32_t now)
{
    const bool was = busy();                      // with the last tick's time
    now_ms = now;
    match::tick(now);
    if (!B) return;
    if (was && !busy()) {
        if (revealing) revealing = false;
        update_keys();
        redraw();
        match::refresh();
    }
    update_keys();
    if (B->moves != saved_moves || kit::save_due(now, last_save_ms, match::state().seconds)) {
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
    uint8_t* img = new (std::nothrow) uint8_t[kSaveBytes];
    Board* b = new (std::nothrow) Board();
    bool ok = false;
    if (img && b) {
        match::State st;
        ok = shell().load_game && load_image(img, shell().load_game(kId, img, kSaveBytes), *b, &st);
        if (ok) match::describe(st, b->result(), b->moves, kSides, buf, cap);
    }
    delete[] img;
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
    const int s = lv_area_get_width(&a), c = s / 3;
    for (int i = 0; i < 9; ++i) {
        const int x = a.x1 + (i % 3) * c, y = a.y1 + (i / 3) * c;
        kit::fill_rect(layer, x + 1, y + 1, x + c - 2, y + c - 2, lv_color_mix(P.frame, P.cell, 60), 3);
        if (i == 0 || i == 4) draw_mark(layer, x + c / 2, y + c / 2, c / 3, 0);
        else if (i == 2) draw_mark(layer, x + c / 2, y + c / 2, c / 3, 1);
        else draw_star(layer, x + c / 2, y + c / 2 + 1, c / 4, i);
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

namespace hollywood_preview {
hcyd::Board* board() { return B; }
void sync() { if (B && Q && B->q >= 0) trivia::get(B->q, *Q); update_keys(); redraw(); match::refresh(); }
void reveal(bool on) { revealing = on; hold_until = on ? now_ms + 600000 : 0; sync(); }
}

namespace games {
extern const GameOps hollywood_ops;
const GameOps hollywood_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
