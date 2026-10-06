// Pipe Race: registry entry, save file and screen. Rules in piperace_core.*.
//
// Screen: top bar (score, level and pipes or the countdown), the next five
// pieces in a row (the next one edged in gold), the 8x8 board, and one key
// at the bottom: Water Now (start it at once), Fast Flow, Next Level or
// Play Again. A tap on a square lays the next piece there. The water is
// drawn as it goes: each frame redraws only the square it is in, so the
// flow stays smooth on the slow panels.
//
// The water only runs while the game is on screen with nothing over it;
// a game reopened from a save starts paused (Continue).
//
// Sounds: a piece laid (Place), swapped (Move), the water starting (Turn),
// a level cleared (Trill), the game over (Lose). Filling pipes is silent.
#include <cstdio>
#include <cstdlib>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "piperace_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace piperace;
using namespace ui;

constexpr const char* kId = "piperace";

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;            // the queue + board, one custom-drawn object
lv_obj_t*   go_k = nullptr;
lv_obj_t*   go_l = nullptr;
int         cell = 26;                 // square size
int         bx = 0, by = 0;            // board, relative to `area`
int         qx = 0, qy = 0, qs = 0;    // queue row
bool        held = false;              // paused until Continue
uint32_t    last_ms = 0, last_save_ms = 0;
int         drawn_head = -1;
uint32_t    last_wait_s = 0;
int32_t     best_score = 0;

void open_menu();
void update_status();

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
    r.level = S->g.level;
    r.pipes = S->g.total;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
    if (r.score > best_score) best_score = r.score;
}

// ---- Drawing -------------------------------------------------------------------------------
lv_color_t water() { return lv_color_mix(pal().frame, pal().stone_light, 215); }
lv_color_t empty_sq() { return lv_color_mix(pal().key_border, pal().screen, 110); }

// A pipe's straight run from the middle of the square to `side`; `from`..`to`
// are 0 (the middle) .. 1 (the edge); `inset` narrows it (the channel)
void arm(lv_layer_t* layer, int x, int y, int s, uint8_t side, int half, float from, float to, lv_color_t c)
{
    const int cx = x + s / 2, cy = y + s / 2, r = s / 2;
    const int a = int(from * r), b = int(to * r + 0.5f);
    switch (side) {
        case kN: kit::fill_rect(layer, cx - half, cy - b, cx + half - 1 + (s & 1), cy - a, c); break;
        case kS: kit::fill_rect(layer, cx - half, cy + a, cx + half - 1 + (s & 1), cy + b + (s & 1), c); break;
        case kW: kit::fill_rect(layer, cx - b, cy - half, cx - a, cy + half - 1 + (s & 1), c); break;
        case kE: kit::fill_rect(layer, cx + a, cy - half, cx + b + (s & 1), cy + half - 1 + (s & 1), c); break;
    }
}

