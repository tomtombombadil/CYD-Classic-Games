// Card Sharks CYD: screen and registry entry. Rules and the computer in
// cardsharks_core.*; turns, the computer's task, menus, stats and
// pass-and-play come from the shared two-player controller (match.*).
//
// Two rows of five cards, the other player's on top, yours at the bottom
// (in pass-and-play Gold's row is at the bottom). The card in play is edged
// in gold; a gold bar marks where you froze. A miss shows the card that
// beat you, crossed out, until the next move. Keys: Higher | Lower in the
// bottom row, Freeze | Change above them. Each call holds a moment so the
// card can be seen before play goes on.
//
// Sounds: a right call (Place / Turn), a miss ("aww"), a round won
// (Trill), the match (Win / Lose). Freeze and Change are silent.
#include <cstdio>
#include <cstring>
#include <new>
#include "cardsharks_core.h"
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace csh;
using namespace ui;

constexpr const char* kId = "cardsharks";
const twoplayer::Sides kSides = {"Gold", "Blue"};
constexpr uint32_t kHoldMs = 750;

Board*    B = nullptr;
lv_obj_t* table_obj = nullptr;
lv_obj_t* key_hi = nullptr;
lv_obj_t* key_lo = nullptr;
lv_obj_t* key_fr = nullptr;
lv_obj_t* key_ch = nullptr;
int       cw = 40, chh = 56, gap = 6, top_y = 0, mid_y = 0, bot_y = 0, row_x = 0;
uint32_t  now_ms = 0, hold_until = 0;

bool pnp() { return match::state().mode == twoplayer::Mode::PassAndPlay; }
int  bottom_side() { const int s = match::my_side(); return s >= 0 ? s : 0; }
bool busy() { return int32_t(hold_until - now_ms) > 0; }
void redraw() { if (table_obj) lv_obj_invalidate(table_obj); }
void update_keys();

// ---- Rules for the controller ---------------------------------------------------------------
int result() { return B->result(); }
int turn()   { return B->turn(); }
int moves()  { return B->moves; }

void play(int m)
{
    if (!B->play(m)) return;
    if (m == kHigher || m == kLower) hold_until = now_ms + kHoldMs;
    update_keys();
    redraw();
}

void reset()
{
    B->reset(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1);
    hold_until = 0;
    update_keys();
}

int think(int level, uint32_t seed, volatile bool*) { return best_move(*B, level, seed); }

const char* side_name(int s)
{
    if (pnp()) return s == 0 ? kSides.side1 : kSides.side2;
    return s == match::my_side() ? "You" : (match::opponent_name() ? match::opponent_name() : "They");
}

void move_sound(bool by_other)
{
    switch (B->last) {
        case Last::Right:    sound(by_other ? Sound::Turn : Sound::Place); break;
        case Last::Wrong:    sound(Sound::Error); break;
        case Last::RoundWon: sound(Sound::Trill); break;
        default: break;
    }
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    const int s = B->last_side;
    if (s < 0) { snprintf(buf, cap, "Higher or Lower? Aces are high"); return; }
    const char* who = side_name(s);
    const bool you = !pnp() && s == match::my_side();
    switch (B->last) {
        case Last::Right:    snprintf(buf, cap, "%s %s right!", who, you ? "were" : "was"); break;
        case Last::Wrong:    snprintf(buf, cap, "%s missed: the %s%s", who, cards::rank_text(B->last_card % 13 + 1),
                                      B->last_card % 13 == 0 ? " (high)" : ""); break;
        case Last::Froze:    snprintf(buf, cap, "%s froze", who); break;
        case Last::Changed:  snprintf(buf, cap, "%s changed the card", who); break;
        case Last::RoundWon: snprintf(buf, cap, "%s won the round!", who); break;
        default: break;
    }
}

void score(char* buf, size_t cap)
{
    snprintf(buf, cap, "Rounds: %s %d, %s %d", side_name(0), B->wins[0], side_name(1), B->wins[1]);
}

