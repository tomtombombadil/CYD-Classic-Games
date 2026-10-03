// Sliding Tiles (the 15-Puzzle): registry entry, save file and screen.
// Levels are the board size: 3x3, 4x4 (the classic) and 5x5. Tap a tile in
// the gap's row or column to slide it (and the tiles between) into the gap.
// Tiles already in their home spot are tinted.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "sliding_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using sliding::Puzzle;
using namespace ui;

constexpr const char* kId = "sliding";
const char* const kLevels[3] = {"3x3", "4x4", "5x5"};

struct Game {
    Puzzle   p;
    Puzzle   start;                     // for Restart
    uint8_t  level = 1;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
Game* G = nullptr;

constexpr size_t kSaveBytes = 2 * Puzzle::kSaveBytes + 6;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board_obj = nullptr;
lv_obj_t*   info_l = nullptr;
lv_obj_t*   again_k = nullptr;
int         cell = 0;
int         saved_moves = -1;
uint32_t    last_save_ms = 0;

// ---- Save --------------------------------------------------------------------------
size_t pack(const Game& g, uint8_t* buf)
{
    size_t n = g.p.serialize(buf, Puzzle::kSaveBytes);
    n += g.start.serialize(buf + n, Puzzle::kSaveBytes);
    buf[n++] = g.level;
    buf[n++] = g.recorded;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(g.seconds >> (8 * k));
    return n;
}

bool unpack(Game& g, const uint8_t* buf, size_t len)
{
    if (len != kSaveBytes) return false;
    Game t;
    if (!t.p.deserialize(buf, Puzzle::kSaveBytes) ||
        !t.start.deserialize(buf + Puzzle::kSaveBytes, Puzzle::kSaveBytes)) return false;
    const uint8_t* q = buf + 2 * Puzzle::kSaveBytes;
    if (q[0] > 2) return false;
    t.level = q[0];
    t.recorded = q[1] ? 1 : 0;
    t.seconds = 0;
    for (int k = 0; k < 4; ++k) t.seconds |= uint32_t(q[2 + k]) << (8 * k);
    g = t;
    return true;
}

bool load(Game& g)
{
    uint8_t buf[kSaveBytes];
    const Shell& H = shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return unpack(g, buf, n);
}

void save()
{
    if (!G || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    shell().save_game(kId, buf, pack(*G, buf));
}

void build();
void open_menu();

// ---- Game flow -----------------------------------------------------------------------
void update_status()
{
    if (!bar.center) return;
    char t[16], s[32];
    twoplayer::format_time(t, sizeof t, G->seconds);
    lv_label_set_text(bar.left, t);
    if (G->p.solved())      snprintf(s, sizeof s, "Solved!");
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Moves %u", (unsigned)G->p.moves);
    kit::top_bar_status(bar, s);
    char in[48];
    if (G->p.solved()) snprintf(in, sizeof in, "%s solved in %u moves", kLevels[G->level], (unsigned)G->p.moves);
    else               snprintf(in, sizeof in, "%s: put the tiles in order", kLevels[G->level]);
    lv_label_set_text(info_l, in);
    if (G->p.solved()) lv_obj_set_hidden(again_k, false);
    else               lv_obj_set_hidden(again_k, true);
}

void record(bool solved)
{
    puzzle::Record r;
    r.level = G->level;
    r.solved = solved;
    r.moves = G->p.moves;
    r.seconds = G->seconds;
    kit::record_solo(kId, r, kLevels);
}

// Leaving a puzzle that was really played (moves or 30 s) counts as giving up
void leave_current()
{
    if (!G->p.solved() && !G->recorded && (G->p.moves > 0 || G->seconds >= 30)) record(false);
}

void start_new(int level)
{
    kit::flash_stop();
    leave_current();
    G->level = static_cast<uint8_t>(level);
    G->p.reset(sliding::size_for_level(level));
    sliding::Rng rng(shell().random_seed ? shell().random_seed() : 1);
    G->p.shuffle(rng);
    G->start = G->p;
    G->recorded = 0;
    G->seconds = 0;
    build();
}

void tap(int i)
{
    if (!G || G->p.solved() || overlay_open()) return;
    if (!G->p.tap(i)) return;
    lv_obj_invalidate(board_obj);
    if (G->p.solved() && !G->recorded) {
        G->recorded = 1;
        record(true);
        sound(Sound::Win);
        kit::flash();
        save();
    } else {
        sound(Sound::Move);
    }
    update_status();
}

// ---- Board ------------------------------------------------------------------------------
void draw_tiles(lv_layer_t* layer, const Puzzle& p, int x0, int y0, int c, const lv_font_t* f, bool solved_tint)
{
    const Palette& P = pal();
    const int n = p.n, gap = c >= 40 ? 3 : 2;
    kit::fill_rect(layer, x0, y0, x0 + n * c - 1, y0 + n * c - 1, P.line_thin, c / 6);
    char s[4];
    for (int i = 0; i < p.cells(); ++i) {
        if (!p.tile[i]) continue;
        const int x = x0 + (i % n) * c, y = y0 + (i / n) * c;
        const lv_color_t bg = solved_tint && p.solved() ? P.win : p.in_place(i) ? P.peer : P.key;
        kit::fill_rect(layer, x + gap, y + gap, x + c - 1 - gap, y + c - 1 - gap, bg, c / 6);
        if (!f) continue;
        snprintf(s, sizeof s, "%u", (unsigned)p.tile[i]);
        kit::text(layer, s, f, solved_tint && p.solved() ? contrast_text(P.win) : P.ink,
                  x + gap, y + gap, c - 2 * gap, c - 2 * gap);
    }
}

void draw_cb(lv_event_t* e)
{
    if (!G) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const lv_font_t* f = cell >= 70 ? &lv_font_montserrat_28 : cell >= 44 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    draw_tiles(lv_event_get_layer(e), G->p, a.x1, a.y1, cell, f, true);
}

void press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev || !G) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int n = G->p.n;
    int c = (pt.x - a.x1) / cell, r = (pt.y - a.y1) / cell;
    c = c < 0 ? 0 : c >= n ? n - 1 : c;
    r = r < 0 ? 0 : r >= n ? n - 1 : r;
    tap(r * n + c);
}

