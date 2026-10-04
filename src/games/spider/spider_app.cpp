// Spider solitaire: registry entry, save file and screen. Rules in
// spider_core.*; cards and the win show in common/cards.*.
//
// Screen: top bar (clock, score, ☰); the table: finished runs (one King
// each) and the stock (one back per deal left) - the stock in the top
// corner on the stylus hand's side (Settings), the runs in the other - the ten
// columns; Undo | Hint. Tap a card in a column's top run (it and the cards
// on it are picked), then tap the column it goes to; tap the picked card
// again to send it to the best column (its own suit first). Tap the stock
// to deal a row.
//
// Sounds: none while playing (card games are quiet) except a soft "aww"
// (Error) for a move that isn't allowed; Fanfare when won.
#include <cstdio>
#include <new>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "spider_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace spider;
using namespace ui;

constexpr const char* kId = "spider";
const char* const kLevels[3] = {"1 Suit", "2 Suits", "4 Suits"};

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   table = nullptr;
lv_obj_t*   undo_k = nullptr;
lv_obj_t*   hint_k = nullptr;
lv_obj_t*   again_k = nullptr;
int cw = 0, ch = 0, pitch = 0, x0 = 0, row_y = 0, tab_y = 0, tab_bottom = 0, down_step = 0, up_step = 0;
int sel_col = -1, sel_idx = -1, hint_to = -1;
bool hint_deal = false, note_empty = false;
uint32_t last_save_ms = 0;

void build();
void open_menu();

// ---- Save -----------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = S->g.serialize(buf, kSaveBytes);
    buf[n] = S->recorded;
    for (int k = 0; k < 4; ++k) buf[n + 1 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) {
        st.recorded = buf[Game::kSaveBytes];
        st.seconds = 0;
        for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 1 + k]) << (8 * k);
    }
    delete[] buf;
    return ok;
}

// ---- Geometry -----------------------------------------------------------------------------------
int col_x(int c) { return x0 + c * pitch; }

void steps(int c, int* d, int* u)
{
    const Game& g = S->g;
    const int downs = g.first_up(c), ups = g.n[c] - downs;
    *d = down_step;
    *u = up_step;
    const int room = tab_bottom - tab_y - ch;
    if (ups > 1 && downs * *d + (ups - 1) * *u > room) {
        const int min_u = cards::index_h(cw, ch) * 2 / 3;
        *u = (room - downs * *d) / (ups - 1);
        if (*u < min_u) {
            *u = min_u;
            *d = downs ? (room - (ups - 1) * *u) / downs : *d;
            if (*d < 2) *d = 2;
        }
    }
}

int card_y(int c, int i)
{
    int d, u;
    steps(c, &d, &u);
    const int downs = S->g.first_up(c);
    return tab_y + (i < downs ? i * d : downs * d + (i - downs) * u);
}

int deals_left() { return S->g.stock_n / kCols; }
// The stock goes in the top corner on the stylus hand's side, the finished
// runs in the other one (Settings -> Right Hand / Left Hand)
bool rh = true;
int stock_w()    { return cw + (deals_left() > 0 ? deals_left() - 1 : 0) * (cw / 4); }
int stock_x()    { return rh ? col_x(kCols - 1) + cw - stock_w() : col_x(0); }
int run_x(int k) { return (rh ? col_x(0) : col_x(kCols - 1) - 7 * (cw / 2 + 1)) + k * (cw / 2 + 1); }

