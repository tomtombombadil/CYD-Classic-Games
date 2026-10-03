// Minesweeper: registry entry, save file and screen. Rules and the
// no-guess board generator live in minesweeper_core.*.
//
// Screen: top bar (clock, mines left, ☰), the board, and a Dig | Flag mode
// switch (Play Again once the game is over). Tap a cell to open it, or to
// flag it in Flag mode. Long-press any hidden cell to flag or unflag it in
// either mode (Tom's choice; the Flag mode does the same without it). Tap
// an open number whose flags are all set to open the cells around it.
//
// Sounds: opening cells, a mine, a cleared board. Flags are silent.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "minesweeper_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace ui;
using mines::Board;
using mines::Cell;
using mines::Status;

constexpr const char* kId = "minesweeper";
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};

struct Game {
    Board    b;
    uint8_t  flag_mode = 0;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
Game* G = nullptr;

constexpr size_t kSaveBytes = Board::kSaveBytes + 6;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board_obj = nullptr;
lv_obj_t*   dig_k = nullptr;
lv_obj_t*   flag_k = nullptr;
lv_obj_t*   again_k = nullptr;
int         cell = 0;
uint32_t    last_save_ms = 0;
bool        dirty = false;

void build();
void open_menu();

mines::Rng rng_now()
{
    return mines::Rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
}

// ---- Save --------------------------------------------------------------------------
size_t pack(const Game& g, uint8_t* buf)
{
    size_t n = g.b.serialize(buf, Board::kSaveBytes);
    buf[n++] = g.flag_mode;
    buf[n++] = g.recorded;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(g.seconds >> (8 * k));
    return n;
}

bool unpack(Game& g, const uint8_t* buf, size_t len)
{
    if (len != kSaveBytes) return false;
    Game t;
    if (!t.b.deserialize(buf, Board::kSaveBytes)) return false;
    const uint8_t* q = buf + Board::kSaveBytes;
    t.flag_mode = q[0] ? 1 : 0;
    t.recorded = q[1] ? 1 : 0;
    for (int k = 0; k < 4; ++k) t.seconds |= uint32_t(q[2 + k]) << (8 * k);
    g = t;
    return true;
}

bool load(Game& g)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const Shell& H = shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = unpack(g, buf, n);
    delete[] buf;
    return ok;
}

void save()
{
    if (!G || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    shell().save_game(kId, buf, pack(*G, buf));
    delete[] buf;
    dirty = false;
}

// ---- Game flow -----------------------------------------------------------------------
bool over() { return G->b.status != Status::Playing; }

void update_status()
{
    if (!bar.center || !G) return;
    char t[16], s[32];
    twoplayer::format_time(t, sizeof t, G->seconds);
    lv_label_set_text(bar.left, t);
    if (G->b.status == Status::Won)       snprintf(s, sizeof s, "Cleared!");
    else if (G->b.status == Status::Lost) snprintf(s, sizeof s, "Boom!");
    else if (clock_.paused)               snprintf(s, sizeof s, "Paused");
    else                                  snprintf(s, sizeof s, "Mines %d", G->b.mines_left());
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !over());
    lv_obj_set_hidden(dig_k, over());
    lv_obj_set_hidden(flag_k, over());
    set_checked(dig_k, !G->flag_mode);
    set_checked(flag_k, G->flag_mode);
}

void record(bool won, bool lost)
{
    puzzle::Record r;
    r.level = G->b.level;
    r.solved = won;
    r.lost = lost;
    r.moves = G->b.moves;
    r.seconds = G->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!over() && !G->recorded && G->b.moves > 0) record(false, false);   // gave up
    const uint8_t mode = G->flag_mode;
    *G = Game{};
    G->flag_mode = mode;
    G->b.start(level);
    dirty = true;
    build();
}

void changed()
{
    lv_obj_invalidate(board_obj);
    dirty = true;
    if (over() && !G->recorded) {
        G->recorded = 1;
        const bool won = G->b.status == Status::Won;
        record(won, !won);
        if (won) { sound(Sound::Win); kit::flash(); }
        else       sound(Sound::Lose);
        save();
    }
    update_status();
}

void tap(int i)
{
    if (!G || over() || overlay_open()) return;
    const Cell c = G->b.cell[i];
    if (G->flag_mode && c != Cell::Open) {
        G->b.toggle_flag(i);
        changed();
        return;
    }
    mines::Rng rng = rng_now();
    if (G->b.open(i, rng)) {
        if (!over()) sound(Sound::Place);
        changed();
    }
}

void flag(int i)
{
    if (!G || over() || overlay_open()) return;
    if (G->b.cell[i] == Cell::Open) return;
    G->b.toggle_flag(i);
    changed();
}