void again_cb(lv_event_t*) { start_new(G->level); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int ih = lv_font_get_line_height(&lv_font_montserrat_14);
    info_l = lv_label_create(scr);
    lv_obj_set_style_text_font(info_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_l, pal().muted, 0);
    lv_obj_set_size(info_l, m.w - 2 * pad, ih);
    lv_label_set_long_mode(info_l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_align(info_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(info_l, pad, m.h - pad - ih);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play again", menu_font());
    lv_obj_set_pos(again_k, pad, m.h - pad - ih - pad - kh);
    const int top = bar.h, bottom = m.h - pad - ih - pad - kh - pad;
    const int n = G->p.n, margin = m.large ? 8 : 4;
    const int w = (m.w - 2 * margin) / n, h = (bottom - top) / n;
    cell = w < h ? w : h;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, n * cell, n * cell);
    lv_obj_set_pos(board_obj, (m.w - n * cell) / 2, top + (bottom - top - n * cell) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, press_cb, LV_EVENT_PRESSED, nullptr);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu --------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id <= kit::kLevel2) { start_new(id); return; }
    if (id == kit::kRestart) {
        kit::flash_stop();
        G->p = G->start;
        G->recorded = 0;
        G->seconds = 0;
        lv_obj_invalidate(board_obj);
        update_status();
    }
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu); }
void menu_back()  { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Sliding Tiles", kLevels, h);
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { app_go_home(); return; }
    if (!load(*G)) {
        G->level = 1;
        G->p.reset(4);
        sliding::Rng rng(shell().random_seed ? shell().random_seed() : 1);
        G->p.shuffle(rng);
        G->start = G->p;
    }
    saved_moves = G->p.moves;
    build();
}

void close()
{
    if (!G) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board_obj = info_l = again_k = nullptr;
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    if (!G) return;
    if (clock_.tick(now, !G->p.solved(), G->seconds)) update_status();
    if (G->p.moves != saved_moves || now - last_save_ms > 30000) {
        saved_moves = G->p.moves;
        last_save_ms = now;
        save();
    }
}

void restyle() { if (G) build(); }

bool summary(char* buf, size_t cap)
{
    Game tmp;
    const Game* g = G;
    if (!g) { if (!load(tmp)) return false; g = &tmp; }
    if (g->p.solved()) snprintf(buf, cap, "%s, solved", kLevels[g->level]);
    else               snprintf(buf, cap, "%s, %u moves", kLevels[g->level], (unsigned)g->p.moves);
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int size = lv_area_get_width(&a);
    Puzzle p;
    p.reset(3);
    p.tap(7);                            // one tile out of place
    const int c = size / 3;
    const lv_font_t* f = c >= 20 ? &lv_font_montserrat_14 : c >= 14 ? &lv_font_montserrat_10 : nullptr;
    draw_tiles(lv_event_get_layer(e), p, a.x1 + (size - 3 * c) / 2, a.y1 + (size - 3 * c) / 2, c, f, false);
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
extern const GameOps sliding_ops;
const GameOps sliding_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