match::Game make_game()
{
    match::Game g{kId, "Card Sharks CYD", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.move_sound = move_sound;
    g.busy = busy;
    g.ai_stack = 4096;
    return g;
}

// ---- Keys ---------------------------------------------------------------------------------------
void key_cb(lv_event_t* e)
{
    if (!B || overlay_open() || busy() || !match::human_may_move()) return;
    const int m = int(intptr_t(lv_event_get_user_data(e)));
    if (B->can_play(m)) match::human_move(m);
}

void update_keys()
{
    if (!key_hi) return;
    const bool on = B->result() < 0;
    const bool mine = on && match::human_may_move();
    lv_obj_t* ks[4] = {key_hi, key_lo, key_fr, key_ch};
    for (int m = 0; m < 4; ++m) {
        lv_obj_set_hidden(ks[m], !on);
        set_dim(ks[m], !mine || !B->can_play(m));
    }
}

// ---- Drawing ------------------------------------------------------------------------------------
void draw_row(lv_layer_t* layer, int x0, int y, int side, bool to_move)
{
    const Palette& P = pal();
    const Row& r = B->row[side];
    const bool missed = B->last == Last::Wrong && B->last_side == side && B->last_card != kNoCard;
    for (int i = 0; i < kRow; ++i) {
        const int x = x0 + i * (cw + gap);
        if (r.card[i] != kNoCard) {
            const bool current = i == r.pos && to_move;
            if (current) kit::fill_rect(layer, x - 3, y - 3, x + cw + 2, y + chh + 2, P.lit, 6);
            cards::draw_face(layer, x, y, cw, chh, r.card[i], false);
        } else if (missed && i == B->missed_at) {
            // The card that beat you, crossed out
            cards::draw_face(layer, x, y, cw, chh, B->last_card, false);
            kit::line(layer, x + 4, y + 4, x + cw - 5, y + chh - 5, 3, P.piece_a);
            kit::line(layer, x + cw - 5, y + 4, x + 4, y + chh - 5, 3, P.piece_a);
        } else {
            cards::draw_back(layer, x, y, cw, chh);
        }
        if (i == r.frozen && r.frozen > 0) kit::fill_rect(layer, x, y + chh + 4, x + cw - 1, y + chh + 7, P.piece_b, 2);
    }
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(table_obj, &a);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 8);
    const int bs = bottom_side(), ts = bs ^ 1;
    const bool on = B->result() < 0;
    const bool large = metrics().large;
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int lh = lv_font_get_line_height(f);
    char t[48];
    // names and rounds won
    snprintf(t, sizeof t, "%s  %s", side_name(ts), B->wins[ts] ? (B->wins[ts] > 1 ? "**" : "*") : "");
    kit::text(layer, t, f, P.stone_light, a.x1, a.y1 + top_y - lh - 2, lv_area_get_width(&a), lh);
    draw_row(layer, a.x1 + row_x, a.y1 + top_y, ts, on && B->turn() == ts);
    draw_row(layer, a.x1 + row_x, a.y1 + bot_y, bs, on && B->turn() == bs);
    snprintf(t, sizeof t, "%s  %s", side_name(bs), B->wins[bs] ? (B->wins[bs] > 1 ? "**" : "*") : "");
    kit::text(layer, t, f, P.stone_light, a.x1, a.y1 + bot_y + chh + 10, lv_area_get_width(&a), lh);
    // The middle: whose turn and the chance of each call (for the side to move)
    if (on) {
        const Row& r = B->row[B->turn()];
        snprintf(t, sizeof t, "%s: Higher or Lower than %s?", side_name(B->turn()), cards::rank_text(r.card[r.pos] % 13 + 1));
        kit::text(layer, t, f, P.stone_light, a.x1, a.y1 + mid_y, lv_area_get_width(&a), lh);
    }
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

bool load_image(const uint8_t* buf, size_t n, Board& b, match::State* st)
{
    if (n != kSaveBytes || !b.deserialize(buf, Board::kSaveBytes)) return false;
    return st ? match::read_state(buf + Board::kSaveBytes, match::kStateBytes, *st)
              : match::load_state(buf + Board::kSaveBytes, match::kStateBytes);
}

bool load(Board& b)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    return load_image(buf, n, b, nullptr);
}

