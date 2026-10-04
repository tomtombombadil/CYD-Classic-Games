// MasterCYD: registry entry, save file and screen. Rules in mastercyd_core.*.
//
// Screen, top to bottom: top bar (clock, guess count, ☰); the code (hidden
// until the game ends) and the 10 guess rows with their answers; the row
// being entered with a Check key; the six colors. Tap a color to put it in
// the first empty slot, tap a filled slot to take it out, tap Check.
// Answers: a filled dot = right color in the right place, a ring = right
// color in the wrong place (shapes, not colors, so they read on any theme).
// The rows, the entry row and the colors are each one custom-drawn object.
//
// Sounds: a checked guess, a color that can't go in, solving, running out.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "mastercyd_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace mastercyd;
using namespace ui;

constexpr const char* kId = "mastercyd";
const char* const kLevels[3] = {"Easy", "Normal", "Hard"};

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   rows_obj = nullptr;
lv_obj_t*   entry_obj = nullptr;
lv_obj_t*   colors_obj = nullptr;
lv_obj_t*   check_k = nullptr;
lv_obj_t*   again_k = nullptr;
int         row_h = 0, slot = 0, swatch = 0;
uint32_t    last_save_ms = 0;
bool        dirty = false;

void build();
void open_menu();

// ---- Save ---------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = S->recorded;
    for (int k = 0; k < 4; ++k) buf[n + 1 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, sizeof buf);
    dirty = false;
}

bool load(State& st)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    if (n != kSaveBytes || !st.g.deserialize(buf, n)) return false;
    st.recorded = buf[Game::kSaveBytes] ? 1 : 0;
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 1 + k]) << (8 * k);
    return true;
}

// ---- Colors and shapes ---------------------------------------------------------------------
// Six peg colors from the theme: red, yellow, green, blue, white, black
lv_color_t peg_color(int c)
{
    const Palette& P = pal();
    switch (c) {
        case 0:  return P.piece_a;
        case 1:  return P.piece_b;
        case 2:  return P.win;
        case 3:  return P.frame;
        case 4:  return P.stone_light;
        default: return P.stone_dark;
    }
}

void circle(lv_layer_t* layer, int cx, int cy, int d, lv_color_t c)
{
    kit::fill_rect(layer, cx - d / 2, cy - d / 2, cx - d / 2 + d - 1, cy - d / 2 + d - 1, c, d / 2);
}

void ring(lv_layer_t* layer, int cx, int cy, int d, int w, lv_color_t c, lv_color_t inside)
{
    circle(layer, cx, cy, d, c);
    circle(layer, cx, cy, d - 2 * w, inside);
}

// A peg: color with a thin rim (white and black pegs stay visible on any
// background); an empty hole is a small dot
void peg(lv_layer_t* layer, int cx, int cy, int d, uint8_t c, lv_color_t bg)
{
    const Palette& P = pal();
    if (c == kEmpty) { circle(layer, cx, cy, d / 3 > 4 ? d / 3 : 4, P.key_border); return; }
    circle(layer, cx, cy, d, P.key_border);
    circle(layer, cx, cy, d - 2, peg_color(c));
    (void)bg;
}

// Answer marks in one line: filled dots (exact) first, then rings (near),
// then small dots for the rest
void answer(lv_layer_t* layer, int x, int y, int w, int h, const Feedback& f, int n, lv_color_t bg)
{
    const Palette& P = pal();
    const int cw = w / n;
    int d = (cw < h ? cw : h) - 3;
    if (d < 5) d = 5;
    for (int k = 0; k < n; ++k) {
        const int cx = x + k * cw + cw / 2, cy = y + h / 2;
        if (k < f.exact)               circle(layer, cx, cy, d, P.ink);
        else if (k < f.exact + f.near) ring(layer, cx, cy, d, d >= 10 ? 2 : 1, P.ink, bg);
        else                           circle(layer, cx, cy, 3, P.key_border);
    }
}

// ---- Status ----------------------------------------------------------------------------------
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
    if (g.solved())         snprintf(s, sizeof s, "Solved in %d!", g.rows);
    else if (g.over())      snprintf(s, sizeof s, "Out of guesses");
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Guess %d of %d", g.rows + 1, kRows);
    kit::top_bar_status(bar, s);
    set_dim(check_k, !g.full());
    set_checked(check_k, g.full());
    lv_obj_set_hidden(check_k, g.over());
    lv_obj_set_hidden(again_k, !g.over());
    if (!ticking) lv_obj_invalidate(rows_obj);
    if (!ticking) lv_obj_invalidate(entry_obj);
    if (!ticking) lv_obj_invalidate(colors_obj);
}

