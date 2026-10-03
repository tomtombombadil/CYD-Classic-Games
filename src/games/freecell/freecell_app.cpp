// FreeCell: registry entry, save file and screen. Rules in freecell_core.*;
// cards and the win show in common/cards.*.
//
// Screen: top bar (clock, cards up, ☰); the table: four free cells (top
// left), the foundations (top right: Spades, Hearts, Clubs, Diamonds), the
// eight columns; Undo | Hint. Tap a card in a column's top run (or a free
// cell's card) to pick it (gold), then tap where it goes: a column, the
// free-cell row (the first empty cell), or the foundation row (its suit).
// A second tap on the picked card sends it to its foundation, else to a
// column, else a free cell. Safe cards go up by themselves.
//
// Sounds: none while playing (card games are quiet); Fanfare when won.
#include <cstdio>
#include <new>
#include "freecell_core.h"
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace freecell;
using namespace ui;

constexpr const char* kId = "freecell";
const char* const kLevels[3] = {"FreeCell", "", ""};

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
int cw = 0, ch = 0, pitch = 0, x0 = 0, row_y = 0, tab_y = 0, tab_bottom = 0, up_step = 0;
int sel_pile = -1, sel_idx = -1, hint_to = -1;
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

// ---- Geometry ----------------------------------------------------------------------------------
int col_x(int i) { return x0 + i * pitch; }

int step_for(int c)
{
    const int k = S->g.n[c];
    if (k <= 1) return up_step;
    const int room = tab_bottom - tab_y - ch;
    const int s = room / (k - 1);
    return s < up_step ? s : up_step;
}

int card_y(int c, int i) { return tab_y + i * step_for(c); }

// ---- Drawing ------------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 0);
    const int ox = a.x1, oy = a.y1;
    for (int i = 0; i < 4; ++i) {
        const int x = ox + col_x(i), y = oy + row_y;
        if (g.cell[i] == 0xFF) cards::draw_slot(layer, x, y, cw, ch);
        else cards::draw_face(layer, x, y, cw, ch, g.cell[i], sel_pile == Cell0 + i);
        if (hint_to == Cell0 + i) kit::fill_rect(layer, x, y + ch - 4, x + cw - 1, y + ch - 1, pal().target, 2);
    }
    for (int f = 0; f < 4; ++f) {
        const int x = ox + col_x(4 + f), y = oy + row_y;
        if (!g.found[f]) cards::draw_slot(layer, x, y, cw, ch, kFoundSuit[f]);
        else cards::draw_face(layer, x, y, cw, ch, uint8_t(kFoundSuit[f] * 13 + g.found[f] - 1));
        if (hint_to == Found0 + f) kit::fill_rect(layer, x, y + ch - 4, x + cw - 1, y + ch - 1, pal().target, 2);
    }
    for (int c = 0; c < 8; ++c) {
        const int x = ox + col_x(c);
        if (!g.n[c]) cards::draw_slot(layer, x, oy + tab_y, cw, ch);
        for (int i = 0; i < g.n[c]; ++i)
            cards::draw_face(layer, x, oy + card_y(c, i), cw, ch, g.col[c][i], sel_pile == Col0 + c && i >= sel_idx);
        if (hint_to == Col0 + c) {
            const int y = oy + (g.n[c] ? card_y(c, g.n[c] - 1) : tab_y) + ch - 4;
            kit::fill_rect(layer, x, y, x + cw - 1, y + 3, pal().target, 2);
        }
    }
}

// ---- Flow ----------------------------------------------------------------------------------------
void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    const int up = g.found[0] + g.found[1] + g.found[2] + g.found[3];
    if (g.won())            snprintf(s, sizeof s, "Won in %u moves", unsigned(g.moves));
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "%d of 52 up", up);
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !g.won());
    lv_obj_set_hidden(undo_k, g.won());
    lv_obj_set_hidden(hint_k, g.won());
    set_dim(undo_k, g.log_n == 0);
    lv_obj_invalidate(table);
}

void record(bool won)
{
    puzzle::Record r;
    r.level = 0;
    r.solved = won;
    r.lost = !won;
    r.moves = S->g.moves;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void clear_marks() { sel_pile = sel_idx = hint_to = -1; }

void deal(bool same)
{
    cards::celebrate_stop();
    kit::flash_stop();
    if (!S->recorded && S->g.moves > 0) record(false);
    const uint32_t seed = same ? S->g.seed : (shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.deal(seed);
    S->recorded = 0;
    S->seconds = 0;
    clear_marks();
    save();
    build();
}

void show_done() { update_status(); }

void after_move()
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
        cards::Launch list[52];
        int n = 0;
        for (int r = 13; r >= 1; --r)
            for (int f = 0; f < 4; ++f)
                list[n++] = cards::Launch{int16_t(a.x1 + col_x(4 + f)), int16_t(a.y1 + row_y), uint8_t(kFoundSuit[f] * 13 + r - 1),
                                          uint8_t(r > 1 ? kFoundSuit[f] * 13 + r - 2 : 0xFF)};
        cards::celebrate(list, n, cw, ch, show_done);
        return;
    }
    save();
    update_status();
}

