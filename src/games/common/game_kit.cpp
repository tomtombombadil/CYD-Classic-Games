#include "game_kit.h"

#include <cstdio>
#include <cstring>
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace kit {

using namespace ui;

namespace {

constexpr uint32_t kIdlePauseMs = 2 * 60 * 1000;

lv_timer_t* flash_timer = nullptr;
int         flash_step = 0;
constexpr int      kFlashToggles = 6;
constexpr uint32_t kFlashMs = 200;

MenuHandlers handlers{};

void flash_cb(lv_timer_t*)
{
    ++flash_step;
    const bool on = (flash_step < kFlashToggles) && (flash_step % 2 == 0);
    if (shell().flash_invert) shell().flash_invert(on);
    if (flash_step >= kFlashToggles) {
        lv_timer_delete(flash_timer);
        flash_timer = nullptr;
    }
}

void menu_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    switch (id) {
        case kStats:     if (handlers.stats) handlers.stats(); break;
        case kExitGame:  app_go_home(); break;
        case kSettings:  settings_open(handlers.reopen); break;
        case kExitMenu:  close_overlays(); if (handlers.back) handlers.back(); break;
        case kWireless:  break;                       // stage 5 (multiplayer)
        default:
            close_overlays();
            if (handlers.pick) handlers.pick(static_cast<int>(id));
            break;
    }
}

lv_obj_t* row(int h)
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

lv_obj_t* row_key(lv_obj_t* r, const char* text, intptr_t id)
{
    lv_obj_t* b = make_key(r, 10, menu_btn_h(), menu_cb, id);
    lv_obj_set_flex_grow(b, 1);
    key_label(b, text, menu_font());
    return b;
}

void menu_tail()
{
    lv_obj_t* r = row(menu_btn_h());
    row_key(r, "Stats", kStats);
    row_key(r, "Settings", kSettings);
    overlay_exit_row(menu_cb, kExitMenu, kExitGame);
}

// Delete last / Clear all on stats screens act at once, then redraw
const char* stats_id = nullptr;
void (*stats_back)() = nullptr;
void (*stats_reopen)() = nullptr;
twoplayer::Sides stats_sides{};
const char* const* stats_levels = nullptr;

void stats_back_cb(lv_event_t*) { if (stats_back) stats_back(); }

void stats_action_cb(lv_event_t* e)
{
    const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const Shell& H = shell();
    if (which == 1 && H.stats_delete_last) H.stats_delete_last(stats_id);
    if (which == 2 && H.stats_clear) H.stats_clear(stats_id);
    if (stats_reopen) stats_reopen();
}

void stats_bottom(bool any)
{
    const int bh = menu_btn_h(), gap = metrics().large ? 10 : 6;
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.",
             shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (!any || !shell().stats_delete_last || !shell().stats_clear) return;
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), bh);
    lv_obj_set_ignore_layout(r, true);
    lv_obj_align_to(r, back, LV_ALIGN_OUT_TOP_MID, 0, -gap);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    const char* labels[2] = {"Delete Last", "Clear All"};
    for (int k = 0; k < 2; ++k) {
        lv_obj_t* b = make_key(r, 10, bh, stats_action_cb, k + 1);
        lv_obj_set_flex_grow(b, 1);
        key_label(b, labels[k], menu_font());
    }
}

void tp_line(const char* line, void* ctx)
{
    twoplayer::Record r;
    if (twoplayer::parse_line(line, r, stats_sides)) static_cast<twoplayer::Summary*>(ctx)->add(r);
}

void pz_line(const char* line, void* ctx)
{
    puzzle::Record r;
    if (puzzle::parse_line(line, r, stats_levels)) static_cast<puzzle::Summary*>(ctx)->add(r);
}

void reopen_two_player() { stats_two_player(stats_id, stats_sides, stats_back); }
void reopen_solo()       { stats_solo(stats_id, stats_levels, stats_back); }

} // namespace

// ---- Top bar ----------------------------------------------------------------------
lv_obj_t* screen_begin()
{
    metrics_update();
    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, pal().screen, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
    return scr;
}