// ---- Drawing -------------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 0);
    const int ox = a.x1, oy = a.y1;
    // Finished runs: their Kings, overlapping
    for (int k = 0; k < 8; ++k) {
        const int x = ox + run_x(k);
        if (k < g.done) cards::draw_face(layer, x, oy + row_y, cw, ch, uint8_t(g.done_suit[k] * 13 + 12));
        else if (k == g.done) cards::draw_slot(layer, x, oy + row_y, cw, ch);
    }
    // Stock: a back for each deal left
    const int dl = deals_left();
    for (int k = 0; k < dl; ++k) cards::draw_back(layer, ox + stock_x() + k * (cw / 4), oy + row_y, cw, ch);
    if (hint_deal && dl) kit::fill_rect(layer, ox + stock_x(), oy + row_y + ch - 4, ox + stock_x() + stock_w() - 1, oy + row_y + ch - 1, pal().target, 2);
    if (note_empty) kit::text(layer, "Fill every column first", &lv_font_montserrat_12, pal().stone_light,
                              ox + col_x(0), oy + row_y + ch + 1, col_x(kCols - 1) + cw - col_x(0), 14);
    // Columns
    for (int c = 0; c < kCols; ++c) {
        const int x = ox + col_x(c);
        if (!g.n[c]) cards::draw_slot(layer, x, oy + tab_y, cw, ch);
        for (int i = 0; i < g.n[c]; ++i) {
            const int y = oy + card_y(c, i);
            if (!up(g.col[c][i])) cards::draw_back(layer, x, y, cw, ch);
            else cards::draw_face(layer, x, y, cw, ch, uint8_t(g.col[c][i] & 0x3F), c == sel_col && i >= sel_idx);
        }
        if (hint_to == c) {
            const int y = oy + (g.n[c] ? card_y(c, g.n[c] - 1) : tab_y) + ch - 4;
            kit::fill_rect(layer, x, y, x + cw - 1, y + 3, pal().target, 2);
        }
    }
}

// ---- Flow ----------------------------------------------------------------------------------------
// Clock ticks only change the top bar: the board isn't redrawn for them
// (a full card table redraw every second slowed taps down - Tom).
bool ticking = false;

void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (g.won())            snprintf(s, sizeof s, "Won! Score %d", g.score);
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Score %d", g.score);
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !g.won());
    lv_obj_set_hidden(undo_k, g.won());
    lv_obj_set_hidden(hint_k, g.won());
    set_dim(undo_k, g.log_n == 0);
    if (!ticking) lv_obj_invalidate(table);
}