// Pile and card under a tap; the free-cell row and the foundation row
// come back as "the row" (Cell0 / Found0) with the slot in *idx
bool hit(int x, int y, int* pile, int* idx)
{
    const Game& g = S->g;
    if (y >= row_y && y < row_y + ch) {
        const int k = (x - x0 + (pitch - cw) / 2) / pitch;
        if (k < 0 || k > 7) return false;
        if (k < 4) { *pile = Cell0 + k; *idx = 0; }
        else       { *pile = Found0 + (k - 4); *idx = 0; }
        return true;
    }
    if (y < tab_y - 3) return false;
    for (int c = 0; c < 8; ++c) {
        if (x < col_x(c) - 1 || x >= col_x(c) + cw + 1) continue;
        *pile = Col0 + c;
        *idx = -1;
        for (int i = g.n[c] - 1; i >= 0; --i) if (y >= card_y(c, i)) { *idx = i; break; }
        if (*idx < 0 && g.n[c]) *idx = 0;
        return true;
    }
    return false;
}

bool pickable(int p, int i)
{
    const Game& g = S->g;
    if (p < Found0) return g.cell[p] != 0xFF;
    if (p < Col0) return false;
    const int c = p - Col0;
    return i >= 0 && i < g.n[c] && i >= g.run_start(c);
}

void table_cb(lv_event_t*)
{
    if (!S || overlay_open() || S->g.won()) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    int p, i;
    if (!hit(pt.x - a.x1, pt.y - a.y1, &p, &i)) { clear_marks(); update_status(); return; }
    Game& g = S->g;
    if (sel_pile >= 0) {
        const int sp = sel_pile, si = sel_idx;
        if (p == sp && (i == si || p < Col0)) {                    // second tap: send it
            const int to = g.best_target(sp, si);
            if (to >= 0 && g.move(sp, si, to)) { after_move(); return; }
            clear_marks(); update_status(); return;
        }
        int dest = p;
        if (p >= Found0 && p < Col0) {                             // the foundation row: its suit
            const uint8_t c = sp < Found0 ? g.cell[sp] : g.col[sp - Col0][si];
            dest = found_for(c);
        } else if (p < Found0 && g.cell[p] != 0xFF) {             // the free-cell row: an empty cell
            for (int k = 0; k < 4; ++k) if (g.cell[k] == 0xFF) { dest = Cell0 + k; break; }
        }
        if (g.move(sp, si, dest)) { after_move(); return; }
        if (pickable(p, i)) { sel_pile = p; sel_idx = i; hint_to = -1; update_status(); return; }
        clear_marks(); update_status(); return;
    }
    clear_marks();
    if (pickable(p, i)) { sel_pile = p; sel_idx = p < Found0 ? 0 : i; }
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
    if (S->g.hint(&f, &i, &t)) { sel_pile = f; sel_idx = i; hint_to = t; }
    update_status();
}

void again_cb(lv_event_t*) { deal(false); }

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
    pitch = (m.w - 2 * mg) / 8;
    cw = pitch - 2;
    ch = cw * 7 / 5;
    x0 = mg + (m.w - 2 * mg - 8 * pitch) / 2 + 1;
    row_y = m.large ? 6 : 4;
    tab_y = row_y + ch + (m.large ? 10 : 6);
    tab_bottom = ky - gap / 2 - bar.h - 2;
    up_step = cards::index_h(cw, ch) + 2;
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu -----------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id == kit::kLevel0) deal(false);
    else if (id == kit::kRestart) deal(true);
    else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu, true, 1); }
void menu_back()  { clear_marks(); update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("FreeCell", nullptr, h, true, "Card Back");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { S->g.deal(shell().random_seed ? shell().random_seed() : lv_tick_get()); S->recorded = 0; S->seconds = 0; }
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
    if (clock_.tick(now, !S->g.won() && S->g.moves > 0, S->seconds) && !cards::celebrating()) update_status();
    if (now - last_save_ms > 30000 && !cards::celebrating()) { last_save_ms = now; save(); }
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
    if (g.won()) snprintf(buf, cap, "Won in %u moves", unsigned(g.moves));
    else         snprintf(buf, cap, "%d of 52 cards up", g.found[0] + g.found[1] + g.found[2] + g.found[3]);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: four cards, one sitting in a free cell
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 46 / 100, h = size * 60 / 100;
    cards::draw_slot(layer, a.x1, a.y1, w, h);
    cards::draw_face(layer, a.x1 + size - w, a.y1, w, h, cards::make(1, cards::Hearts));
    cards::draw_face(layer, a.x1 + (size - w) / 2, a.y1 + size - h, w, h, cards::make(7, cards::Clubs));
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
extern const GameOps freecell_ops;
const GameOps freecell_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
