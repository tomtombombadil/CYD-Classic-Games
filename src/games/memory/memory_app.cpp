// Memory Match: registry entry, save file and screen. Rules in memory_core.*.
//
// Screen: top bar (clock, pairs found, ☰) and the tiles, one custom-drawn
// object. Face down a tile shows the player's card back (shared by the
// card games); face up
// a picture (an LVGL symbol, each in its own color as a second cue). A
// missed pair stays up, outlined, until the next tap.
//
// Sounds: a pair found, a miss, the last pair. Turning one tile is silent.
#include <cstdio>
#include <new>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "memory_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace memory;
using namespace ui;

constexpr const char* kId = "memory";
const char* const kLevels[3] = {"4x4", "4x5", "5x6"};

const char* const kPics[kPictures] = {
    LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_AUDIO, LV_SYMBOL_IMAGE, LV_SYMBOL_TINT,
    LV_SYMBOL_CHARGE, LV_SYMBOL_GPS, LV_SYMBOL_ENVELOPE, LV_SYMBOL_CALL, LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_SETTINGS, LV_SYMBOL_WIFI, LV_SYMBOL_CUT, LV_SYMBOL_EDIT, LV_SYMBOL_DIRECTORY};

lv_color_t pic_color(int p)
{
    const Palette& P = pal();
    switch (p % 5) {
        case 0:  return P.piece_a;
        case 1:  return P.frame;
        case 2:  return P.win;
        case 3:  return P.selected;
        default: return P.stone_dark;
    }
}

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   grid_obj = nullptr;
lv_obj_t*   again_k = nullptr;
int         tw = 0, th = 0, gap = 0;
uint32_t    last_save_ms = 0;

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

// ---- Flow ----------------------------------------------------------------------------------
void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (g.solved())         snprintf(s, sizeof s, "All found in %u turns", (unsigned)g.turns);
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Pairs %d of %d", g.found, g.pairs());
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !g.solved());
    lv_obj_invalidate(grid_obj);
}

void record(bool solved)
{
    puzzle::Record r;
    r.level = S->g.level;
    r.solved = solved;
    r.moves = S->g.turns;
    r.par = static_cast<uint16_t>(S->g.pairs());
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void start_new(int level)
{
    kit::flash_stop();
    if (!S->g.solved() && !S->recorded && S->g.turns > 0) record(false);   // gave up
    *S = State{};
    Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->g.start(level, rng);
    save();
    build();
}

// ---- Drawing --------------------------------------------------------------------------------
void draw_tile(lv_layer_t* layer, int x, int y, int w, int h, const Game& g, int i)
{
    const Palette& P = pal();
    if (!g.face_up(i)) {
        cards::draw_back(layer, x, y, w, h);              // the player's card back
        return;
    }
    const bool miss = g.up_b >= 0 && (i == g.up_a || i == g.up_b);
    const lv_color_t edge = g.matched[i] ? P.win : miss ? P.conflict : P.selected;
    const int bw = w >= 50 ? 3 : 2, rad = w / 8;
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, edge, rad);
    kit::fill_rect(layer, x + bw, y + bw, x + w - 1 - bw, y + h - 1 - bw, P.stone_light, rad > bw ? rad - bw : 0);
    kit::text(layer, kPics[g.pic[i]], &lv_font_montserrat_28, pic_color(g.pic[i]), x, y, w, h);
}

void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(grid_obj, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    for (int i = 0; i < g.tiles(); ++i) {
        const int x = a.x1 + (i % g.cols) * (tw + gap), y = a.y1 + (i / g.cols) * (th + gap);
        draw_tile(layer, x, y, tw, th, g, i);
    }
}

void tap_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(grid_obj, &a);
    Game& g = S->g;
    const int c = (p.x - a.x1) / (tw + gap), r = (p.y - a.y1) / (th + gap);
    if (c < 0 || c >= g.cols || r < 0 || r >= g.rows) return;
    switch (g.tap(r * g.cols + c)) {
        case Tap::Ignored: break;
        case Tap::First:   break;
        case Tap::Miss:    sound(Sound::Error); break;
        case Tap::Match:
            if (g.solved() && !S->recorded) {
                S->recorded = 1;
                record(true);
                sound(Sound::Win);
                kit::flash();
            } else {
                sound(Sound::Place);
            }
            break;
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
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    gap = m.large ? 8 : 5;
    const Game& g = S->g;
    // Tiles fill the screen below the bar, leaving room for Play Again
    const int top = bar.h + gap, bottom = m.h - pad - kh - gap;
    tw = (m.w - 2 * pad - (g.cols - 1) * gap) / g.cols;
    th = (bottom - top - (g.rows - 1) * gap) / g.rows;
    if (th > tw * 3 / 2) th = tw * 3 / 2;
    const int gw = g.cols * tw + (g.cols - 1) * gap, gh = g.rows * th + (g.rows - 1) * gap;
    grid_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(grid_obj);
    lv_obj_set_size(grid_obj, gw, gh);
    lv_obj_set_pos(grid_obj, (m.w - gw) / 2, top + (bottom - top - gh) / 2);
    lv_obj_set_clickable(grid_obj, true);
    lv_obj_add_event_cb(grid_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(grid_obj, tap_cb, LV_EVENT_PRESSED, nullptr);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, m.h - pad - kh);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu ---------------------------------------------------------------------------------------
void menu_pick(int id) { if (id <= kit::kLevel2) start_new(id); }
void menu_stats()      { kit::stats_solo(kId, kLevels, open_menu); }
void menu_back()       { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Memory Match", kLevels, h, false);
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
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    grid_obj = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.solved(), S->seconds)) update_status();
    if (now - last_save_ms > 30000) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    const Game& g = st->g;
    if (g.solved()) snprintf(buf, cap, "%s, all found in %u turns", kLevels[g.level], (unsigned)g.turns);
    else            snprintf(buf, cap, "%s, %d of %d pairs", kLevels[g.level], g.found, g.pairs());
    return true;
}

void save_now() { save(); }

// Icon: two face-up tiles that match and one face down
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), w = size * 45 / 100, h = size * 45 / 100, g = size - 2 * w;
    cards::draw_back(layer, a.x1, a.y1, w, h);
    for (int k = 0; k < 2; ++k) {
        const int x = a.x1 + (k ? w + g : w / 2 + g / 2), y = a.y1 + (k ? 0 : h + g);
        kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, P.win, w / 8);
        kit::fill_rect(layer, x + 2, y + 2, x + w - 3, y + h - 3, P.stone_light, w / 8);
        kit::text(layer, LV_SYMBOL_BELL, size >= 60 ? &lv_font_montserrat_20 : &lv_font_montserrat_14,
                  pic_color(1), x, y, w, h);
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
extern const GameOps memory_ops;
const GameOps memory_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