void record(bool won)
{
    puzzle::Record r;
    r.level = S->g.level;
    r.solved = won;
    r.lost = !won;
    r.moves = S->g.moves;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void clear_marks() { sel_col = sel_idx = hint_to = -1; hint_deal = false; note_empty = false; }

void deal_new(int level, bool same)
{
    cards::celebrate_stop();
    kit::flash_stop();
    if (!S->recorded && S->g.moves > 0) record(false);
    const uint32_t seed = same ? S->g.seed : (shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.deal(seed, level);
    S->recorded = 0;
    S->seconds = 0;
    clear_marks();
    save();
    build();
}

void show_done() { update_status(); }

void after_change(uint8_t done_before)
{
    clear_marks();
    Game& g = S->g;
    if (g.won() && !S->recorded) {
        S->recorded = 1;
        record(true);
        sound(Sound::Fanfare);
        save();
        update_status();
        lv_area_t a;
        lv_obj_get_coords(table, &a);
        cards::Launch* list = new (std::nothrow) cards::Launch[104];
        if (!list) return;
        int n = 0;
        for (int r = 13; r >= 1; --r)
            for (int k = 0; k < 8; ++k)
                list[n++] = cards::Launch{int16_t(a.x1 + run_x(k)), int16_t(a.y1 + row_y),
                                          uint8_t(g.done_suit[k] * 13 + r - 1),
                                          uint8_t(r > 1 ? g.done_suit[k] * 13 + r - 2 : 0xFE)};
        cards::celebrate(list, n, cw, ch, show_done);
        delete[] list;
        return;
    }
    save();
    update_status();
}

void table_cb(lv_event_t*)
{
    if (!S || overlay_open() || S->g.won()) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    const int x = pt.x - a.x1, y = pt.y - a.y1;
    Game& g = S->g;
    const uint8_t done_before = g.done;
    if (y >= row_y && y < row_y + ch && (rh ? x >= stock_x() : x < stock_x() + stock_w()) && deals_left()) {
        clear_marks();
        if (g.deal_row()) {after_change(done_before); }
        else { note_empty = true; sound(Sound::Error); update_status(); }
        return;
    }
    if (y < tab_y - 2) { clear_marks(); update_status(); return; }
    int c = -1;
    for (int k = 0; k < kCols; ++k) if (x >= col_x(k) - 1 && x < col_x(k) + cw + 1) c = k;
    if (c < 0) return;
    int i = -1;
    for (int k = g.n[c] - 1; k >= 0; --k) if (y >= card_y(c, k)) { i = k; break; }
    if (i < 0 && g.n[c]) i = 0;
    if (sel_col >= 0) {
        const int sc = sel_col, si = sel_idx;
        if (c == sc && i == si) {
            const int to = g.best_target(sc, si);
            if (to >= 0 && g.move(sc, si, to)) { after_change(done_before); return; }
            clear_marks(); sound(Sound::Error); update_status(); return;
        }
        if (g.move(sc, si, c)) { after_change(done_before); return; }
        if (i >= 0 && i >= g.run_start(c)) { sel_col = c; sel_idx = i; hint_to = -1; update_status(); return; }
        clear_marks(); sound(Sound::Error); update_status(); return;
    }
    clear_marks();
    if (i >= 0 && i >= g.run_start(c) && up(g.col[c][i])) { sel_col = c; sel_idx = i; }
    update_status();
}

void undo_cb(lv_event_t*)
{
    if (!S || S->g.won() || !S->g.undo()) return;
    clear_marks();

    save();
    update_status();
}

void hint_cb(lv_event_t*)
{
    if (!S) return;
    clear_marks();
    int f, i, t;
    if (!S->g.hint(&f, &i, &t)) {update_status(); return; }
    if (f < 0) hint_deal = true;
    else { sel_col = f; sel_idx = i; hint_to = t; }

    update_status();
}

void again_cb(lv_event_t*) { deal_new(S->g.level, false); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 6;
    const int ky = m.h - pad - kh, half = (m.w - 2 * pad - gap) / 2;
    table = lv_obj_create(scr);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, m.w, ky - gap / 2 - bar.h);
    lv_obj_set_pos(table, 0, bar.h);
    lv_obj_set_clickable(table, true);
    lv_obj_add_event_cb(table, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(table, table_cb, LV_EVENT_PRESSED, nullptr);
    undo_k = make_key(scr, half, kh, undo_cb, 0);
    key_label(undo_k, "Undo", menu_font());
    lv_obj_set_pos(undo_k, pad, ky);
    hint_k = make_key(scr, half, kh, hint_cb, 0);
    key_label(hint_k, "Hint", menu_font());
    lv_obj_set_pos(hint_k, m.w - pad - half, ky);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);

    const int mg = m.large ? 3 : 1;
    rh = right_handed();
    pitch = (m.w - 2 * mg) / kCols;
    cw = pitch - 2;
    ch = cw * 7 / 5;
    x0 = mg + (m.w - 2 * mg - kCols * pitch) / 2 + 1;
    row_y = m.large ? 6 : 4;
    tab_y = row_y + ch + (m.large ? 18 : 16);          // room for the "fill every column" note
    tab_bottom = ky - gap / 2 - bar.h - 2;
    down_step = ch / 6 > 4 ? ch / 6 : 4;
    up_step = cards::index_h(cw, ch) + 2;
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu ----------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id <= kit::kLevel2) deal_new(id, false);
    else if (id == kit::kRestart) deal_new(S->g.level, true);
    else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu, true); }
void menu_back()  { clear_marks(); update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Spider", kLevels, h, true, "Card Back");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { S->g.deal(shell().random_seed ? shell().random_seed() : lv_tick_get(), 0); S->recorded = 0; S->seconds = 0; }
    clear_marks();
    build();
}

void close()
{
    if (!S) return;
    cards::celebrate_stop();
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    table = undo_k = hint_k = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.won() && S->g.moves > 0, S->seconds) && !cards::celebrating()) { ticking = true; update_status(); ticking = false; }
    if (kit::save_due(now, last_save_ms, S->seconds) && !cards::celebrating()) { last_save_ms = now; save(); }
}

void restyle() { if (S) { cards::celebrate_stop(); build(); } }

bool summary(char* buf, size_t cap)
{
    State* tmp = nullptr;
    const State* st = S;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    const Game& g = st->g;
    if (g.won()) snprintf(buf, cap, "%s, won", kLevels[g.level]);
    else         snprintf(buf, cap, "%s, %d of 8 runs", kLevels[g.level], g.done);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a short spade run
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 58 / 100, h = size * 70 / 100;
    for (int i = 0; i < 3; ++i)
        cards::draw_face(layer, a.x1 + i * (size - w) / 2, a.y1 + i * (size - h) / 2, w, h, cards::make(13 - i, cards::Spades));
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
extern const GameOps spider_ops;
const GameOps spider_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