// One square: what's in it, how full of water
void draw_square(lv_layer_t* layer, int x, int y, int s, uint8_t p, uint8_t fill, bool head, uint8_t in, float prog,
                 float tank_level)
{
    const Palette& P = pal();
    kit::fill_rect(layer, x, y, x + s - 1, y + s - 1, empty_sq(), s / 8);
    if (p == kEmpty) return;
    if (p == kRock) {
        // A boulder: dark edge, grey body, a lighter top
        const int m = s / 7;
        const lv_color_t body = lv_color_mix(P.stone_light, P.stone_dark, 110);
        kit::fill_rect(layer, x + m, y + m + 1, x + s - 1 - m, y + s - 1 - m, P.stone_dark, s / 3);
        kit::fill_rect(layer, x + m + 2, y + m + 3, x + s - 3 - m, y + s - 3 - m, body, s / 3);
        kit::fill_rect(layer, x + m + s / 6, y + m + s / 8, x + s - 1 - m - s / 4, y + s / 2 - 1,
                       lv_color_mix(P.stone_light, body, 140), s / 5);
        return;
    }
    const uint8_t open = sides(p);
    const int outer = s * 21 / 100 > 3 ? s * 21 / 100 : 3;      // half the pipe's width
    const int inner = outer - (s >= 34 ? 3 : 2);
    const lv_color_t wall = P.stone_dark, bore = lv_color_mix(P.stone_light, P.stone_dark, 170), w = water();
    const uint8_t all[4] = {kN, kE, kS, kW};
    if (is_start(p)) {
        // The tank: a round drum, its outlet a short pipe; water rises in it during the countdown
        for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, outer, 0, 1, wall);
        for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, inner, 0, 1, bore);
        const int R = s * 40 / 100;
        kit::fill_circle(layer, x + s / 2, y + s / 2, R, wall);
        kit::fill_circle(layer, x + s / 2, y + s / 2, R - 2, bore);
        if (tank_level > 0) kit::fill_circle(layer, x + s / 2, y + s / 2, int((R - 2) * tank_level), w);
        if (head) {
            for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, inner, 0, prog, w);
        } else if (tank_level >= 1) {
            for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, inner, 0, 1, w);
        }
        return;
    }
    // Walls first, then the bore, so bends and crosses join cleanly
    for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, outer, 0, 1, wall);
    kit::fill_rect(layer, x + s / 2 - outer, y + s / 2 - outer, x + s / 2 + outer - 1 + (s & 1), y + s / 2 + outer - 1 + (s & 1), wall);
    for (uint8_t sd : all) if (open & sd) arm(layer, x, y, s, sd, inner, 0, 1, bore);
    if (p != kCross) kit::fill_rect(layer, x + s / 2 - inner, y + s / 2 - inner, x + s / 2 + inner - 1 + (s & 1), y + s / 2 + inner - 1 + (s & 1), bore);
    // The water: whole runs already filled, then the one it is in
    auto fill_side = [&](uint8_t sd) { arm(layer, x, y, s, sd, inner, 0, 1, w); };
    if (p == kCross) {
        if (fill & 1) { fill_side(kE); fill_side(kW); }
        if (fill & 2) { fill_side(kN); fill_side(kS); }
        if (head && in) {
            const uint8_t out = opposite(in);
            if (prog < 0.5f) arm(layer, x, y, s, in, inner, 1 - prog * 2, 1, w);
            else { fill_side(in); arm(layer, x, y, s, out, inner, 0, (prog - 0.5f) * 2, w); }
        }
        return;
    }
    if (fill) {
        for (uint8_t sd : all) if (open & sd) fill_side(sd);
        kit::fill_rect(layer, x + s / 2 - inner, y + s / 2 - inner, x + s / 2 + inner - 1 + (s & 1), y + s / 2 + inner - 1 + (s & 1), w);
    } else if (head && in) {
        const uint8_t out = uint8_t(open & ~in);
        if (prog < 0.5f) arm(layer, x, y, s, in, inner, 1 - prog * 2, 1, w);
        else {
            fill_side(in);
            kit::fill_rect(layer, x + s / 2 - inner, y + s / 2 - inner, x + s / 2 + inner - 1 + (s & 1), y + s / 2 + inner - 1 + (s & 1), w);
            arm(layer, x, y, s, out, inner, 0, (prog - 0.5f) * 2, w);
        }
    }
}

void area_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int x0 = a.x1 + bx, y0 = a.y1 + by;
    // The next pieces: the first one edged in gold
    for (int i = 0; i < kQueue; ++i) {
        const int x = a.x1 + qx + i * (qs + qs / 5), y = a.y1 + qy;
        if (i == 0) kit::fill_rect(layer, x - 3, y - 3, x + qs + 2, y + qs + 2, P.lit, qs / 6);
        draw_square(layer, x, y, qs, g.queue[i], 0, false, 0, 0, 0);
    }
    // The board
    const int gap = cell >= 34 ? 2 : 1;
    kit::fill_rect(layer, x0 - 2, y0 - 2, x0 + kCols * cell + 1, y0 + kRows * cell + 1, P.key_border, 4);
    const float tank = g.phase == Phase::Waiting ? 1.0f - float(g.wait_ms) / float(g.wait_total()) : 1.0f;
    for (int c = 0; c < kCells; ++c) {
        const int x = x0 + (c % kCols) * cell, y = y0 + (c / kCols) * cell;
        const bool head = g.phase == Phase::Flowing && c == g.head;
        draw_square(layer, x + gap, y + gap, cell - 2 * gap, g.cell[c], g.fill[c], head, g.in, g.progress(), tank);
    }
    drawn_head = g.head;
    // The end of a level: a banner over the board
    if (g.phase == Phase::Passed || g.phase == Phase::Over) {
        char l1[32], l2[48];
        if (g.phase == Phase::Passed) {
            snprintf(l1, sizeof l1, "Level %u Cleared!", unsigned(g.level));
            snprintf(l2, sizeof l2, "%u pipes, score %ld", unsigned(g.pipes), long(g.score));
        } else {
            snprintf(l1, sizeof l1, "Out of Pipe!");
            snprintf(l2, sizeof l2, "%u of %d pipes. Score %ld", unsigned(g.pipes), g.goal(), long(g.score));
        }
        const bool large = metrics().large;
        const lv_font_t* f1 = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const lv_font_t* f2 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
        const int h1 = lv_font_get_line_height(f1), h2 = lv_font_get_line_height(f2);
        const int bw = kCols * cell - 2 * (cell / 2), bh = h1 + h2 + (large ? 16 : 10);
        const int bx1 = x0 + cell / 2, by1 = y0 + (kRows * cell - bh) / 2;
        kit::fill_rect(layer, bx1 - 2, by1 - 2, bx1 + bw + 1, by1 + bh + 1, g.phase == Phase::Passed ? P.lit : P.piece_a, 8);
        kit::fill_rect(layer, bx1, by1, bx1 + bw - 1, by1 + bh - 1, P.cell, 7);
        kit::text(layer, l1, f1, P.ink, bx1, by1 + (large ? 6 : 4), bw, h1);
        kit::text(layer, l2, f2, P.muted, bx1, by1 + (large ? 6 : 4) + h1, bw, h2);
    }
}

