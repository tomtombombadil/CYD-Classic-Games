// Light Switch (a Lights Out style puzzle): registry entry, save file and
// screen. Tap a light to flip it and its four neighbours; turn every light
// off. The top bar shows moves and par (the fewest presses that solve it).
// Hint: the first tap points at a light from a shortest solution, the
// second tap presses it (the same two-tap hint as Sudoku).
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "lightswitch_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using lightswitch::Puzzle;
using lightswitch::kN;
using lightswitch::kCells;
using namespace ui;

constexpr const char* kId = "lightswitch";
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};

struct Game {
    Puzzle   p;
    uint8_t  level = 0;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
Game* G = nullptr;

constexpr size_t kSaveBytes = Puzzle::kSaveBytes + 6;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board_obj = nullptr;
lv_obj_t*   info_l = nullptr;
lv_obj_t*   again_k = nullptr;
lv_obj_t*   hint_k = nullptr;
int         cell = 0;
int         hint_cell = -1;
int         saved_moves = -1;
uint32_t    last_save_ms = 0;

void build();
void open_menu();

// ---- Save --------------------------------------------------------------------------
size_t pack(const Game& g, uint8_t* buf)
{
    size_t n = g.p.serialize(buf, Puzzle::kSaveBytes);
    buf[n++] = g.level;
    buf[n++] = g.recorded;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(g.seconds >> (8 * k));
    return n;
}

bool unpack(Game& g, const uint8_t* buf, size_t len)
{
    if (len != kSaveBytes) return false;
    Game t;
    if (!t.p.deserialize(buf, Puzzle::kSaveBytes)) return false;
    const uint8_t* q = buf + Puzzle::kSaveBytes;
    if (q[0] > 2) return false;
    t.level = q[0];
    t.recorded = q[1] ? 1 : 0;
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

// ---- Game flow -----------------------------------------------------------------------
void update_status()
{
    if (!bar.center) return;
    char t[16], s[32];
    twoplayer::format_time(t, sizeof t, G->seconds);
    lv_label_set_text(bar.left, t);
    if (G->p.solved())      snprintf(s, sizeof s, "Solved!");
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else if (hint_cell >= 0) snprintf(s, sizeof s, "Tap Hint to press");
    else                    snprintf(s, sizeof s, "Moves %u  Par %u", (unsigned)G->p.moves, (unsigned)G->p.par);
    kit::top_bar_status(bar, s);
    char in[48];
    if (G->p.solved()) snprintf(in, sizeof in, "%s solved in %u (par %u)", kLevels[G->level],
                                (unsigned)G->p.moves, (unsigned)G->p.par);
    else               snprintf(in, sizeof in, "%s. Turn every light off.", kLevels[G->level]);
    lv_label_set_text(info_l, in);
    if (G->p.solved()) { lv_obj_set_hidden(again_k, false); lv_obj_set_hidden(hint_k, true); }
    else               { lv_obj_set_hidden(again_k, true); lv_obj_set_hidden(hint_k, false); }
    set_checked(hint_k, hint_cell >= 0);
}

void record(bool solved)
{
    puzzle::Record r;
    r.level = G->level;
    r.solved = solved;
    r.moves = G->p.moves;
    r.par = G->p.par;
    r.seconds = G->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!G->p.solved() && !G->recorded && (G->p.moves > 0 || G->seconds >= 30)) record(false);
    G->level = static_cast<uint8_t>(level);
    lightswitch::Rng rng(shell().random_seed ? shell().random_seed() : 1);
    G->p.generate(level, rng);
    G->recorded = 0;
    G->seconds = 0;
    hint_cell = -1;
    build();
}

void press(int i)
{
    if (!G || G->p.solved() || overlay_open()) return;
    G->p.press(i);
    hint_cell = -1;
    lv_obj_invalidate(board_obj);
    if (G->p.solved() && !G->recorded) {
        G->recorded = 1;
        record(true);
        sound(Sound::Win);
        kit::flash();
        save();
    } else {
        sound(Sound::Place);
    }
    update_status();
}

void hint_cb(lv_event_t*)
{
    if (!G || G->p.solved()) return;
    if (hint_cell >= 0) {
        const int c = hint_cell;
        press(c);
        if (!G->p.solved()) sound(Sound::Hint);     // a solve keeps its win sound
        return;
    }
    hint_cell = G->p.hint();
    lv_obj_invalidate(board_obj);
    update_status();
}

// ---- Board ------------------------------------------------------------------------------
void draw_lights(lv_layer_t* layer, uint32_t lights, int x0, int y0, int c, int hint)
{
    const Palette& P = pal();
    const int n = kN, gap = c >= 40 ? 4 : c >= 16 ? 3 : 1;
    kit::fill_rect(layer, x0, y0, x0 + n * c - 1, y0 + n * c - 1, P.line_thin, c / 5);
    for (int i = 0; i < kCells; ++i) {
        const int x = x0 + (i % n) * c, y = y0 + (i / n) * c;
        const bool on = (lights >> i) & 1;
        kit::fill_rect(layer, x + gap, y + gap, x + c - 1 - gap, y + c - 1 - gap, on ? P.lit : P.cell, c / 5);
        if (i == hint) {
            const int w = c >= 40 ? 4 : 3;
            kit::fill_rect(layer, x + gap, y + gap, x + c - 1 - gap, y + gap + w - 1, P.selected);
            kit::fill_rect(layer, x + gap, y + c - gap - w, x + c - 1 - gap, y + c - 1 - gap, P.selected);
            kit::fill_rect(layer, x + gap, y + gap, x + gap + w - 1, y + c - 1 - gap, P.selected);
            kit::fill_rect(layer, x + c - gap - w, y + gap, x + c - 1 - gap, y + c - 1 - gap, P.selected);
        }
    }
}

void draw_cb(lv_event_t* e)
{
    if (!G) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    draw_lights(lv_event_get_layer(e), G->p.lights, a.x1, a.y1, cell, hint_cell);
}

void press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev || !G) return;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int c = (pt.x - a.x1) / cell, r = (pt.y - a.y1) / cell;
    c = c < 0 ? 0 : c >= kN ? kN - 1 : c;
    r = r < 0 ? 0 : r >= kN ? kN - 1 : r;
    press(r * kN + c);
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
    const int ky = m.h - pad - ih - pad - kh;
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);
    hint_k = make_key(scr, m.w - 2 * pad, kh, hint_cb, 0);
    key_label(hint_k, "Hint", menu_font());
    lv_obj_set_pos(hint_k, pad, ky);
    const int top = bar.h, bottom = ky - pad;
    const int margin = m.large ? 8 : 4;
    const int w = (m.w - 2 * margin) / kN, h = (bottom - top) / kN;
    cell = w < h ? w : h;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, kN * cell, kN * cell);
    lv_obj_set_pos(board_obj, (m.w - kN * cell) / 2, top + (bottom - top - kN * cell) / 2);
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
        // A solved puzzle replayed isn't a new result for the stats
        const bool was_solved = G->p.solved();
        G->p.restart();
        G->recorded = was_solved ? 1 : 0;
        G->seconds = 0;
        hint_cell = -1;
        lv_obj_invalidate(board_obj);
        update_status();
    }
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu); }
void menu_back()  { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Light Switch", kLevels, h);
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { app_go_home(); return; }
    if (!load(*G)) {
        lightswitch::Rng rng(shell().random_seed ? shell().random_seed() : 1);
        G->p.generate(0, rng);
    }
    hint_cell = -1;
    saved_moves = G->p.moves;
    build();
}

void close()
{
    if (!G) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board_obj = info_l = again_k = hint_k = nullptr;
    delete G;
    G = nullptr;
}

void tick(uint32_t now)
{
    if (!G) return;
    if (clock_.tick(now, !G->p.solved(), G->seconds)) update_status();
    if (G->p.moves != saved_moves || kit::save_due(now, last_save_ms, G->seconds)) {
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
    else               snprintf(buf, cap, "%s, %u moves (par %u)", kLevels[g->level],
                                (unsigned)g->p.moves, (unsigned)g->p.par);
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int size = lv_area_get_width(&a), c = size / kN;
    // A plus of lit lights
    const uint32_t art = lightswitch::press_mask(12) | (1u << 0) | (1u << 24);
    draw_lights(lv_event_get_layer(e), art, a.x1 + (size - kN * c) / 2, a.y1 + (size - kN * c) / 2, c, -1);
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
extern const GameOps lightswitch_ops;
const GameOps lightswitch_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
