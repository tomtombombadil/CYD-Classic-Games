// CYD-dle: registry entry, save file and screen. The guesses grid and the
// keyboard are each one custom-drawn object (28 keys as objects would cost
// far more memory). Keys act when pressed. ✓ submits a guess; once the game
// is over it starts a new word. The keyboard shows what each letter has
// shown so far (green, gold, grey).
#include <cstdio>
#include <cstring>
#include <new>
#include "cyddle_core.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace cyddle;
using namespace ui;

constexpr const char* kId = "cyddle";
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
lv_obj_t*   grid_obj = nullptr;
lv_obj_t*   keys_obj = nullptr;
int         tile = 0, tile_gap = 0;
char        message[40] = "";            // e.g. "Not in word list" until the next key
int         saved_rows = -1;
uint32_t    last_save_ms = 0;

void build();
void open_menu();

// ---- Keyboard layout ---------------------------------------------------------------------
const char* const kRows[3] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
constexpr char kEnter = '\n', kBack = '\b';

struct KeyGeom { int key_w, key_h, gap, wide_w; };
KeyGeom kg;

// Calls fn(ch, x, y, w, h) for each key, relative to the keyboard object
template <class Fn>
void each_key(int width, Fn fn)
{
    for (int r = 0; r < 3; ++r) {
        const int n = static_cast<int>(strlen(kRows[r]));
        const int row_w = r == 2 ? n * kg.key_w + (n + 1) * kg.gap + 2 * kg.wide_w : n * kg.key_w + (n - 1) * kg.gap;
        int x = (width - row_w) / 2;
        const int y = r * (kg.key_h + kg.gap);
        if (r == 2) { fn(kEnter, x, y, kg.wide_w, kg.key_h); x += kg.wide_w + kg.gap; }
        for (int k = 0; k < n; ++k) { fn(kRows[r][k], x, y, kg.key_w, kg.key_h); x += kg.key_w + kg.gap; }
        if (r == 2) fn(kBack, x, y, kg.wide_w, kg.key_h);
    }
}

lv_color_t mark_bg(Mark m, lv_color_t none)
{
    const Palette& P = pal();
    switch (m) {
        case Correct: return P.win;
        case Present: return P.lit;
        case Absent:  return P.absent;
        default:      return none;
    }
}

// ---- Save --------------------------------------------------------------------------------
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

// ---- Game flow -------------------------------------------------------------------------------
void update_status()
{
    if (!bar.center) return;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    const Game& g = S->g;
    if (g.solved())          snprintf(s, sizeof s, "You got it!");
    else if (g.over()) {
        char a[kLen + 1] = {};
        answer_word(g.answer, a);
        for (char& c : a) if (c) c = static_cast<char>(c - 32);
        snprintf(s, sizeof s, "It was %s", a);
    }
    else if (message[0])     snprintf(s, sizeof s, "%s", message);
    else if (clock_.paused)  snprintf(s, sizeof s, "Paused");
    else                     snprintf(s, sizeof s, "Guess %d of %d", g.rows + 1, g.max_rows());
    kit::top_bar_status(bar, s);
}

void record(bool solved, bool lost = false)
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
    if (!S->g.over() && !S->recorded && (S->g.rows > 0 || S->seconds >= 30)) record(false);
    Rng rng(shell().random_seed ? shell().random_seed() : 1);
    S->g.start(level, rng);
    S->recorded = 0;
    S->seconds = 0;
    message[0] = 0;
    build();
    save();
}

void key(char c)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.over()) {                                 // ✓ = next word
        if (c == kEnter) start_new(g.level);
        return;
    }
    message[0] = 0;
    if (c == kBack) g.back();
    else if (c != kEnter) g.type(c);
    else {
        switch (g.submit()) {
            case Submit::TooShort:     snprintf(message, sizeof message, "Five letters, please"); sound(Sound::Error); break;
            case Submit::NotWord:      snprintf(message, sizeof message, "Not in word list"); sound(Sound::Error); break;
            case Submit::MustUseHints: snprintf(message, sizeof message, "Hard: use the hints"); sound(Sound::Error); break;
            case Submit::Ok:
                if (g.over() && !S->recorded) {
                    S->recorded = 1;
                    record(g.solved(), !g.solved());   // out of guesses = Lost
                    if (g.solved()) { sound(Sound::Win); kit::flash(); }
                    else sound(Sound::Lose);
                } else {
                    sound(Sound::Place);
                }
                save();
                break;
        }
    }
    lv_obj_invalidate(grid_obj);
    lv_obj_invalidate(keys_obj);
    update_status();
}