void record(bool solved, bool lost)
{
    puzzle::Record r;
    r.level = S->g.level;
    r.solved = solved;
    r.lost = lost;
    r.moves = S->g.rows;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!S->g.over() && !S->recorded && S->g.rows > 0) record(false, false);   // gave up
    *S = State{};
    Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.start(level, rng);
    save();
    build();
}

// ---- Drawing ---------------------------------------------------------------------------------
void rows_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(rows_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int w = lv_area_get_width(&a), n = g.pegs();
    const int num_w = row_h + 2, ans_w = n * row_h * 4 / 5 + 4;
    const int pw = (w - num_w - ans_w) / kMaxPegs;          // same peg spacing on every level
    const int d = (pw < row_h ? pw : row_h) - 4;
    const lv_font_t* f = row_h >= 24 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    // Row 0: the code. Hidden as "?" until the game ends.
    for (int r = 0; r <= kRows; ++r) {
        const int y = a.y1 + r * row_h;
        const lv_color_t bg = r == 0 ? P.cell : (r % 2 ? P.screen : P.cell);
        kit::fill_rect(layer, a.x1, y, a.x2, y + row_h - 1, bg, 0);
        if (r == 0) {
            for (int i = 0; i < n; ++i) {
                const int cx = a.x1 + num_w + i * pw + pw / 2 + 6, cy = y + row_h / 2;
                if (g.over()) peg(layer, cx, cy, d, g.secret[i], bg);
                else {
                    circle(layer, cx, cy, d, P.key_border);
                    kit::text(layer, "?", f, P.ink, cx - d / 2, cy - d / 2, d, d);
                }
            }
            kit::fill_rect(layer, a.x1, y + row_h - 2, a.x2, y + row_h - 1, P.line_thick, 0);
            continue;
        }
        const int k = r - 1;
        char num[12];
        snprintf(num, sizeof num, "%d", k + 1);
        kit::text(layer, num, f, k < g.rows ? P.muted : P.key_border, a.x1, y, num_w, row_h);
        for (int i = 0; i < n; ++i) {
            const int cx = a.x1 + num_w + i * pw + pw / 2 + 6, cy = y + row_h / 2;
            peg(layer, cx, cy, d, k < g.rows ? g.guess[k][i] : kEmpty, bg);
        }
        if (k < g.rows) answer(layer, a.x2 - ans_w, y + 1, ans_w - 4, row_h - 2, g.fb[k], n, bg);
    }
}

void entry_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(entry_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.cell, 6);
    const int n = g.pegs();
    for (int i = 0; i < n; ++i) {
        const int cx = a.x1 + i * slot + slot / 2, cy = (a.y1 + a.y2) / 2;
        if (g.cur[i] == kEmpty) ring(layer, cx, cy, slot - 6, 2, P.key_border, P.cell);
        else                    peg(layer, cx, cy, slot - 6, g.cur[i], P.cell);
    }
}

void colors_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(colors_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    for (int c = 0; c < kColors; ++c) {
        const int x = a.x1 + c * swatch;
        bool used = false;
        if (!g.repeats()) for (int i = 0; i < g.pegs(); ++i) used |= g.cur[i] == c;
        kit::fill_rect(layer, x + 2, a.y1, x + swatch - 3, a.y2, P.key, 6);
        kit::fill_rect(layer, x + 2, a.y2 - 1, x + swatch - 3, a.y2, P.key_border, 0);
        const int d = (swatch < lv_area_get_height(&a) ? swatch : lv_area_get_height(&a)) - 10;
        if (used || g.over()) ring(layer, x + swatch / 2, (a.y1 + a.y2) / 2, d, 2, P.key_border, P.key);
        else                  peg(layer, x + swatch / 2, (a.y1 + a.y2) / 2, d, uint8_t(c), P.key);
    }
}

// ---- Taps -------------------------------------------------------------------------------------
int index_at(lv_obj_t* o, int width)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    return (p.x - a.x1) / width;
}

void colors_cb(lv_event_t*)
{
    if (!S || S->g.over() || overlay_open()) return;
    const int c = index_at(colors_obj, swatch);
    if (c < 0 || c >= kColors) return;
    if (!S->g.place(uint8_t(c))) { sound(Sound::Error); return; }
    dirty = true;
    update_status();
}