// ---- Board ------------------------------------------------------------------------------
lv_color_t number_color(int n)
{
    const Palette& P = pal();
    switch (n) {
        case 1:  return P.entry;
        case 2:  return P.win;
        case 3:  return P.conflict;
        default: return P.ink;
    }
}

// A flag: pole and a pennant pointing right, drawn as 1-px rows
void draw_flag(lv_layer_t* layer, int x, int y, int c, lv_color_t cloth)
{
    const Palette& P = pal();
    const int pole_x = x + c * 3 / 8, top = y + c / 5, foot = y + c - c / 5;
    const int pw = c / 3 > 2 ? c / 3 : 2;
    kit::fill_rect(layer, pole_x, top, pole_x + (c >= 30 ? 2 : 1), foot, P.ink);
    kit::fill_rect(layer, pole_x - c / 6, foot - (c >= 30 ? 2 : 1), pole_x + c / 6 + 1, foot, P.ink);
    const int ph = c * 2 / 5, half = ph / 2;
    for (int r = 0; r <= ph; ++r) {
        const int d = r < half ? r : ph - r;
        const int len = half ? pw * d / half : pw;
        if (len > 0) kit::fill_rect(layer, pole_x + 1, top + r, pole_x + 1 + len, top + r, cloth);
    }
}

void draw_mine(lv_layer_t* layer, int cx, int cy, int c)
{
    const lv_color_t ink = pal().ink;
    const int r = c / 5 > 3 ? c / 5 : 3, s = r + r / 2 + 1, d = s * 7 / 10, w = c >= 30 ? 3 : 2;
    kit::line(layer, cx - s, cy, cx + s, cy, w, ink);
    kit::line(layer, cx, cy - s, cx, cy + s, w, ink);
    kit::line(layer, cx - d, cy - d, cx + d, cy + d, w, ink);
    kit::line(layer, cx - d, cy + d, cx + d, cy - d, w, ink);
    kit::fill_circle(layer, cx, cy, r, ink);
}

lv_color_t hidden_face()
{
    const Palette& P = pal();
    const int d = int(lv_color_luminance(P.peer)) - int(lv_color_luminance(P.screen));
    if (d >= 60 || d <= -60) return P.peer;
    return lv_color_mix(P.ink, P.peer, 80);      // about a third of the way to the text color
}

// Draws cells of `b` (or the icon's mini board) with its top-left at x0, y0
void draw_board(lv_layer_t* layer, const Board& b, int x0, int y0, int c)
{
    const Palette& P = pal();
    const int gap = c >= 30 ? 2 : 1, rad = c / 6;
    const lv_font_t* f = c >= 30 ? &lv_font_montserrat_28 : c >= 22 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const bool lost = b.status == Status::Lost;
    kit::fill_rect(layer, x0, y0, x0 + b.w * c - 1, y0 + b.h * c - 1, P.line_thin, rad);
    for (int i = 0; i < b.cells(); ++i) {
        const int x = x0 + (i % b.w) * c, y = y0 + (i / b.w) * c;
        const int x1 = x + gap, y1 = y + gap, x2 = x + c - 1 - gap, y2 = y + c - 1 - gap;
        const Cell s = b.cell[i];
        if (s == Cell::Open) {
            kit::fill_rect(layer, x1, y1, x2, y2, i == b.boom ? P.conflict : P.screen, rad);   // open = sunk in
            if (b.mine[i]) {
                draw_mine(layer, x + c / 2, y + c / 2, c);
            } else if (b.near[i]) {
                char t[2] = {char('0' + b.near[i]), 0};
                kit::text(layer, t, f, number_color(b.near[i]), x, y, c, c);
            }
            continue;
        }
        // Hidden or flagged: a raised tile in the row-highlight hue, lifted
        // toward the text color when that hue sits too close to the open
        // cells (Dark, some custom themes) - TN panels lose small steps
        const int e = c >= 30 ? 2 : 1;
        kit::fill_rect(layer, x1, y1, x2, y2, P.key_border, rad);
        kit::fill_rect(layer, x1, y1, x2 - e, y2 - e, hidden_face(), rad);
        if (s == Cell::Flag) {
            draw_flag(layer, x, y, c, P.conflict);
            if (lost && !b.mine[i]) {            // wrong flag
                kit::line(layer, x1 + 2, y1 + 2, x2 - 2, y2 - 2, 2, P.conflict);
                kit::line(layer, x1 + 2, y2 - 2, x2 - 2, y1 + 2, 2, P.conflict);
            }
        } else if (lost && b.mine[i]) {
            draw_mine(layer, x + c / 2, y + c / 2, c);
        }
    }
}