// ---- Drawing ----------------------------------------------------------------------------------
const lv_font_t* tile_font()
{
    return tile >= 40 ? &lv_font_montserrat_28 : tile >= 24 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
}

void draw_tile(lv_layer_t* layer, int x, int y, int size, char c, Mark m, bool typing)
{
    const Palette& P = pal();
    const lv_color_t bg = mark_bg(m, P.cell);
    if (m == Unknown) {
        kit::fill_rect(layer, x, y, x + size - 1, y + size - 1, typing && c ? P.ink : P.line_thin, 3);
        kit::fill_rect(layer, x + 2, y + 2, x + size - 3, y + size - 3, bg, 2);
    } else {
        kit::fill_rect(layer, x, y, x + size - 1, y + size - 1, bg, 3);
    }
    if (!c) return;
    const char s[2] = {static_cast<char>(c - 32), 0};
    kit::text(layer, s, tile_font(), m == Unknown ? P.ink : contrast_text(bg), x, y, size, size);
}

void grid_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Game& g = S->g;
    for (int r = 0; r < g.max_rows(); ++r) {
        Mark m[kLen] = {};
        if (r < g.rows) g.marks(r, m);
        for (int k = 0; k < kLen; ++k) {
            const int x = a.x1 + k * (tile + tile_gap), y = a.y1 + r * (tile + tile_gap);
            char c = 0;
            if (r < g.rows) c = g.guess[r][k];
            else if (r == g.rows && k < g.typed) c = g.typing[k];
            draw_tile(layer, x, y, tile, c, r < g.rows ? m[k] : Unknown, r == g.rows);
        }
    }
}

void keys_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const lv_font_t* f = kg.key_h >= 44 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    each_key(lv_area_get_width(&a), [&](char c, int x, int y, int w, int h) {
        const Mark m = (c >= 'a' && c <= 'z') ? S->g.key_mark(c) : Unknown;
        const bool enter_ready = c == kEnter && (S->g.typed == kLen || S->g.over());
        const lv_color_t bg = enter_ready ? P.key_on : mark_bg(m, P.key);
        kit::fill_rect(layer, a.x1 + x, a.y1 + y, a.x1 + x + w - 1, a.y1 + y + h - 1,
                       m == Unknown && !enter_ready ? P.key_border : bg, 5);
        kit::fill_rect(layer, a.x1 + x + 1, a.y1 + y + 1, a.x1 + x + w - 2, a.y1 + y + h - 2, bg, 4);
        char s[8];
        if (c == kEnter)     snprintf(s, sizeof s, "%s", LV_SYMBOL_OK);
        else if (c == kBack) snprintf(s, sizeof s, "%s", LV_SYMBOL_BACKSPACE);
        else                 snprintf(s, sizeof s, "%c", c - 32);
        const lv_color_t ink = enter_ready ? P.key_on_text : m == Unknown ? P.ink : contrast_text(bg);
        kit::text(layer, s, f, ink, a.x1 + x, a.y1 + y, w, h);
    });
}

