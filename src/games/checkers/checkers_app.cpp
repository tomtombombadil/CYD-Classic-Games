// Checkers: registry entry, save file and screen. Rules in checkers_core.*,
// turns / computer / menus in the shared match controller, the board in
// board8.
//
// Moving: tap one of your pieces (its landing squares show as dots), then
// tap where it goes. A multi-jump is tapped one landing at a time; the
// move is made as soon as the taps pick out exactly one legal move. When a
// jump is possible it must be taken, so only pieces that can jump respond.
// Long-press any piece, yours or the other side's, to see where it could
// go (Tom's choice); the next tap clears that.
#include <cstdio>
#include <cstring>
#include <new>
#include "checkers_core.h"
#include "games/common/board8.h"
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace checkers;
constexpr const char* kId = "checkers";
Game* G = nullptr;

// Tap state
int  sel = -1;                         // picked piece
int  path_n = 0;                       // landings tapped so far
uint8_t path[kMaxPath];
// The pick a long-press peek covers, restored when it ends
int     peek_sel = -1, peek_path_n = 0;
uint8_t peek_path[kMaxPath];
bool peek = false;                     // long-press view on screen
char note_text[40] = "";
uint64_t must_jump = 0;                // pieces that can jump, lit while the note shows

// ---- Rules for the match controller ----------------------------------------------
int  result() { return G->result(); }
int  turn()   { return G->turn(); }
int  moves()  { return G->plies; }
void reset()  { *G = Game{}; sel = -1; path_n = 0; }
void redraw();
void play(int index)
{
    G->play(index);
    sel = -1;
    path_n = 0;
    peek = false;
    note_text[0] = 0; must_jump = 0;
    redraw();
}
int  think(int level, uint32_t seed, volatile bool* stop) { return best_move(*G, level, seed, stop); }
void score(char* buf, size_t cap)
{
    snprintf(buf, cap, "Black %d  White %d", __builtin_popcountll(G->pos.pieces(0)),
             __builtin_popcountll(G->pos.pieces(1)));
}
void note(char* buf, size_t cap) { snprintf(buf, cap, "%s", note_text); }

match::Game make_game()
{
    match::Game g{kId, "Checkers", {"Black", "White"}, result, turn, moves, play, reset, think, redraw};
    g.score = score;
    g.note = note;
    g.ai_stack = 40 * 1024;
    return g;
}

// ---- Board -------------------------------------------------------------------------------
void draw_piece(lv_layer_t* layer, int sq, int cx, int cy, int size)
{
    if (!G) return;
    bool king = false;
    const int who = G->pos.cell(sq, &king);
    if (who < 0) return;
    const ui::Palette& P = ui::pal();
    const int r = size * 40 / 100;
    const lv_color_t body = who == 0 ? P.stone_dark : P.stone_light;
    const lv_color_t edge = who == 0 ? P.stone_light : P.stone_dark;
    kit::fill_circle(layer, cx, cy, r + 1, edge);
    kit::fill_circle(layer, cx, cy, r, body);
    kit::ring(layer, cx, cy, r * 70 / 100, size >= 36 ? 2 : 1, edge);   // the rim of a checker
    if (king) kit::fill_circle(layer, cx, cy, r * 40 / 100, P.key_on);    // a gold crown
}

// Legal moves that start at `from` and follow the landings tapped so far
int matching(const MoveList& l, int from, int* only)
{
    int n = 0;
    for (int k = 0; k < l.n; ++k) {
        const Move& m = l.m[k];
        if (m.from != from || m.n < path_n) continue;
        if (memcmp(m.path, path, path_n) != 0) continue;
        if (only) *only = k;
        ++n;
    }
    return n;
}

void redraw()
{
    if (!G) return;
    board8::Marks m;
    if (G->last_from >= 0) m.last = board8::bit(G->last_from) | board8::bit(G->last_to);
    m.warn = must_jump;                     // "A jump must be taken": these can
    if (sel >= 0) {
        MoveList l;
        if (peek) {
            const int who = G->pos.cell(sel);
            generate(G->pos, who, l);
        } else {
            G->legal(l);
        }
        m.selected = path_n ? path[path_n - 1] : sel;
        for (int k = 0; k < l.n; ++k) {
            const Move& mv = l.m[k];
            if (mv.from != sel || mv.n <= path_n || memcmp(mv.path, path, path_n) != 0) continue;
            m.targets |= board8::bit(mv.path[path_n]);
        }
    }
    board8::set_marks(m);
}

void clear_pick()
{
    sel = -1;
    path_n = 0;
    peek = false;
}