void draw_cb(lv_event_t* e)
{
    if (!G) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    draw_board(lv_event_get_layer(e), G->b, a.x1, a.y1, cell);
}

int cell_at(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev || !G) return -1;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int c = (pt.x - a.x1) / cell, r = (pt.y - a.y1) / cell;
    c = c < 0 ? 0 : c >= G->b.w ? G->b.w - 1 : c;
    r = r < 0 ? 0 : r >= G->b.h ? G->b.h - 1 : r;
    return r * G->b.w + c;
}

void tap_cb(lv_event_t* e)  { const int i = cell_at(e); if (i >= 0) tap(i); }
void long_cb(lv_event_t* e) { const int i = cell_at(e); if (i >= 0) flag(i); }

void mode_cb(lv_event_t* e)
{
    if (!G) return;
    G->flag_mode = static_cast<uint8_t>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    dirty = true;
    update_status();
}

void again_cb(lv_event_t*) { start_new(G->b.level); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 6;
    const int ky = m.h - pad - kh, half = (m.w - 2 * pad - gap) / 2;
    dig_k = make_key(scr, half, kh, mode_cb, 0);
    key_label(dig_k, "Dig", menu_font());
    lv_obj_set_pos(dig_k, pad, ky);
    flag_k = make_key(scr, half, kh, mode_cb, 1);
    key_label(flag_k, "Flag", menu_font());
    lv_obj_set_pos(flag_k, m.w - pad - half, ky);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);

    const Board& b = G->b;
    const int top = bar.h, bottom = ky - pad;
    const int margin = m.large ? 4 : 2;
    const int w = (m.w - 2 * margin) / b.w, h = (bottom - top) / b.h;
    cell = w < h ? w : h;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, b.w * cell, b.h * cell);
    lv_obj_set_pos(board_obj, (m.w - b.w * cell) / 2, top + (bottom - top - b.h * cell) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, tap_cb, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(board_obj, long_cb, LV_EVENT_LONG_PRESSED, nullptr);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu --------------------------------------------------------------------------------
void menu_pick(int id) { if (id <= kit::kLevel2) start_new(id); }
void menu_stats()      { kit::stats_solo(kId, kLevels, open_menu, true); }
void menu_back()       { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Minesweeper", kLevels, h, false);
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { app_go_home(); return; }
    if (!load(*G)) { *G = Game{}; G->b.start(0); }
    dirty = false;
    build();
}

void close()
{
    if (!G) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board_obj = dig_k = flag_k = again_k = nullptr;
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    if (!G) return;
    const bool running = G->b.placed && !over();
    if (clock_.tick(now, running, G->seconds)) update_status();
    if (dirty || (running && now - last_save_ms > 30000)) {
        last_save_ms = now;
        save();
    }
}

void restyle() { if (G) build(); }

bool summary(char* buf, size_t cap)
{
    Game* tmp = nullptr;
    const Game* g = G;
    if (!g) {
        tmp = new (std::nothrow) Game();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        g = tmp;
    }
    const char* lv = kLevels[g->b.level];
    if (g->b.status == Status::Won)       snprintf(buf, cap, "%s, cleared", lv);
    else if (g->b.status == Status::Lost) snprintf(buf, cap, "%s, hit a mine", lv);
    else if (!g->b.placed)                snprintf(buf, cap, "%s, new board", lv);
    else                                  snprintf(buf, cap, "%s, %d mines left", lv, g->b.mines_left());
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a 3x3 corner of a board - numbers, a flag and hidden cells
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int size = lv_area_get_width(&a), c = size / 3;
    Board* ap = new (std::nothrow) Board();   // heap, not static RAM
    if (!ap) return;
    Board& art = *ap;
    {
        art.start(0);
        art.w = 3;
        art.h = 3;
        // . 1 #     # = hidden, F = flag, M = mine (hidden)
        // 1 2 F
        // # F #
        const uint8_t mine_at[9] = {0, 0, 0, 0, 0, 1, 0, 1, 0};
        const uint8_t near_at[9] = {0, 1, 1, 1, 2, 1, 1, 1, 2};
        const Cell state[9] = {Cell::Open, Cell::Open, Cell::Hidden, Cell::Open, Cell::Open,
                               Cell::Flag, Cell::Hidden, Cell::Flag, Cell::Hidden};
        for (int i = 0; i < 9; ++i) { art.mine[i] = mine_at[i]; art.near[i] = near_at[i]; art.cell[i] = state[i]; }
    }
    draw_board(lv_event_get_layer(e), art, a.x1 + (size - 3 * c) / 2, a.y1 + (size - 3 * c) / 2, c);
    delete ap;
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
extern const GameOps minesweeper_ops;
const GameOps minesweeper_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
