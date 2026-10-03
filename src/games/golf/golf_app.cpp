// Golf solitaire: registry entry, save file and screen. Rules in
// golf_core.*; cards and the win show in common/cards.*.
//
// Screen: top bar (clock, cards left, ☰); the table: seven columns of five
// cards, then the stock and the waste (bigger cards); Undo | Hint. Tap a
// column to play its top card onto the waste, tap the stock to turn a card.
//
// Sounds: none while playing (card games are quiet) except a soft "aww"
// (Error) for a card that can't play; Fanfare when won.
#include <cstdio>
#include <new>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "golf_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace golf;
using namespace ui;

constexpr const char* kId = "golf";
const char* const kLevels[3] = {"Golf", "", ""};

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
int cw = 0, ch = 0, pitch = 0, x0 = 0, col_y = 0, up = 0;
int bw = 0, bh = 0, stock_x = 0, waste_x = 0, low_y = 0;     // the stock and waste (bigger)
int hint_col = -1;
uint32_t last_save_ms = 0;

void build();
void open_menu();

// ---- Save ----------------------------------------------------------------------------------
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

// ---- Drawing ---------------------------------------------------------------------------------
int col_x(int c) { return x0 + c * pitch; }

void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 0);
    for (int c = 0; c < kCols; ++c) {
        const int x = a.x1 + col_x(c);
        if (!g.col_n[c]) cards::draw_slot(layer, x, a.y1 + col_y, cw, ch);
        for (int i = 0; i < g.col_n[c]; ++i) {
            const bool top = i == g.col_n[c] - 1;
            cards::draw_face(layer, x, a.y1 + col_y + i * up, cw, ch, g.col[c][i], top && c == hint_col);
        }
    }
    // Stock with its count, waste top (and the card under it peeking out)
    if (g.stock_n) {
        cards::draw_back(layer, a.x1 + stock_x, a.y1 + low_y, bw, bh);
        char n[8];
        snprintf(n, sizeof n, "%d", g.stock_n);
        kit::text(layer, n, bar_font(), pal().stone_light, a.x1 + stock_x, a.y1 + low_y + bh + 2, bw, 20);
        if (hint_col == 7) kit::fill_rect(layer, a.x1 + stock_x, a.y1 + low_y + bh - 4, a.x1 + stock_x + bw - 1, a.y1 + low_y + bh - 1, pal().target, 2);
    } else {
        cards::draw_slot(layer, a.x1 + stock_x, a.y1 + low_y, bw, bh);
    }
    if (g.waste_n > 1) cards::draw_face(layer, a.x1 + waste_x - bw / 3, a.y1 + low_y, bw, bh, g.waste[g.waste_n - 2]);
    if (g.waste_n) cards::draw_face(layer, a.x1 + waste_x, a.y1 + low_y, bw, bh, g.waste[g.waste_n - 1]);
}

// ---- Flow ----------------------------------------------------------------------------------------
bool over() { return S->g.won() || S->g.stuck(); }

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
    if (g.won())            snprintf(s, sizeof s, "All cleared!");
    else if (g.stuck())     snprintf(s, sizeof s, "No moves: %d left", g.left());
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Cards left %d", g.left());
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !over());
    lv_obj_set_hidden(undo_k, over() && g.won());
    lv_obj_set_hidden(hint_k, over());
    set_dim(undo_k, g.log_n == 0);
    const Metrics& m = metrics();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6, half = (m.w - 2 * pad - gap) / 2;
    lv_obj_set_width(again_k, g.won() ? m.w - 2 * pad : half);
    lv_obj_set_x(again_k, g.won() ? pad : m.w - pad - half);
    if (!ticking) lv_obj_invalidate(table);
}

