// 2048: registry entry, save file and screen. Rules in twenty48_core.*.
//
// Screen: top bar (clock, score, ☰), the board, and a line with your best
// score and the move count. No swiping on a resistive screen (Tom's design,
// 2026-10-03): the board's two diagonals, extended to the screen edges,
// split everything below the top bar into four invisible tap zones. A tap
// above the board's centre (between the diagonals) slides up, right of it
// slides right, and so on. One custom-drawn object covers the whole area.
//
// Sounds: a slide that moves, making 2048, the end of the game. A tap that
// moves nothing is silent.
#include <cstdio>
#include <cstdlib>
#include <new>
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "twenty48_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace twenty48;
using namespace ui;

constexpr const char* kId = "twenty48";

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;            // everything below the top bar: board + tap zones
lv_obj_t*   again_k = nullptr;
int         bx = 0, by = 0, bsize = 0; // board, relative to `area`
int         info_y = 0;
uint32_t    best_score = 0;            // from the stats, for "Best"
uint32_t    last_save_ms = 0;

void build();
void open_menu();

// ---- Save ----------------------------------------------------------------------------------
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

// ---- Stats ---------------------------------------------------------------------------------
void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

void load_best()
{
    Summary* sum = new (std::nothrow) Summary();
    best_score = 0;
    if (!sum) return;
    if (shell().stats_read && shell().stats_read(kId, stats_line, sum)) best_score = sum->best;
    delete sum;
}

void record()
{
    Record r;
    r.score = S->g.score;
    r.tile = Game::value(S->g.max_exp());
    r.moves = S->g.moves;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
    if (r.score > best_score) best_score = r.score;
}

// ---- Game flow -----------------------------------------------------------------------------
Rng rng_now() { return Rng(shell().random_seed ? shell().random_seed() : lv_tick_get()); }

// Clock ticks only change the top bar: the board isn't redrawn for them
// (a full card table redraw every second slowed taps down - Tom).
bool ticking = false;

void update_status()
{
    if (!bar.center || !S) return;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (S->g.over())        snprintf(s, sizeof s, "Final Score %lu", (unsigned long)S->g.score);
    else if (clock_.paused) snprintf(s, sizeof s, "Paused");
    else                    snprintf(s, sizeof s, "Score %lu", (unsigned long)S->g.score);
    kit::top_bar_status(bar, s);
    lv_obj_set_hidden(again_k, !S->g.over());
    if (!ticking) lv_obj_invalidate(area);
}

void new_game()
{
    kit::flash_stop();
    // Finished games are recorded when they end; a game left for a new one
    // is recorded only if it reached 2048, so a win is never lost.
    if (!S->recorded && S->g.won) record();
    *S = State{};
    Rng rng = rng_now();
    S->g.start(rng);
    save();
    update_status();
}

void do_slide(Dir d)
{
    Game& g = S->g;
    if (g.over() || overlay_open()) return;
    const bool had_won = g.won;
    Rng rng = rng_now();
    uint8_t made = 0;
    if (!g.slide(d, rng, &made)) return;           // nothing moved: silent
    if (g.won && !had_won) { sound(Sound::Win); kit::flash(); }
    else                   sound(Sound::Move);
    if (g.over() && !S->recorded) {
        record();
        sound(Sound::Lose);
    }
    save();
    update_status();
}

// ---- Drawing -------------------------------------------------------------------------------
lv_color_t mix(lv_color_t a, lv_color_t b, int t, int n)     // a..b, step t of n
{
    return lv_color_mix(b, a, static_cast<uint8_t>(255 * t / n));
}

// Tile colors: a ramp drawn from the theme, so custom themes recolor it too.
// 2..32 pale wood -> amber -> red; 64..1024 glade green -> blue; 2048 gold;
// beyond that the text color.
lv_color_t tile_color(uint8_t e)
{
    const Palette& P = pal();
    if (e <= 3) return mix(P.sq_light, P.selected, e - 1, 2);
    if (e <= 5) return mix(P.selected, P.piece_a, e - 3, 2);
    if (e <= 10) return mix(P.felt, P.frame, e - 6, 4);
    if (e == 11) return P.lit;
    return P.ink;
}