lv_obj_t* add_key(lv_obj_t* scr, int w, int h, int x, int y, const char* t, intptr_t m, bool primary)
{
    lv_obj_t* k = make_key(scr, w, h, key_cb, m);
    key_label(k, t, menu_font());
    lv_obj_set_pos(k, x, y);
    if (primary) lv_obj_add_state(k, LV_STATE_CHECKED);
    return k;
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
    // Keys: Higher | Lower in the controller's Play Again row, Freeze | Change above
    const int half = (m.w - 3 * pad) / 2;
    const int ky2 = bottom + pad, ky1 = ky2 - kh - pad;
    key_hi = add_key(scr, half, kh, pad, ky2, "Higher", kHigher, true);
    key_lo = add_key(scr, half, kh, 2 * pad + half, ky2, "Lower", kLower, true);
    key_fr = add_key(scr, half, kh, pad, ky1, "Freeze", kFreeze, false);
    key_ch = add_key(scr, half, kh, 2 * pad + half, ky1, "Change", kChange, false);
    // The table between the top bar and the keys
    const int th = ky1 - pad - top - pad;
    gap = m.large ? 8 : 5;
    const bool large = m.large;
    const int lh = lv_font_get_line_height(large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    cw = (m.w - 2 * pad - 4 * gap - 16) / 5;
    chh = cw * 14 / 10;
    const int need = 2 * chh + 3 * lh + 40;
    if (need > th) { chh = (th - 3 * lh - 40) / 2; cw = chh * 10 / 14; }
    row_x = (m.w - 2 * pad - (5 * cw + 4 * gap)) / 2;
    top_y = lh + 8;
    bot_y = th - chh - lh - 14;
    mid_y = (top_y + chh + bot_y) / 2 - lh / 2;
    table_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(table_obj);
    lv_obj_set_size(table_obj, m.w - 2 * pad, th);
    lv_obj_set_pos(table_obj, pad, top + pad);
    lv_obj_add_event_cb(table_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    update_keys();
    match::restart_view();
    update_keys();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    if (!B) { app_go_home(); return; }
    if (!load(*B)) {
        *B = Board{};
        B->reset(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1);
        match::state() = match::State{};
    }
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    save();
    match::closed();
    table_obj = key_hi = key_lo = key_fr = key_ch = nullptr;
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    now_ms = now;
    const bool was = busy();
    match::tick(now);
    if (!B) return;
    if (was && !busy()) { update_keys(); redraw(); }
    static bool may = false;
    const bool m = match::human_may_move();
    if (m != may) { may = m; update_keys(); }
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
    uint8_t img[kSaveBytes];
    Board* b = new (std::nothrow) Board();
    if (!b) return false;
    match::State st;
    const bool ok = shell().load_game && load_image(img, shell().load_game(kId, img, sizeof img), *b, &st);
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
    const int s = lv_area_get_width(&a);
    const int w = s * 30 / 100, h = s * 44 / 100;
    cards::draw_face(layer, a.x1 + s * 4 / 100, a.y1 + s * 28 / 100, w, h, uint8_t(2 * 13 + 4));   // 5 of diamonds
    cards::draw_face(layer, a.x1 + s * 35 / 100, a.y1 + s * 28 / 100, w, h, uint8_t(0 * 13 + 11)); // Q of spades
    cards::draw_back(layer, a.x1 + s * 66 / 100, a.y1 + s * 28 / 100, w, h);
    kit::text(layer, LV_SYMBOL_UP, &lv_font_montserrat_14, pal().ink, a.x1 + s * 66 / 100, a.y1 + s * 6 / 100, w, s * 20 / 100);
    kit::text(layer, LV_SYMBOL_DOWN, &lv_font_montserrat_14, pal().ink, a.x1 + s * 66 / 100, a.y1 + s * 74 / 100, w, s * 20 / 100);
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

namespace cardsharks_preview {
csh::Board* board() { return B; }
void redraw_all() { update_keys(); redraw(); match::refresh(); }
}

namespace games {
extern const GameOps cardsharks_ops;
const GameOps cardsharks_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