void invalidate_square(int c)
{
    if (!area || c < 0 || c >= kCells) return;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int x = a.x1 + bx + (c % kCols) * cell, y = a.y1 + by + (c / kCols) * cell;
    lv_area_t r{x, y, x + cell - 1, y + cell - 1};
    lv_obj_invalidate_area(area, &r);
}

void invalidate_queue()
{
    if (!area) return;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_area_t r{a.x1, a.y1 + qy - 4, a.x2, a.y1 + qy + qs + 4};
    lv_obj_invalidate_area(area, &r);
}

// ---- Game flow -----------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

bool ticking = false;

void set_go_label()
{
    if (!go_l || !S) return;
    const Game& g = S->g;
    const char* t = held ? "Continue"
                  : g.phase == Phase::Waiting ? "Water Now"
                  : g.phase == Phase::Flowing ? (g.fast ? "Flowing Fast" : "Fast Flow")
                  : g.phase == Phase::Passed ? "Next Level" : "Play Again";
    lv_label_set_text(go_l, t);
    const bool lit = held || g.phase == Phase::Passed || g.phase == Phase::Over;
    if (lit) lv_obj_add_state(go_k, LV_STATE_CHECKED); else lv_obj_remove_state(go_k, LV_STATE_CHECKED);
    if (g.phase == Phase::Flowing && g.fast && !held) lv_obj_add_state(go_k, LV_STATE_DISABLED);
    else lv_obj_remove_state(go_k, LV_STATE_DISABLED);
}

void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char l[16], s[48], sh[32];
    snprintf(l, sizeof l, "%ld", long(g.score));
    lv_label_set_text(bar.left, l);
    sh[0] = 0;
    if (held) snprintf(s, sizeof s, "Paused");
    else if (g.phase == Phase::Waiting) {
        last_wait_s = (g.wait_ms + 999) / 1000;
        snprintf(s, sizeof s, "Level %u: water in %lus", unsigned(g.level), (unsigned long)last_wait_s);
        snprintf(sh, sizeof sh, "Water in %lus", (unsigned long)last_wait_s);
    } else if (g.phase == Phase::Flowing) {
        snprintf(s, sizeof s, "Level %u: %u of %d pipes", unsigned(g.level), unsigned(g.pipes), g.goal());
        snprintf(sh, sizeof sh, "%u of %d pipes", unsigned(g.pipes), g.goal());
    } else if (g.phase == Phase::Passed) snprintf(s, sizeof s, "Level %u cleared", unsigned(g.level));
    else snprintf(s, sizeof s, "Final Score %ld", long(g.score));
    kit::top_bar_status(bar, s, sh[0] ? sh : nullptr);
    set_go_label();
}

void new_game()
{
    // A game left before its end is kept in the stats once it got past level 1
    if (!S->recorded && S->g.level >= 2) record();
    *S = State{};
    S->g.start(seed_now());
    held = false;
    save();
    update_status();
    if (area) lv_obj_invalidate(area);
}

void go_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (held) { held = false; last_ms = 0; update_status(); return; }
    if (g.phase == Phase::Over) { new_game(); return; }
    if (g.phase == Phase::Passed) {
        g.next_level();
        save();
        update_status();
        lv_obj_invalidate(area);
        return;
    }
    g.go();
    update_status();
}

void area_tap_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (held) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int x = p.x - a.x1 - bx, y = p.y - a.y1 - by;
    if (x < 0 || y < 0 || x >= kCols * cell || y >= kRows * cell) return;
    const int c = (y / cell) * kCols + x / cell;
    const int r = g.tap(c);
    if (!r) return;
    sound(r == 2 ? Sound::Move : Sound::Place);
    invalidate_square(c);
    invalidate_queue();
    update_status();
    save();
}