TopBar top_bar(lv_event_cb_t menu_cb)
{
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    TopBar t;
    const int margin = m.large ? 2 : 1;
    const int bh = m.large ? 36 : 26;
    const lv_font_t* f = bar_font();
    const int ty = margin + (bh - lv_font_get_line_height(f)) / 2;
    t.left = lv_label_create(scr);
    lv_obj_set_style_text_font(t.left, f, 0);
    lv_obj_set_style_text_color(t.left, pal().muted, 0);
    lv_obj_set_pos(t.left, margin + 6, ty);
    lv_label_set_text(t.left, "");
    t.center = lv_label_create(scr);
    lv_obj_set_style_text_font(t.center, f, 0);
    lv_obj_set_style_text_color(t.center, pal().ink, 0);
    lv_obj_align(t.center, LV_ALIGN_TOP_MID, 0, ty);
    lv_label_set_text(t.center, "");
    const int hb_w = bh * 3 / 2;
    lv_obj_t* hb = make_hamburger(scr, hb_w, bh, menu_cb, 0);
    lv_obj_set_pos(hb, m.w - margin - hb_w, margin);
    t.h = margin + bh + (m.large ? 4 : 2);
    t.text_y = ty;
    return t;
}

void top_bar_status(const TopBar& t, const char* text)
{
    if (!t.center) return;
    lv_label_set_text(t.center, text);
    lv_obj_align(t.center, LV_ALIGN_TOP_MID, 0, t.text_y);
}

// ---- Clock ----------------------------------------------------------------------------
bool Clock::tick(uint32_t now_ms, bool running, uint32_t& seconds)
{
    bool changed = false;
    const bool idle = lv_display_get_inactive_time(lv_display_get_default()) >= kIdlePauseMs;
    if (idle != paused) { paused = idle; changed = true; }
    // A blocked loop (saving, AI hand-over) isn't billed: drop the backlog
    if (last_ms == 0 || now_ms - last_ms > 3000) last_ms = now_ms;
    if (now_ms - last_ms >= 1000) {
        last_ms += 1000;
        if (running && !paused && !overlay_open()) { ++seconds; changed = true; }
    }
    return changed;
}

// ---- Win flash ----------------------------------------------------------------------
void flash()
{
    if (flash_timer || !shell().flash_invert) return;
    flash_step = 0;
    shell().flash_invert(true);
    flash_timer = lv_timer_create(flash_cb, kFlashMs, nullptr);
}

void flash_stop()
{
    if (!flash_timer) return;
    lv_timer_delete(flash_timer);
    flash_timer = nullptr;
    if (shell().flash_invert) shell().flash_invert(false);
}

// ---- Menus -----------------------------------------------------------------------------
void menu_two_player(const char* title, const MenuHandlers& h)
{
    handlers = h;
    overlay_begin(title);
    overlay_text("New Game vs Computer:", false);
    lv_obj_t* r1 = row(menu_btn_h());
    for (int l = 0; l < twoplayer::kLevels; ++l)
        row_key(r1, twoplayer::level_name(static_cast<twoplayer::Level>(l)), l);
    lv_obj_t* r2 = row(menu_btn_h());
    row_key(r2, "Pass and Play", kPassAndPlay);
    set_dim(row_key(r2, "Wireless", kWireless), true);   // CYD to CYD comes in stage 5
    menu_tail();
}

void menu_solo(const char* title, const char* const levels[3], const MenuHandlers& h, bool restart)
{
    handlers = h;
    overlay_begin(title);
    if (levels) {
        overlay_text("New Game:", false);
        lv_obj_t* r1 = row(menu_btn_h());
        for (int l = 0; l < 3; ++l) row_key(r1, levels[l], l);
    } else {
        overlay_button(overlay(), "New Game", menu_cb, kLevel0);
    }
    if (restart) overlay_button(overlay(), "Restart This Game", menu_cb, kRestart);
    menu_tail();
}

// ---- Stats ---------------------------------------------------------------------------------
void stats_two_player(const char* game_id, const twoplayer::Sides& sides, void (*back)())
{
    stats_id = game_id;
    stats_sides = sides;
    stats_back = back;
    stats_reopen = reopen_two_player;
    overlay_begin("Stats");
    static twoplayer::Summary sum;
    sum = twoplayer::Summary{};
    const Shell& H = shell();
    const bool ok = H.stats_read && H.stats_read(game_id, tp_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum.total == 0) {
        overlay_text("No games recorded yet. Every finished game is listed here.", false);
    } else {
        static Table t;
        table_clear(t);
        char a[12], b[12], c[12];
        for (int l = 0; l < twoplayer::kLevels; ++l) {
            snprintf(a, sizeof a, "%lu", (unsigned long)sum.won[l]);
            snprintf(b, sizeof b, "%lu", (unsigned long)sum.lost[l]);
            snprintf(c, sizeof c, "%lu", (unsigned long)sum.drawn[l]);
            table_add(t, twoplayer::level_name(static_cast<twoplayer::Level>(l)), a, b, c);
        }
        const char* const head[4] = {"Vs Computer", "Won", "Lost", "Draw"};
        static const int8_t pct[4] = {46, 18, 18, 18};
        table_show(t, head, pct, hf, bf);

        static Table p;
        table_clear(p);
        char w1[24], w2[24];
        snprintf(w1, sizeof w1, "%s Won", sides.side1);
        snprintf(w2, sizeof w2, "%s Won", sides.side2);
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.side1);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.side2);
        snprintf(c, sizeof c, "%lu", (unsigned long)sum.draws);
        table_add(p, w1, a, "", "");
        table_add(p, w2, b, "", "");
        table_add(p, "Draw", c, "", "");
        const char* const head2[4] = {"Pass and Play", "Games", "", ""};
        static const int8_t pct2[4] = {46, 30, 12, 12};
        table_show(p, head2, pct2, hf, bf);
    }
    stats_bottom(ok && sum.total > 0);
}

