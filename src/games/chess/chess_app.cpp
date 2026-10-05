// Chess: registry entry, save file and screen. Rules and the computer in
// chess_core.*, turns / menus in the shared match controller, the board in
// board8. Pieces are the chess symbols of DejaVu Sans (see
// THIRD_PARTY_NOTICES.md), drawn twice: the solid shape in the piece's
// color, then the outline shape on top in the other color.
//
// Moving: tap one of your pieces (its squares show as dots), then tap where
// it goes. A pawn reaching the last rank asks what it becomes. Long-press
// any piece, either side, to see where it can move (Tom's choice).
#include <cstdio>
#include <new>
#include "chess_core.h"
#include "games/common/board8.h"
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

extern "C" {
extern const lv_font_t chess_font_33;
extern const lv_font_t chess_font_46;
}

namespace {

using namespace chess;
constexpr const char* kId = "chess";
Game* G = nullptr;

int  sel = -1;                         // picked piece
bool peek = false;                     // showing a piece's moves, not a pick
int  promo_from = -1, promo_to = -1;   // waiting for the promotion choice

// Solid (U+265A..F) and outline (U+2654..9) symbols, by piece (Pawn = 1 .. King = 6)
const char* const kSolid[7]   = {"", "\xE2\x99\x9F", "\xE2\x99\x9E", "\xE2\x99\x9D", "\xE2\x99\x9C", "\xE2\x99\x9B", "\xE2\x99\x9A"};
const char* const kOutline[7] = {"", "\xE2\x99\x99", "\xE2\x99\x98", "\xE2\x99\x97", "\xE2\x99\x96", "\xE2\x99\x95", "\xE2\x99\x94"};
// Black King, Queen, Bishop, Pawn (tools/make_chess_font.py): thinner light
// lines at U+E000.. than the white pieces' outline; Knight and Rook are
// DejaVu's and use the outline above
const char* const kBlackLines[7] = {"", "\xEE\x80\x85", nullptr, "\xEE\x80\x83", nullptr, "\xEE\x80\x81", "\xEE\x80\x80"};

uint32_t tick_ms() { return lv_tick_get(); }

// ---- Rules for the match controller ----------------------------------------------
int  result() { return G->result(); }
int  turn()   { return G->turn(); }
int  moves()  { return G->plies; }
void reset()  { kit::renew(*G); sel = -1; peek = false; }
void redraw();
// Moves reach the match controller as keys (move_key: from, to, promotion)
void play(int key) { G->play(find_key(*G, uint32_t(key))); sel = -1; peek = false; redraw(); }
int  key_of(int index)
{
    MoveList l;
    G->legal(l);
    return index >= 0 && index < l.n ? int(move_key(l.m[index])) : -1;
}
int  think(int level, uint32_t seed, volatile bool* stop) { return key_of(best_move(*G, level, seed, tick_ms, stop)); }
int  list(int* out, int cap)
{
    MoveList l;
    G->legal(l);
    int n = 0;
    for (int k = 0; k < l.n && n < cap; ++k) out[n++] = int(move_key(l.m[k]));
    return n;
}
void note(char* buf, size_t cap)
{
    const End e = G->end();
    if (e != End::None && e != End::Checkmate) snprintf(buf, cap, "%s", end_text(e));
    else if (e == End::Checkmate) snprintf(buf, cap, "Checkmate");
    else if (G->pos.in_check(G->pos.side)) snprintf(buf, cap, "%s is in check", G->pos.side ? "Black" : "White");
    else buf[0] = 0;
}

match::Game make_game()
{
    match::Game g{kId, "Chess", {"White", "Black"}, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.ai_stack = 32 * 1024;
    g.legal = [](int key) { return key >= 0 && find_key(*G, uint32_t(key)) >= 0; };
    g.list = list;
    return g;
}

// ---- Board -------------------------------------------------------------------------------
void draw_symbol(lv_layer_t* layer, int piece, int side, int cx, int cy, int size)
{
    const ui::Palette& P = ui::pal();
    const bool big = size >= 36;
    const lv_font_t* f = big ? &chess_font_46 : &chess_font_33;
    const int em = big ? 46 : 33;
    const lv_color_t body = side == 0 ? P.stone_light : P.stone_dark;
    const lv_color_t line = side == 0 ? P.stone_dark : P.stone_light;
    // Centre the piece itself (it stands on the baseline, 0.73 em tall),
    // not the font's line box
    const int lh = lv_font_get_line_height(f);
    const int baseline = cy + em * 365 / 1000;
    const int top = baseline - (lh - f->base_line);
    kit::text(layer, kSolid[piece], f, body, cx - size, top, 2 * size, lh);
    const char* lines = side == 1 && kBlackLines[piece] ? kBlackLines[piece] : kOutline[piece];
    kit::text(layer, lines, f, line, cx - size, top, 2 * size, lh);
}

void draw_piece(lv_layer_t* layer, int sq, int cx, int cy, int size)
{
    if (!G || !G->pos.sq[sq]) return;
    draw_symbol(layer, piece_of(G->pos.sq[sq]), side_of(G->pos.sq[sq]), cx, cy, size);
}

void redraw()
{
    if (!G) return;
    board8::Marks m;
    if (G->has_last) m.last = board8::bit(G->last.from) | board8::bit(G->last.to);
    if (G->result() == -1 && G->pos.in_check(G->pos.side)) m.warn = board8::bit(G->pos.king[G->pos.side]);
    if (sel >= 0) {
        MoveList l;
        if (peek) moves_from(G->pos, sel, l);
        else      G->legal(l);
        m.selected = sel;
        for (int k = 0; k < l.n; ++k) if (l.m[k].from == sel) m.targets |= board8::bit(l.m[k].to);
    }
    board8::set_marks(m);
}

void clear_pick() { sel = -1; peek = false; }

// ---- Promotion ------------------------------------------------------------------------------
void promo_cb(lv_event_t* e)
{
    const int piece = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    ui::close_overlays();
    const int from = promo_from, to = promo_to;
    promo_from = promo_to = -1;
    if (!piece || !G) { clear_pick(); redraw(); return; }       // Cancel
    MoveList l;
    G->legal(l);
    for (int k = 0; k < l.n; ++k)
        if (l.m[k].from == from && l.m[k].to == to && l.m[k].promo == piece) { match::human_move(int(move_key(l.m[k]))); return; }
}

void ask_promotion(int from, int to)
{
    promo_from = from;
    promo_to = to;
    ui::overlay_begin("Promote To");
    static const char* const names[4] = {"Queen", "Rook", "Bishop", "Knight"};
    static const int pieces[4] = {Queen, Rook, Bishop, Knight};
    for (int k = 0; k < 4; k += 2) {
        lv_obj_t* row = lv_obj_create(ui::overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), ui::menu_btn_h());
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        for (int j = k; j < k + 2; ++j) {
            lv_obj_t* b = ui::make_key(row, 10, ui::menu_btn_h(), promo_cb, pieces[j]);
            lv_obj_set_flex_grow(b, 1);
            ui::key_label(b, names[j], ui::menu_font());
        }
    }
    ui::overlay_bottom_button("Cancel", promo_cb, 0);
}

// ---- Taps ---------------------------------------------------------------------------------------
void on_tap(int sq)
{
    if (!G) return;
    // A tap acts on what's shown: a move for the picked piece, else a pick.
    // Any piece shows where it can go (Tom, 2026-10-03): your own piece is
    // picked (dots), the other side's - or any piece while you can't move -
    // is only shown, and the next tap clears that view.
    if (peek) { clear_pick(); redraw(); return; }
    const bool may = match::human_may_move();
    MoveList l;
    G->legal(l);
    if (may && sel >= 0) {
        int found = -1, count = 0;
        for (int k = 0; k < l.n; ++k)
            if (l.m[k].from == sel && l.m[k].to == sq) { found = k; ++count; }
        if (count == 1) { match::human_move(int(move_key(l.m[found]))); return; }
        if (count > 1) { ask_promotion(sel, sq); return; }
    }
    const uint8_t c = G->pos.sq[sq];
    if (c && may && side_of(c) == G->turn()) {
        bool has = false;
        for (int k = 0; k < l.n; ++k) has |= l.m[k].from == sq;
        sel = has ? sq : -1;
        if (!has) ui::sound(ui::Sound::Error);
    } else if (c && sq != sel) {
        sel = sq;                               // just a look at its moves
        peek = true;
    } else {
        clear_pick();
    }
    redraw();
}

// ---- Save --------------------------------------------------------------------------------
constexpr size_t kSaveBytes = Game::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_plies = -1;

void save()
{
    if (!G || !ui::shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = G->serialize(buf, kSaveBytes);
    match::save_state(buf + n, kSaveBytes - n);
    ui::shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load(Game& g, match::State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const ui::Shell& H = ui::shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = n == kSaveBytes && g.deserialize(buf, n)
                 && match::read_state(buf + Game::kSaveBytes, match::kStateBytes, st);
    delete[] buf;
    return ok;
}

bool flipped() { return match::my_side() == 1; }        // your side at the bottom
bool shown_flipped = false;

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const ui::Metrics& m = ui::metrics();
    const int margin = m.large ? 4 : 2;
    board8::Config cfg;
    cfg.style = board8::Style::Checkered;
    cfg.flipped = shown_flipped = flipped();
    cfg.draw_piece = draw_piece;
    cfg.on_tap = on_tap;
    board8::create(lv_screen_active(), margin, top, m.w - 2 * margin, bottom - top, cfg);
    match::restart_view();
    redraw();
}

// ---- Registry entry ------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { ui::app_go_home(); return; }
    if (!load(*G, match::state())) { kit::renew(*G); match::state() = match::State{}; }
    clear_pick();
    promo_from = promo_to = -1;
    saved_plies = G->plies;
    build();
}

void close()
{
    if (!G) return;
    match::detach();
    save();
    match::closed();
    board8::forget();
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (!G) return;
    if (flipped() != shown_flipped) { shown_flipped = flipped(); board8::set_flipped(shown_flipped); }
    if (G->plies != saved_plies || kit::save_due(now, last_save_ms, match::state().seconds)) {
        if (G->plies != saved_plies) { clear_pick(); redraw(); match::refresh(); }
        saved_plies = G->plies;
        last_save_ms = now;
        save();
    }
}

void restyle() { if (G) { match::detach(); board8::forget(); build(); } }

bool summary(char* buf, size_t cap)
{
    if (G) { match::summary(buf, cap); return true; }
    Game* g = new (std::nothrow) Game();
    match::State st;
    const bool ok = g && load(*g, st);
    if (ok) match::describe(st, g->result(), g->plies, {"White", "Black"}, buf, cap);
    delete g;
    return ok;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    const int size = lv_area_get_width(&a), c = size / 2;
    const int ox = a.x1 + (size - 2 * c) / 2, oy = a.y1 + (size - 2 * c) / 2;
    for (int i = 0; i < 4; ++i) {
        const int x = ox + (i % 2) * c, y = oy + (i / 2) * c;
        kit::fill_rect(layer, x, y, x + c - 1, y + c - 1, ((i % 2) + (i / 2)) & 1 ? P.sq_dark : P.sq_light);
    }
    draw_symbol(layer, Knight, 0, ox + c / 2, oy + c / 2, c);
    draw_symbol(layer, King, 1, ox + c + c / 2, oy + c + c / 2, c);
    draw_symbol(layer, Pawn, 1, ox + c + c / 2, oy + c / 2, c);
    draw_symbol(layer, Queen, 0, ox + c / 2, oy + c + c / 2, c);
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
extern const GameOps chess_ops;
const GameOps chess_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