// Time passes for the water
void flow(uint32_t now)
{
    Game& g = S->g;
    if (last_ms == 0) { last_ms = now; return; }
    uint32_t dt = now - last_ms;
    last_ms = now;
    if (held || overlay_open() || (g.phase != Phase::Waiting && g.phase != Phase::Flowing)) return;
    if (dt > 200) dt = 200;                                   // a blocked loop isn't played through
    const int before = g.head;
    const uint32_t ev = g.advance(dt);
    if (ev & kEvFlow) sound(Sound::Turn);
    if (g.phase == Phase::Waiting) {
        invalidate_square(g.head);                            // the tank fills
        if ((g.wait_ms + 999) / 1000 != last_wait_s) update_status();
        return;
    }
    if (g.head != before) invalidate_square(before);
    invalidate_square(g.head);
    if (drawn_head >= 0 && drawn_head != g.head) invalidate_square(drawn_head);
    if (ev & (kEvFilled | kEvFlow)) update_status();
    if (ev & kEvPassed) { sound(Sound::Trill); save(); update_status(); lv_obj_invalidate(area); }
    if (ev & kEvOver) {
        if (!S->recorded) record();
        sound(Sound::Lose);
        save();
        update_status();
        lv_obj_invalidate(area);
    }
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); }, 1);
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int top = bar.h, h = m.h - top;
    // Squares: 8 across, and 8 rows + the queue row above the key
    int by_w = (m.w - 2 * pad - 4) / kCols;
    int by_h = (h - kh - 5 * pad - 10) / (kRows + 1);
    cell = by_w < by_h ? by_w : by_h;
    qs = cell * 9 / 10;
    qy = pad + 3;
    const int qw = kQueue * qs + (kQueue - 1) * (qs / 5);
    qx = (m.w - qw) / 2;
    by = qy + qs + pad + 6;
    bx = (m.w - kCols * cell) / 2;

    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, h - kh - pad);
    lv_obj_set_pos(area, 0, top);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, area_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, area_tap_cb, LV_EVENT_CLICKED, nullptr);

    go_k = make_key(scr, m.w - 2 * pad, kh, go_cb, 0);
    go_l = key_label(go_k, "Water Now", menu_font());
    lv_obj_set_pos(go_k, pad, m.h - pad - kh);
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
        overlay_text("No games recorded yet. Every game is listed here when the water runs out "
                     "of pipe, and any game left after level 1.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[12], b[12], c[12], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%ld", long(sum.best));
        snprintf(c, sizeof c, "%ld", long(sum.average()));
        snprintf(d, sizeof d, "%lu", (unsigned long)sum.best_level);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Best", "Average", "Top Level"};
        static const int8_t pct[4] = {22, 24, 26, 28};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[12], s2[12], s3[12], tm[16];
            snprintf(s1, sizeof s1, "%ld", long(r.score));
            snprintf(s2, sizeof s2, "%u", unsigned(r.level));
            snprintf(s3, sizeof s3, "%u", unsigned(r.pipes));
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, s3, tm);
        }
        const char* const head2[4] = {"Recent", "Level", "Pipes", "Time"};
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
void menu_back()       { last_ms = 0; update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Pipe Race", nullptr, h, false);
}

// ---- Registry entry -----------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now()); held = false; }
    else held = S->g.phase == Phase::Waiting || S->g.phase == Phase::Flowing;
    last_ms = 0;
    load_best();
    build();
}

void close()
{
    if (!S) return;
    save();
    bar = kit::TopBar{};
    area = go_k = go_l = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    flow(now);
    const bool running = !held && (S->g.phase == Phase::Waiting || S->g.phase == Phase::Flowing);
    if (clock_.tick(now, running, S->seconds)) { ticking = true; update_status(); ticking = false; }
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) { held = held || S->g.phase == Phase::Flowing; build(); } }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    if (st->g.phase == Phase::Over) snprintf(buf, cap, "Final score %ld", long(st->g.score));
    else snprintf(buf, cap, "Level %u, score %ld", unsigned(st->g.level), long(st->g.score));
    return true;
}

void save_now() { save(); }

// Icon: a bend, a straight and a cross with water running through
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), s = size / 2;
    kit::fill_rect(layer, a.x1, a.y1, a.x1 + size - 1, a.y1 + size - 1, pal().key_border, size / 12);
    draw_square(layer, a.x1, a.y1, s, kStartE, 0, false, 0, 0, 1.0f);
    draw_square(layer, a.x1 + s, a.y1, s, kSW, 1, false, 0, 0, 0);
    draw_square(layer, a.x1 + s, a.y1 + s, s, kWN, 0, true, kN, 0.75f, 0);
    draw_square(layer, a.x1, a.y1 + s, s, kCross, 0, false, 0, 0, 0);
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

// Preview staging (tools/preview): a game as it stands, so renders can show it
namespace piperace_preview {
piperace::Game* game() { return S ? &S->g : nullptr; }
void unhold() { held = false; last_ms = 0; update_status(); if (area) lv_obj_invalidate(area); }
void redraw() { update_status(); if (area) lv_obj_invalidate(area); }
}

namespace games {
extern const GameOps piperace_ops;
const GameOps piperace_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