lv_color_t text_on(lv_color_t bg)
{
    const Palette& P = pal();
    return lv_color_luminance(bg) > 150 ? P.stone_dark : P.stone_light;
}

void draw_tile(lv_layer_t* layer, int x, int y, int s, uint8_t e, bool fresh)
{
    const Palette& P = pal();
    const int rad = s / 10;
    const lv_color_t bg = tile_color(e);
    kit::fill_rect(layer, x, y, x + s - 1, y + s - 1, bg, rad);
    if (fresh) {                                   // the newest tile: a ring
        const int w = s >= 60 ? 3 : 2;
        kit::fill_rect(layer, x, y, x + s - 1, y + w - 1, P.ink, 0);
        kit::fill_rect(layer, x, y + s - w, x + s - 1, y + s - 1, P.ink, 0);
        kit::fill_rect(layer, x, y, x + w - 1, y + s - 1, P.ink, 0);
        kit::fill_rect(layer, x + s - w, y, x + s - 1, y + s - 1, P.ink, 0);
    }
    char t[12];
    snprintf(t, sizeof t, "%lu", (unsigned long)Game::value(e));
    static const lv_font_t* const fonts[3] = {&lv_font_montserrat_28, &lv_font_montserrat_20, &lv_font_montserrat_14};
    const lv_font_t* f = fonts[2];
    for (const lv_font_t* c : fonts)
        if (text_width(t, c) <= s - 4) { f = c; break; }
    kit::text(layer, t, f, text_on(bg), x, y, s, s);
}

void draw_board(lv_layer_t* layer, const Game& g, int x0, int y0, int size, bool marker)
{
    const Palette& P = pal();
    const int gap = size >= 280 ? 8 : size >= 200 ? 6 : 3;
    const int s = (size - 5 * gap) / 4;
    const int used = 4 * s + 5 * gap;
    kit::fill_rect(layer, x0, y0, x0 + used - 1, y0 + used - 1, P.key_border, size / 30);
    const lv_color_t empty = lv_color_mix(P.key_border, P.screen, 100);
    for (int i = 0; i < kCells; ++i) {
        const int x = x0 + gap + (i % 4) * (s + gap), y = y0 + gap + (i / 4) * (s + gap);
        if (g.cell[i]) draw_tile(layer, x, y, s, g.cell[i], marker && i == g.spawned);
        else           kit::fill_rect(layer, x, y, x + s - 1, y + s - 1, empty, s / 10);
    }
}

void area_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    draw_board(layer, S->g, a.x1 + bx, a.y1 + by, bsize, true);
    char t[64];
    const uint32_t best = S->g.score > best_score ? S->g.score : best_score;
    if (S->g.won && !S->g.over())
        snprintf(t, sizeof t, "2048 made! Best %lu", (unsigned long)best);
    else
        snprintf(t, sizeof t, "Best %lu    Moves %u", (unsigned long)best, (unsigned)S->g.moves);
    kit::text(layer, t, bar_font(), pal().muted, a.x1, a.y1 + info_y, lv_area_get_width(&a),
              lv_font_get_line_height(bar_font()));
}

// The four zones: compare the tap with the board's centre. Between the
// diagonals above the centre = Up, right of it = Right, and so on.
void area_tap_cb(lv_event_t* e)
{
    if (!S) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int dx = p.x - (a.x1 + bx + bsize / 2), dy = p.y - (a.y1 + by + bsize / 2);
    Dir d;
    if (abs(dy) >= abs(dx)) d = dy < 0 ? Dir::Up : Dir::Down;
    else                    d = dx < 0 ? Dir::Left : Dir::Right;
    (void)e;
    do_slide(d);
}

