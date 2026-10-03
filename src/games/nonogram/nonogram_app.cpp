// Nonograms: registry entry, save file and screen. Rules and generator in
// nonogram_core.*.
//
// Screen: top bar (clock, size, ☰); the clues (column clues above the grid,
// row clues to its left) and the grid, one custom-drawn object; Fill | Mark
// keys (Play Again once solved). Tap a cell to fill it, or in Mark mode to
// put an X where you know there is no fill; tap again to clear. A clue
// turns grey once its line matches it. The row and column of the last
// tapped cell are tinted so their clues are easy to find on a small grid.
//
// Sounds: solving only. Fill and Mark taps are silent (there are many).
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "nonogram_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace nonogram;
using namespace ui;

constexpr const char* kId = "nonogram";
const char* const kLevels[3] = {"5x5", "8x8", "10x10"};

struct State {
    Game     g;
    uint8_t  mark_mode = 0;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 6;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board_obj = nullptr;
lv_obj_t*   fill_k = nullptr;
lv_obj_t*   mark_k = nullptr;
lv_obj_t*   again_k = nullptr;
int         cell = 0, gx = 0, gy = 0;          // grid origin inside board_obj
int         slot_w = 0, slot_h = 0;            // one clue number
const lv_font_t* clue_font = nullptr;
int         last_r = -1, last_c = -1;
uint32_t    last_save_ms = 0;
bool        dirty = false;

void build();
void open_menu();

// ---- Save -----------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = S->g.serialize(buf, kSaveBytes);
    buf[n] = S->mark_mode;
    buf[n + 1] = S->recorded;
    for (int k = 0; k < 4; ++k) buf[n + 2 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
    dirty = false;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) {
        st.mark_mode = buf[Game::kSaveBytes] ? 1 : 0;
        st.recorded = buf[Game::kSaveBytes + 1] ? 1 : 0;
        st.seconds = 0;
        for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 2 + k]) << (8 * k);
    }
    delete[] buf;
    return ok;
}

// ---- Flow ---------------------------------------------------------------------------------------
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
    if (g.solved())         snprintf(s, sizeof s, "Solved!");
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "%s", kLevels[g.level]);
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !g.solved());
    lv_obj_set_hidden(fill_k, g.solved());
    lv_obj_set_hidden(mark_k, g.solved());
    set_checked(fill_k, !S->mark_mode);
    set_checked(mark_k, S->mark_mode);
    if (!ticking) lv_obj_invalidate(board_obj);
}

void record(bool solved)
{
    puzzle::Record r;
    r.level = S->g.level;
    r.solved = solved;
    r.moves = S->g.taps;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!S->g.solved() && !S->recorded && S->g.taps > 0) record(false);   // gave up
    const uint8_t mode = S->mark_mode;
    *S = State{};
    S->mark_mode = mode;
    Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.start(level, rng);
    last_r = last_c = -1;
    save();
    build();
}

// ---- Drawing ------------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Palette& P = pal();
    const Game& g = S->g;
    const int n = g.n;
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int x0 = a.x1 + gx, y0 = a.y1 + gy, size = n * cell;
    // Tint the last tapped row and column, clues included
    if (last_r >= 0 && !g.solved()) {
        kit::fill_rect(layer, a.x1, y0 + last_r * cell, x0 + size - 1, y0 + (last_r + 1) * cell - 1, P.peer, 0);
        kit::fill_rect(layer, x0 + last_c * cell, a.y1, x0 + (last_c + 1) * cell - 1, y0 + size - 1, P.peer, 0);
    }
    char t[4];
    // Column clues, bottom-aligned above each column
    for (int c = 0; c < n; ++c) {
        const Clue& k = g.cols[c];
        const lv_color_t col = g.col_done(c) ? P.key_dim_text : P.ink;
        const int cnt = k.n ? k.n : 1;
        for (int i = 0; i < cnt; ++i) {
            snprintf(t, sizeof t, "%d", k.n ? k.run[i] : 0);
            kit::text(layer, t, clue_font, col, x0 + c * cell, y0 - (cnt - i) * slot_h - 2, cell, slot_h);
        }
    }
    // Row clues, right-aligned left of each row
    for (int r = 0; r < n; ++r) {
        const Clue& k = g.rows[r];
        const lv_color_t col = g.row_done(r) ? P.key_dim_text : P.ink;
        const int cnt = k.n ? k.n : 1;
        for (int i = 0; i < cnt; ++i) {
            snprintf(t, sizeof t, "%d", k.n ? k.run[i] : 0);
            kit::text(layer, t, clue_font, col, x0 - (cnt - i) * slot_w - 3, y0 + r * cell, slot_w, cell);
        }
    }
    // Grid
    kit::fill_rect(layer, x0 - 1, y0 - 1, x0 + size, y0 + size, P.line_thick, 0);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c) {
            const int x = x0 + c * cell, y = y0 + r * cell;
            const int l = (c % 5 == 0) ? 1 : 0, tp = (r % 5 == 0) ? 1 : 0;   // thicker every 5
            const uint8_t v = g.cell[r * n + c];
            lv_color_t bg = (r == last_r || c == last_c) && !g.solved() ? P.peer : P.cell;
            if (v == Filled) bg = P.ink;
            kit::fill_rect(layer, x + l, y + tp, x + cell - 2, y + cell - 2, bg, 0);
            if (v == Marked) {
                const int m = cell / 4;
                kit::line(layer, x + m, y + m, x + cell - 1 - m, y + cell - 1 - m, cell >= 24 ? 2 : 1, P.muted);
                kit::line(layer, x + m, y + cell - 1 - m, x + cell - 1 - m, y + m, cell >= 24 ? 2 : 1, P.muted);
            }
        }
}

