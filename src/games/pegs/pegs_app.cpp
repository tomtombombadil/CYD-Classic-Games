// Peg Solitaire: registry entry, save file and screen. Rules in pegs_core.*.
//
// Screen: top bar (clock, pegs left, ☰), the board, and Undo (with Play
// Again beside it once no jump is left). Tap a peg (the holes it can jump
// to show as dots), then tap the hole. Undo goes back any number of jumps,
// also after getting stuck. A game is recorded when it's solved, or when
// you start another (as Lost if it was stuck, else Gave Up).
//
// Sounds: a jump, a peg with no jump, getting stuck, solving.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "pegs_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace pegs;
using namespace ui;

constexpr const char* kId = "pegs";
const char* const kLevels[3] = {"Triangle", "English", "European"};

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board_obj = nullptr;
lv_obj_t*   undo_k = nullptr;
lv_obj_t*   again_k = nullptr;
int         sel = -1;
int         cell = 0, ox = 0, oy = 0;   // hole spacing and grid origin inside board_obj
uint32_t    last_save_ms = 0;

void build();
void open_menu();

// ---- Save ------------------------------------------------------------------------------
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
    bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) {
        st.recorded = buf[Game::kSaveBytes] ? 1 : 0;
        st.seconds = 0;
        for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 1 + k]) << (8 * k);
    }
    delete[] buf;
    return ok;
}

// ---- Geometry: hole centres -----------------------------------------------------------------
bool hole_xy(int sq, int* x, int* y)
{
    if (!S->g.hole(sq)) return false;
    const int r = sq / kN, c = sq % kN;
    if (S->g.level == Triangle) {
        *x = ox + c * cell - r * cell / 2;
        *y = oy + r * cell * 87 / 100;
    } else {
        *x = ox + c * cell;
        *y = oy + r * cell;
    }
    return true;
}

// ---- Flow ----------------------------------------------------------------------------------------
bool over() { return S->g.solved() || S->g.stuck(); }

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
    else if (g.stuck())     snprintf(s, sizeof s, "No jumps: %d left", g.peg_count());
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Pegs left %d", g.peg_count());
    kit::top_bar_status(bar, s);
    const Metrics& m = metrics();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    const bool again = over();
    lv_obj_set_hidden(again_k, !again);
    lv_obj_set_width(undo_k, again ? (m.w - 2 * pad - gap) / 2 : m.w - 2 * pad);
    set_dim(undo_k, g.moves == 0 || g.solved());
    if (!ticking) lv_obj_invalidate(board_obj);
}

void record(bool solved, bool lost)
{
    puzzle::Record r;
    r.level = S->g.level;
    r.solved = solved;
    r.lost = lost;
    r.moves = S->g.moves;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!S->recorded && S->g.moves > 0) record(false, S->g.stuck());   // stuck = Lost, else Gave Up
    *S = State{};
    S->g.start(level);
    sel = -1;
    save();
    build();
}

void after_change()
{
    sel = -1;
    Game& g = S->g;
    if (g.solved() && !S->recorded) {
        S->recorded = 1;
        record(true, false);
        sound(Sound::Win);
        kit::flash();
    } else if (g.stuck()) {
        sound(Sound::Lose);
    } else {
        sound(Sound::Move);
    }
    save();
    update_status();
}

// ---- Board ------------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Palette& P = pal();
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int peg_r = cell * 36 / 100, hole_r = cell / 7 > 3 ? cell / 7 : 3;
    // The wooden board: a rounded panel behind each hole
    for (int sq = 0; sq < kSquares; ++sq) {
        int x, y;
        if (!hole_xy(sq, &x, &y)) continue;
        const int h = cell / 2 + 1;
        kit::fill_rect(layer, a.x1 + x - h, a.y1 + y - h, a.x1 + x + h, a.y1 + y + h, P.sq_light, cell / 3);
    }
    uint64_t targets = 0;
    if (sel >= 0)
        for (int t = 0; t < kSquares; ++t) if (g.find(sel, t)) targets |= uint64_t(1) << t;
    for (int sq = 0; sq < kSquares; ++sq) {
        int x, y;
        if (!hole_xy(sq, &x, &y)) continue;
        x += a.x1; y += a.y1;
        if (g.peg(sq)) {
            if (sq == sel) kit::fill_circle(layer, x, y, peg_r + (cell >= 40 ? 4 : 3), P.selected);
            kit::fill_circle(layer, x, y, peg_r, P.frame);
            kit::fill_circle(layer, x - peg_r / 3, y - peg_r / 3, peg_r / 4 > 1 ? peg_r / 4 : 2,
                             lv_color_mix(P.stone_light, P.frame, 150));
        } else if (targets >> sq & 1) {
            kit::fill_circle(layer, x, y, peg_r * 2 / 3, P.target);
        } else {
            kit::fill_circle(layer, x, y, hole_r, P.sq_dark);
        }
    }
}