// Back to the pick from before the long-press
void on_long_end_quiet()
{
    sel = peek_sel;
    path_n = peek_path_n;
    memcpy(path, peek_path, sizeof path);
    peek = false;
}

void on_tap(int sq)
{
    if (!G) return;
    if (peek) on_long_end_quiet();               // (press lost mid-peek)
    if (!match::human_may_move()) { redraw(); return; }
    MoveList l;
    G->legal(l);
    if (sel >= 0) {
        // A landing square for the picked piece?
        path[path_n] = static_cast<uint8_t>(sq);
        ++path_n;
        int only = -1;
        const int n = matching(l, sel, &only);
        if (n == 0) {
            --path_n;
        } else {
            // Exactly one move left, and the taps reached its end: play it
            if (n == 1 && l.m[only].n == path_n) { match::human_move(only); return; }
            redraw();
            return;
        }
        if (path_n > 0) { clear_pick(); redraw(); return; }   // mid-jump: tap elsewhere cancels
    }
    // Pick a piece of the side to move that has a move
    if (G->pos.cell(sq) == G->turn()) {
        bool has = false;
        for (int k = 0; k < l.n; ++k) has |= l.m[k].from == sq;
        if (has) {
            sel = sq;
            path_n = 0;
            note_text[0] = 0; must_jump = 0;
        } else {
            clear_pick();
            if (l.n && l.m[0].jump()) {
                snprintf(note_text, sizeof note_text, "A jump must be taken");
                must_jump = 0;
                for (int k = 0; k < l.n; ++k) must_jump |= board8::bit(l.m[k].from);
                ui::sound(ui::Sound::Error);
                match::refresh();                    // show the note
            }
        }
    } else {
        clear_pick();
    }
    redraw();
}

void on_long(int sq)
{
    if (!G || G->pos.cell(sq) < 0) return;
    if (!peek) {
        peek_sel = sel;
        peek_path_n = path_n;
        memcpy(peek_path, path, sizeof path);
    }
    sel = sq;
    path_n = 0;
    peek = true;
    redraw();
}

void on_long_end()
{
    if (!peek) return;
    on_long_end_quiet();
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

bool flipped() { return match::state().mode == twoplayer::Mode::Computer && match::state().human_side == 1; }

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
    cfg.flipped = flipped();
    cfg.draw_piece = draw_piece;
    cfg.on_tap = on_tap;
    cfg.on_long_press = on_long;
    cfg.on_long_end = on_long_end;
    board8::create(lv_screen_active(), margin, top, m.w - 2 * margin, bottom - top, cfg);
    match::restart_view();
    redraw();
}

// ---- Registry entry ------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { ui::app_go_home(); return; }
    if (!load(*G, match::state())) { *G = Game{}; match::state() = match::State{}; }
    clear_pick();
    note_text[0] = 0; must_jump = 0;
    saved_plies = G->plies;
    build();
}

void close()
{
    if (!G) return;
    match::detach();
    save();
    board8::forget();
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (!G) return;
    // A new game may have changed which side the player has
    static bool was_flipped = false;
    if (flipped() != was_flipped) { was_flipped = flipped(); board8::set_flipped(was_flipped); }
    if (G->plies != saved_plies || now - last_save_ms > 30000) {
        if (G->plies != saved_plies) { clear_pick(); redraw(); }
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
    if (ok) match::describe(st, g->result(), g->plies, {"Black", "White"}, buf, cap);
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
    const int size = lv_area_get_width(&a), c = size / 4;
    const int ox = a.x1 + (size - 4 * c) / 2, oy = a.y1 + (size - 4 * c) / 2;
    for (int i = 0; i < 16; ++i) {
        const int x = ox + (i % 4) * c, y = oy + (i / 4) * c;
        kit::fill_rect(layer, x, y, x + c - 1, y + c - 1, ((i % 4) + (i / 4)) & 1 ? P.sq_dark : P.sq_light);
    }
    static const int8_t art[16] = {-1, 1, -1, 1, -1, -1, -1, -1, -1, -1, -1, -1, 0, -1, 2, -1};
    for (int i = 0; i < 16; ++i) {
        if (art[i] < 0) continue;
        const int cx = ox + (i % 4) * c + c / 2, cy = oy + (i / 4) * c + c / 2, r = c * 40 / 100;
        const bool dark = art[i] != 1;
        kit::fill_circle(layer, cx, cy, r + 1, dark ? P.stone_light : P.stone_dark);
        kit::fill_circle(layer, cx, cy, r, dark ? P.stone_dark : P.stone_light);
        if (art[i] == 2) kit::fill_circle(layer, cx, cy, r * 40 / 100, P.key_on);
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
extern const GameOps checkers_ops;
const GameOps checkers_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