void keys_press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int px = p.x - a.x1, py = p.y - a.y1;
    char hit = 0;
    int best = 1 << 30;
    // The nearest key wins, so the gaps between keys still count
    each_key(lv_area_get_width(&a), [&](char c, int x, int y, int w, int h) {
        const int dx = px < x ? x - px : px > x + w ? px - x - w : 0;
        const int dy = py < y ? y - py : py > y + h ? py - y - h : 0;
        const int d = dx * dx + dy * dy;
        if (d < best) { best = d; hit = c; }
    });
    if (hit) key(hit);
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 4 : 2;
    // Keyboard: 10 keys across the full width
    kg.gap = m.large ? 4 : 2;
    kg.key_w = (m.w - 2 * pad - 9 * kg.gap) / 10;
    kg.key_h = m.large ? 50 : 36;
    kg.wide_w = (m.w - 2 * pad - 7 * kg.key_w - 8 * kg.gap) / 2;
    const int kb_h = 3 * kg.key_h + 2 * kg.gap;
    keys_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(keys_obj);
    lv_obj_set_size(keys_obj, m.w - 2 * pad, kb_h);
    lv_obj_set_pos(keys_obj, pad, m.h - pad - kb_h);
    lv_obj_set_clickable(keys_obj, true);
    lv_obj_add_event_cb(keys_obj, keys_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(keys_obj, keys_press_cb, LV_EVENT_PRESSED, nullptr);
    // Grid: as big as fits between the bar and the keyboard
    const int rows = S->g.max_rows();
    const int top = bar.h + (m.large ? 6 : 3), bottom = m.h - pad - kb_h - (m.large ? 8 : 4);
    tile_gap = m.large ? 5 : 3;
    const int by_h = (bottom - top - (rows - 1) * tile_gap) / rows;
    const int by_w = (m.w - 2 * pad - 4 * tile_gap) / 5;
    tile = by_h < by_w ? by_h : by_w;
    const int gw = 5 * tile + 4 * tile_gap, gh = rows * tile + (rows - 1) * tile_gap;
    grid_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(grid_obj);
    lv_obj_set_size(grid_obj, gw, gh);
    lv_obj_set_pos(grid_obj, (m.w - gw) / 2, top + (bottom - top - gh) / 2);
    lv_obj_add_event_cb(grid_obj, grid_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu -------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id <= kit::kLevel2) { start_new(id); return; }
    if (id == kit::kRestart) {
        kit::flash_stop();
        S->g.restart();
        S->recorded = 0;
        S->seconds = 0;
        message[0] = 0;
        build();
    }
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu); }
void menu_back()  { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("CYD-dle", kLevels, h);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        *S = State{};
        Rng rng(shell().random_seed ? shell().random_seed() : 1);
        S->g.start(1, rng);
    }
    message[0] = 0;
    saved_rows = S->g.rows;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    grid_obj = keys_obj = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.over(), S->seconds)) update_status();
    if (now - last_save_ms > 30000) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State* st = S;
    State* tmp = nullptr;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    if (st->g.solved())     snprintf(buf, cap, "%s, solved in %d", kLevels[st->g.level], st->g.rows);
    else if (st->g.over())  snprintf(buf, cap, "%s, out of guesses", kLevels[st->g.level]);
    else                    snprintf(buf, cap, "%s, guess %d of %d", kLevels[st->g.level], st->g.rows + 1, st->g.max_rows());
    delete tmp;
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a);
    const int t = size / 3 - 1, gap = size - 3 * t > 2 ? (size - 3 * t) / 3 : 1;
    // Three rows of three tiles spelling C-Y-D with the marks of a game
    static const char letters[3][3] = {{'c', 'a', 't'}, {'c', 'y', 'n'}, {'c', 'y', 'd'}};
    static const Mark marks[3][3] = {{Correct, Absent, Absent}, {Correct, Correct, Present}, {Correct, Correct, Correct}};
    const lv_font_t* f = t >= 24 ? &lv_font_montserrat_20 : t >= 12 ? &lv_font_montserrat_12 : nullptr;
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) {
            const int x = a.x1 + k * (t + gap), y = a.y1 + r * (t + gap);
            const lv_color_t bg = mark_bg(marks[r][k], pal().cell);
            kit::fill_rect(layer, x, y, x + t - 1, y + t - 1, bg, 2);
            if (f) {
                const char s[2] = {static_cast<char>(letters[r][k] - 32), 0};
                kit::text(layer, s, f, contrast_text(bg), x, y, t, t);
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
extern const GameOps cyddle_ops;
const GameOps cyddle_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