void tap_cb(lv_event_t*)
{
    if (!S || overlay_open() || S->g.solved()) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    const int x = p.x - a.x1 - gx, y = p.y - a.y1 - gy;
    if (x < 0 || y < 0) return;
    const int c = x / cell, r = y / cell;
    Game& g = S->g;
    if (r >= g.n || c >= g.n) return;
    g.tap(r, c, S->mark_mode ? Marked : Filled);
    last_r = r; last_c = c;
    dirty = true;
    if (g.solved() && !S->recorded) {
        S->recorded = 1;
        record(true);
        sound(Sound::Win);
        kit::flash();
        save();
    }
    update_status();
}

void mode_cb(lv_event_t* e)
{
    if (!S) return;
    S->mark_mode = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    dirty = true;
    update_status();
}

void again_cb(lv_event_t*) { start_new(S->g.level); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 6;
    const int ky = m.h - pad - kh, half = (m.w - 2 * pad - gap) / 2;
    fill_k = make_key(scr, half, kh, mode_cb, 0);
    key_label(fill_k, "Fill", menu_font());
    lv_obj_set_pos(fill_k, pad, ky);
    mark_k = make_key(scr, half, kh, mode_cb, 1);
    key_label(mark_k, "Mark " LV_SYMBOL_CLOSE, menu_font());
    lv_obj_set_pos(mark_k, m.w - pad - half, ky);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);

    // Clue room: the most clues a line of this size can have
    const int n = S->g.n, most = (n + 1) / 2;
    clue_font = n >= 10 ? (m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12)
                        : (m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14);
    slot_w = text_width("0", clue_font) + 3;           // "10" only ever stands alone
    slot_h = lv_font_get_line_height(clue_font) - 3;
    const int top = bar.h + gap / 2, bottom = ky - gap;
    const int bw = m.w - 2 * pad, bh = bottom - top;
    const int clue_w = most * slot_w + 4, clue_h = most * slot_h + 4;
    const int cw = (bw - clue_w) / n, ch = (bh - clue_h) / n;
    cell = cw < ch ? cw : ch;
    const int used_w = clue_w + n * cell, used_h = clue_h + n * cell;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, used_w, used_h);
    lv_obj_set_pos(board_obj, (m.w - used_w) / 2, top + (bh - used_h) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, tap_cb, LV_EVENT_PRESSED, nullptr);
    gx = clue_w;
    gy = clue_h;
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu ------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id <= kit::kLevel2) start_new(id);
    else if (id == kit::kRestart) {
        for (uint8_t& c : S->g.cell) c = Unknown;
        last_r = last_c = -1;
        dirty = true;
        update_status();
    }
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu); }
void menu_back()  { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Nonograms", kLevels, h);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        *S = State{};
        Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
        S->g.start(0, rng);
    }
    last_r = last_c = -1;
    dirty = false;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board_obj = fill_k = mark_k = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.solved(), S->seconds)) { ticking = true; update_status(); ticking = false; }
    if (dirty || now - last_save_ms > 30000) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

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
    int done = 0;
    for (int r = 0; r < g.n; ++r) done += g.row_done(r);
    if (g.solved()) snprintf(buf, cap, "%s, solved", kLevels[g.level]);
    else            snprintf(buf, cap, "%s, %d of %d rows right", kLevels[g.level], done, g.n);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a little 5x5 picture with a few clue marks
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), c = size / 6, x0 = a.x1 + size - 5 * c, y0 = a.y1 + size - 5 * c;
    static const uint8_t pic[5] = {0b00100, 0b01110, 0b11111, 0b01110, 0b01010};
    kit::fill_rect(layer, x0 - 1, y0 - 1, x0 + 5 * c, y0 + 5 * c, P.line_thick, 0);
    for (int r = 0; r < 5; ++r)
        for (int k = 0; k < 5; ++k)
            kit::fill_rect(layer, x0 + k * c, y0 + r * c, x0 + (k + 1) * c - 2, y0 + (r + 1) * c - 2,
                           pic[r] >> k & 1 ? P.ink : P.cell, 0);
    for (int i = 0; i < 5; ++i) {
        kit::fill_rect(layer, a.x1 + c / 3, y0 + i * c + c / 3, a.x1 + c * 2 / 3, y0 + i * c + c * 2 / 3, P.muted, 0);
        kit::fill_rect(layer, x0 + i * c + c / 3, a.y1 + c / 3, x0 + i * c + c * 2 / 3, a.y1 + c * 2 / 3, P.muted, 0);
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
extern const GameOps nonogram_ops;
const GameOps nonogram_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