void again_cb(lv_event_t*) { new_game(); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int line_h = lv_font_get_line_height(bar_font());
    const int top = bar.h, h = m.h - top;
    // Board: as big as fits above the info line and the Play Again key
    int size = m.w - 2 * pad;
    const int room = h - (m.large ? 12 : 6) - line_h - kh - 3 * pad;
    if (size > room) size = room;
    bsize = size;
    bx = (m.w - size) / 2;
    by = m.large ? 8 : 4;
    info_y = by + size + (m.large ? 10 : 6);

    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, h);
    lv_obj_set_pos(area, 0, top);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, area_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, area_tap_cb, LV_EVENT_CLICKED, nullptr);

    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, m.h - pad - kh);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Stats screen --------------------------------------------------------------------------
void stats_back_cb(lv_event_t*) { open_menu(); }
void stats_action_cb(lv_event_t* e);

void open_stats()
{
    overlay_begin("Stats");
    static Summary sum;
    sum = Summary{};
    const bool ok = shell().stats_read && shell().stats_read(kId, stats_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum.games == 0) {
        overlay_text("No games recorded yet. Every game played to the end is listed here, "
                     "and any game that reached 2048.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[12], b[12], c[12], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.best);
        snprintf(c, sizeof c, "%lu", (unsigned long)sum.average());
        snprintf(d, sizeof d, "%lu", (unsigned long)sum.best_tile);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Best", "Average", "Top Tile"};
        static const int8_t pct[4] = {22, 24, 26, 28};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[12], s2[12], s3[12], tm[16];
            snprintf(s1, sizeof s1, "%lu", (unsigned long)r.score);
            snprintf(s2, sizeof s2, "%lu", (unsigned long)r.tile);
            snprintf(s3, sizeof s3, "%u", (unsigned)r.moves);
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, s3, tm);
        }
        const char* const head2[4] = {"Recent", "Tile", "Moves", "Time"};
        static const int8_t pct2[4] = {26, 22, 24, 28};
        table_show(rt, head2, pct2, hf, hf);
    }
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.", shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    const int bh = menu_btn_h(), gap = large ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && sum.games > 0 && shell().stats_delete_last && shell().stats_clear) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), bh);
        lv_obj_set_ignore_layout(row, true);
        lv_obj_align_to(row, back, LV_ALIGN_OUT_TOP_MID, 0, -gap);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        const char* labels[2] = {"Delete Last", "Clear All"};
        for (int k = 0; k < 2; ++k) {
            lv_obj_t* b = make_key(row, 10, bh, stats_action_cb, k + 1);
            lv_obj_set_flex_grow(b, 1);
            key_label(b, labels[k], menu_font());
        }
    }
}

void stats_action_cb(lv_event_t* e)
{
    const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (which == 1 && shell().stats_delete_last) shell().stats_delete_last(kId);
    if (which == 2 && shell().stats_clear) shell().stats_clear(kId);
    load_best();
    open_stats();
}

// ---- Menu ---------------------------------------------------------------------------------
void menu_pick(int id) { if (id == kit::kLevel0) new_game(); }
void menu_back()       { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("2048", nullptr, h, false);
}

// ---- Registry entry -----------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; Rng rng = rng_now(); S->g.start(rng); }
    load_best();
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    area = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (clock_.tick(now, !S->g.over(), S->seconds)) { ticking = true; update_status(); ticking = false; }
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    if (st->g.over()) snprintf(buf, cap, "Final score %lu", (unsigned long)st->g.score);
    else              snprintf(buf, cap, "Score %lu, top tile %lu", (unsigned long)st->g.score,
                               (unsigned long)Game::value(st->g.max_exp()));
    return true;
}

void save_now() { save(); }

// Icon: four big tiles, 2 4 / 8 16, on the board color
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), gap = size / 16 > 2 ? size / 16 : 2;
    const int s = (size - 3 * gap) / 2;
    kit::fill_rect(layer, a.x1, a.y1, a.x1 + size - 1, a.y1 + size - 1, pal().key_border, size / 12);
    const uint8_t e4[4] = {1, 2, 3, 4};
    for (int i = 0; i < 4; ++i)
        draw_tile(layer, a.x1 + gap + (i % 2) * (s + gap), a.y1 + gap + (i / 2) * (s + gap), s, e4[i], false);
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
extern const GameOps twenty48_ops;
const GameOps twenty48_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