int hole_at(lv_event_t*)
{
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    int best = -1, best_d = cell * cell * 3 / 8;        // within ~0.6 of a hole spacing
    for (int sq = 0; sq < kSquares; ++sq) {
        int x, y;
        if (!hole_xy(sq, &x, &y)) continue;
        const int dx = p.x - (a.x1 + x), dy = p.y - (a.y1 + y), d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = sq; }
    }
    return best;
}

void tap_cb(lv_event_t* e)
{
    if (!S || overlay_open() || S->g.solved()) return;
    const int sq = hole_at(e);
    if (sq < 0) return;
    Game& g = S->g;
    if (sel >= 0 && g.find(sel, sq)) {
        g.play(sel, sq);
        after_change();
        return;
    }
    if (g.peg(sq)) {
        if (g.can_move_from(sq)) sel = sq == sel ? -1 : sq;
        else { sel = -1; sound(Sound::Error); }
    } else {
        sel = -1;
    }
    lv_obj_invalidate(board_obj);
}

void undo_cb(lv_event_t*)
{
    if (!S || S->g.solved() || !S->g.undo()) return;
    sel = -1;
    sound(Sound::Move);
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
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 6;
    const int ky = m.h - pad - kh;
    undo_k = make_key(scr, m.w - 2 * pad, kh, undo_cb, 0);
    key_label(undo_k, "Undo", menu_font());
    lv_obj_set_pos(undo_k, pad, ky);
    again_k = make_key(scr, (m.w - 2 * pad - gap) / 2, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, m.w - pad - (m.w - 2 * pad - gap) / 2, ky);

    const int top = bar.h + gap, bottom = ky - gap;
    const int bw = m.w - 2 * pad, bh = bottom - top;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, bw, bh);
    lv_obj_set_pos(board_obj, pad, top);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, tap_cb, LV_EVENT_PRESSED, nullptr);
    if (S->g.level == Triangle) {
        // 5 holes across the bottom row, rows 0.87 apart
        const int cw = bw / 5, ch = bh * 100 / (5 * 87);
        cell = cw < ch ? cw : ch;
        ox = bw / 2;                                        // row r, col c: x = ox + (c - r/2) cell
        oy = (bh - 4 * cell * 87 / 100) / 2;
    } else {
        cell = (bw < bh ? bw : bh) / 7;
        ox = (bw - 6 * cell) / 2;
        oy = (bh - 6 * cell) / 2;
    }
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu --------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id <= kit::kLevel2) start_new(id);
    else if (id == kit::kRestart) {
        while (S->g.undo()) {}
        sel = -1;
        save();
        update_status();
    }
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu, true); }
void menu_back()  { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Peg Solitaire", kLevels, h);
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(English); }
    sel = -1;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board_obj = undo_k = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !over(), S->seconds)) { ticking = true; update_status(); ticking = false; }
    if (now - last_save_ms > 30000) { last_save_ms = now; save(); }
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
    if (g.solved())     snprintf(buf, cap, "%s, solved", level_name(g.level));
    else if (g.stuck()) snprintf(buf, cap, "%s, stuck with %d", level_name(g.level), g.peg_count());
    else                snprintf(buf, cap, "%s, %d pegs left", level_name(g.level), g.peg_count());
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a small plus of pegs with one empty hole in the middle
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), c = size / 3, r = c * 36 / 100;
    for (int i = 0; i < 9; ++i) {
        if (i == 0 || i == 2 || i == 6 || i == 8) continue;
        const int x = a.x1 + (i % 3) * c + c / 2, y = a.y1 + (i / 3) * c + c / 2;
        kit::fill_rect(layer, x - c / 2, y - c / 2, x + c / 2, y + c / 2, P.sq_light, c / 3);
        if (i == 4) kit::fill_circle(layer, x, y, r / 2, P.sq_dark);
        else {
            kit::fill_circle(layer, x, y, r, P.frame);
            kit::fill_circle(layer, x - r / 3, y - r / 3, r / 4 > 1 ? r / 4 : 1, lv_color_mix(P.stone_light, P.frame, 150));
        }
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
extern const GameOps pegs_ops;
const GameOps pegs_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