void record(bool won)
{
    puzzle::Record r;
    r.level = 0;
    r.solved = won;
    r.lost = !won;
    r.moves = S->g.log_n;
    r.par = uint16_t(S->g.left());
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void deal(bool same)
{
    cards::celebrate_stop();
    kit::flash_stop();
    if (!S->recorded && S->g.log_n > 0) record(false);
    const uint32_t seed = same ? S->g.seed : (shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.deal(seed);
    S->recorded = 0;
    S->seconds = 0;
    hint_col = -1;
    save();
    build();
}

void show_done() { update_status(); }

void after_change()
{
    hint_col = -1;
    Game& g = S->g;
    // A stuck deal is recorded (as lost) only when the next one starts:
    // Undo can still get it going again
    if (g.won() && !S->recorded) {
        S->recorded = 1;
        record(true);
        {
            sound(Sound::Fanfare);
            update_status();
            lv_area_t a;
            lv_obj_get_coords(table, &a);
            cards::Launch list[52];
            int n = 0;
            for (int i = g.waste_n - 1; i >= 0 && n < 52; --i)
                list[n++] = cards::Launch{int16_t(a.x1 + waste_x), int16_t(a.y1 + low_y), g.waste[i],
                                          uint8_t(i ? g.waste[i - 1] : 0xFF)};
            cards::celebrate(list, n, bw, bh, show_done);
            save();
            return;
        }
    }
    save();
    update_status();
}

void table_cb(lv_event_t*)
{
    if (!S || overlay_open() || S->g.won()) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    const int x = p.x - a.x1, y = p.y - a.y1;
    Game& g = S->g;
    if (y >= low_y && x >= stock_x && x < stock_x + bw) {
        if (g.draw()) after_change();
        else sound(Sound::Error);
        return;
    }
    if (y < low_y - 4) {
        for (int c = 0; c < kCols; ++c)
            if (x >= col_x(c) - 1 && x < col_x(c) + cw + 1) {
                if (g.play(c)) after_change();
                else if (g.col_n[c]) sound(Sound::Error);
                return;
            }
    }
}

void undo_cb(lv_event_t*)
{
    if (!S || S->g.won() || !S->g.undo()) return;
    hint_col = -1;

    save();
    update_status();
}

void hint_cb(lv_event_t*)
{
    if (!S) return;
    hint_col = S->g.hint();
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
    again_k = make_key(scr, half, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, m.w - pad - half, ky);

    const int mg = m.large ? 4 : 2;
    pitch = (m.w - 2 * mg) / kCols;
    cw = pitch - (m.large ? 4 : 3);
    ch = cw * 7 / 5;
    x0 = mg + (m.w - 2 * mg - kCols * pitch) / 2 + (pitch - cw) / 2;
    col_y = m.large ? 8 : 5;
    up = cards::index_h(cw, ch) + (m.large ? 6 : 4);
    const int table_h = ky - gap / 2 - bar.h;
    low_y = col_y + (kDepth - 1) * up + ch + (m.large ? 18 : 10);
    bh = table_h - low_y - (m.large ? 30 : 22);       // room for the stock count under it
    bw = bh * 5 / 7;
    if (bw > cw * 2) { bw = cw * 2; bh = bw * 7 / 5; }
    stock_x = m.w / 2 - bw - (m.large ? 24 : 14);
    waste_x = m.w / 2 + (m.large ? 24 : 14);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu -------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id == kit::kLevel0) deal(false);
    else if (id == kit::kRestart) deal(true);
    else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu, true, 1); }
void menu_back()  { hint_col = -1; update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Golf", nullptr, h, true, "Card Back");
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.deal(shell().random_seed ? shell().random_seed() : lv_tick_get()); }
    hint_col = -1;
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
    if (clock_.tick(now, !over() && S->g.log_n > 0, S->seconds) && !cards::celebrating()) { ticking = true; update_status(); ticking = false; }
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
    if (st->g.won()) snprintf(buf, cap, "All cleared");
    else             snprintf(buf, cap, "%d cards left, %d in the stock", st->g.left(), st->g.stock_n);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a run 5-6-7 fanned onto the waste
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 50 / 100, h = size * 70 / 100;
    for (int i = 0; i < 3; ++i)
        cards::draw_face(layer, a.x1 + i * (size - w) / 2, a.y1 + (2 - i) * (size - h) / 2, w, h,
                         cards::make(5 + i, i == 1 ? cards::Hearts : cards::Clubs));
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
extern const GameOps golf_ops;
const GameOps golf_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