void entry_cb(lv_event_t*)
{
    if (!S || S->g.over() || overlay_open()) return;
    const int i = index_at(entry_obj, slot);
    if (i < 0 || i >= S->g.pegs()) return;
    S->g.clear(i);
    dirty = true;
    update_status();
}

void check_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (!g.submit()) { sound(Sound::Error); return; }
    if (g.over() && !S->recorded) {
        S->recorded = 1;
        record(g.solved(), !g.solved());
        if (g.solved()) { sound(Sound::Win); kit::flash(); }
        else            sound(Sound::Lose);
    } else {
        sound(Sound::Place);
    }
    save();
    update_status();
}

void again_cb(lv_event_t*) { start_new(S->g.level); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 5;
    // Colors along the bottom
    const int cy = m.h - pad - kh;
    swatch = (m.w - 2 * pad) / kColors;
    colors_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(colors_obj);
    lv_obj_set_size(colors_obj, swatch * kColors, kh);
    lv_obj_set_pos(colors_obj, (m.w - swatch * kColors) / 2, cy);
    lv_obj_set_clickable(colors_obj, true);
    lv_obj_add_event_cb(colors_obj, colors_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(colors_obj, colors_cb, LV_EVENT_PRESSED, nullptr);
    // Entry row + Check
    const int ey = cy - gap - kh;
    const int check_w = m.large ? 100 : 72;
    const int ew = m.w - 2 * pad - check_w - gap;
    slot = ew / kMaxPegs;
    if (slot > kh + 8) slot = kh + 8;
    entry_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(entry_obj);
    lv_obj_set_size(entry_obj, slot * S->g.pegs(), kh);
    lv_obj_set_pos(entry_obj, pad, ey);
    lv_obj_set_clickable(entry_obj, true);
    lv_obj_add_event_cb(entry_obj, entry_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(entry_obj, entry_cb, LV_EVENT_PRESSED, nullptr);
    check_k = make_key(scr, check_w, kh, check_cb, 0);
    key_label(check_k, "Check", menu_font());
    lv_obj_set_pos(check_k, m.w - pad - check_w, ey);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ey);
    // The code and the 10 rows fill the rest
    const int top = bar.h + 2, bottom = ey - gap;
    row_h = (bottom - top) / (kRows + 1);
    rows_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(rows_obj);
    lv_obj_set_size(rows_obj, m.w - 2 * pad, row_h * (kRows + 1));
    lv_obj_set_pos(rows_obj, pad, top);
    lv_obj_add_event_cb(rows_obj, rows_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu --------------------------------------------------------------------------------------
void menu_pick(int id) { if (id <= kit::kLevel2) start_new(id); }
void menu_stats()      { kit::stats_solo(kId, kLevels, open_menu, true); }
void menu_back()       { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("MasterCYD", kLevels, h, false);
}

// ---- Registry entry ------------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        *S = State{};
        Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
        S->g.start(1, rng);
    }
    dirty = false;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    rows_obj = entry_obj = colors_obj = check_k = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.over(), S->seconds)) { ticking = true; update_status(); ticking = false; }
    if (dirty || kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    const Game& g = st->g;
    const char* lv = kLevels[g.level];
    if (g.solved())    snprintf(buf, cap, "%s, solved in %d", lv, g.rows);
    else if (g.over()) snprintf(buf, cap, "%s, out of guesses", lv);
    else               snprintf(buf, cap, "%s, guess %d of %d", lv, g.rows + 1, kRows);
    return true;
}

void save_now() { save(); }

// Icon: two rows of three big pegs, each with its marks (a dot and a ring)
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), d = size * 27 / 100, step = d + (size >= 60 ? 2 : 1);
    static const uint8_t rows[2][3] = {{0, 1, 3}, {3, 0, 2}};
    for (int r = 0; r < 2; ++r) {
        const int y = a.y1 + size / 4 + r * (size / 2);
        for (int i = 0; i < 3; ++i) peg(layer, a.x1 + d / 2 + i * step, y, d, rows[r][i], P.screen);
        const int md = d * 45 / 100 > 5 ? d * 45 / 100 : 5, mx = a.x1 + 3 * step + md / 2 + 1;
        circle(layer, mx, y - md / 2 - 1, md, P.ink);
        if (r == 0) ring(layer, mx, y + md / 2 + 1, md, md >= 10 ? 2 : 1, P.ink, P.key);
        else        circle(layer, mx, y + md / 2 + 1, md, P.ink);
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
extern const GameOps mastercyd_ops;
const GameOps mastercyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