void stats_solo(const char* game_id, const char* const levels[3], void (*back)())
{
    stats_id = game_id;
    stats_levels = levels;
    stats_back = back;
    stats_reopen = reopen_solo;
    overlay_begin("Stats");
    static puzzle::Summary sum;
    sum = puzzle::Summary{};
    const Shell& H = shell();
    const bool ok = H.stats_read && H.stats_read(game_id, pz_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    static const int8_t pct[4] = {24, 24, 28, 24};
    static const int8_t pct_recent[4] = {27, 23, 22, 28};
    if (!ok || sum.total == 0) {
        overlay_text("No games recorded yet. Solved games, and games you leave "
                     "for a new one, are listed here.", false);
    } else {
        static Table t;
        table_clear(t);
        for (int l = 0; l < puzzle::kLevels; ++l) {
            char n[12], bt[16] = "-", bm[12] = "-";
            snprintf(n, sizeof n, "%lu", (unsigned long)sum.solved[l]);
            if (sum.best_s[l]) twoplayer::format_time(bt, sizeof bt, sum.best_s[l]);
            if (sum.best_moves[l]) snprintf(bm, sizeof bm, "%lu", (unsigned long)sum.best_moves[l]);
            table_add(t, levels[l], n, bt, bm);
        }
        const char* const head[4] = {"Level", "Solved", "Best", "Moves"};
        table_show(t, head, pct, hf, bf);

        static Table rt;
        table_clear(rt);
        const int show = sum.recent_n < (large ? 6 : 4) ? sum.recent_n : (large ? 6 : 4);
        for (int i = 0; i < show; ++i) {
            const puzzle::Record& r = sum.newest(i);
            char tm[16], mv[12];
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            snprintf(mv, sizeof mv, "%u", (unsigned)r.moves);
            table_add(rt, levels[r.level < 3 ? r.level : 0], tm, mv, r.solved ? "Solved" : "Gave Up");
        }
        const char* const head2[4] = {"Recent", "Time", "Moves", ""};
        table_show(rt, head2, pct_recent, hf, hf);
    }
    stats_bottom(ok && sum.total > 0);
}

void record_two_player(const char* game_id, const twoplayer::Record& r, const twoplayer::Sides& s)
{
    char body[96];
    if (shell().stats_append && twoplayer::format_body(body, sizeof body, r, s))
        shell().stats_append(game_id, twoplayer::kCsvHeader, body);
}

void record_solo(const char* game_id, const puzzle::Record& r, const char* const levels[3])
{
    char body[96];
    if (shell().stats_append && puzzle::format_body(body, sizeof body, r, levels))
        shell().stats_append(game_id, puzzle::kCsvHeader, body);
}

// ---- Drawing helpers ------------------------------------------------------------------------
void fill_rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, int32_t radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a{x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

void fill_circle(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r, lv_color_t c)
{
    fill_rect(layer, cx - r, cy - r, cx + r, cy + r, c, LV_RADIUS_CIRCLE);
}

void ring(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t r, int32_t width, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_width = width;
    d.border_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a{cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &d, &a);
}

void line(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t width, lv_color_t c)
{
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = c;
    d.width = width;
    d.round_start = 1;
    d.round_end = 1;
    d.p1.x = x1; d.p1.y = y1;
    d.p2.x = x2; d.p2.y = y2;
    lv_draw_line(layer, &d);
}

void text(lv_layer_t* layer, const char* s, const lv_font_t* font, lv_color_t c,
          int32_t x1, int32_t y1, int32_t w, int32_t h)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = font;
    d.color = c;
    d.align = LV_TEXT_ALIGN_CENTER;
    const int32_t lh = lv_font_get_line_height(font);
    const int32_t top = y1 + (h - lh) / 2;
    lv_area_t a{x1, top, x1 + w - 1, top + lh - 1};
    lv_draw_label(layer, &d, &a);
}

} // namespace kit
