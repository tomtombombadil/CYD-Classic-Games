#include "sudoku_screen.h"

#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include "sudoku_app.h"
#include "sudoku_board_view.h"
#include "sudoku_stock.h"
#include "games/common/game_kit.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace sudoku_ui {

using namespace ui;
namespace stats = sudoku::stats;

namespace {

sudoku::Game* G = nullptr;

// Widgets
lv_obj_t* board      = nullptr;
lv_obj_t* clock_l    = nullptr;
lv_obj_t* center_l   = nullptr;      // difficulty, "Solved!" or a hint prompt
lv_obj_t* digit_btn[10] = {};
lv_obj_t* digit_cnt[10] = {};        // "left to place" (large screens only)
lv_obj_t* undo_btn   = nullptr;
lv_obj_t* notes_btn  = nullptr;
lv_obj_t* mode_btn   = nullptr;
lv_obj_t* mode_lbl   = nullptr;
lv_obj_t* hint_btn   = nullptr;

// Input state
int  selected     = -1;
bool notes_mode   = false;
int  brush_digit  = 0;               // Digit 1st mode: the digit being placed
int  hint_cell    = -1;              // cell the last Hint tap pointed at
bool solved_seen  = false;           // solve already celebrated + recorded
bool idle_paused  = false;           // clock stopped: no touches for a while

// The clock stops after this long with no touch, so time the board sits on
// unattended doesn't count. The next touch starts it again.
constexpr uint32_t kIdlePauseMs = 2 * 60 * 1000;

// Timing
bool     dirty = false;
uint32_t dirty_ms = 0, last_sec_ms = 0, last_save_ms = 0, now_cache = 0;

bool large()       { return metrics().large; }
bool digit_first() { return settings().input == InputMode::DigitFirst; }
bool finished()    { return G->solved(); }

void update_status()
{
    if (!clock_l) return;
    char t[16];
    stats::format_time(t, sizeof t, G->elapsed_s());
    lv_label_set_text(clock_l, t);
    if (finished())          lv_label_set_text(center_l, "Solved!");
    else if (idle_paused)    lv_label_set_text(center_l, "Paused");
    else if (hint_cell >= 0) lv_label_set_text(center_l, "Tap Hint to fill");
    else                     lv_label_set_text(center_l, sudoku::difficulty_name(G->difficulty()));
}

void celebrate();

// Push the game state out to every widget.
void update()
{
    if (!board) return;
    const bool done = finished();
    BoardHighlight hl;
    hl.selected = done ? -1 : selected;
    if (!done) {
        if (digit_first() && brush_digit) hl.digit = brush_digit;
        else if (selected >= 0) hl.digit = G->value(selected);
    }
    board_set_highlight(board, hl);

    for (int d = 1; d <= 9; ++d) {
        const bool digit_done = G->placed_correct(d) >= 9;
        set_checked(digit_btn[d], !done && digit_first() && brush_digit == d);
        set_dim(digit_btn[d], digit_done);
        if (digit_cnt[d]) {
            // Left to place = 9 minus how many are on the board now, right or
            // wrong (what the player can count). Blank once the digit is done.
            const int left = 9 - G->count(d);
            if (digit_done) lv_label_set_text(digit_cnt[d], "");
            else            lv_label_set_text_fmt(digit_cnt[d], "%d", left > 0 ? left : 0);
        }
    }
    set_checked(notes_btn, notes_mode && !done);
    lv_label_set_text(mode_lbl, digit_first() ? "Digit 1st" : "Cell 1st");
    set_dim(undo_btn, done || !G->can_undo());
    set_dim(notes_btn, done);
    set_dim(mode_btn, done);
    set_dim(hint_btn, done);
    set_checked(hint_btn, hint_cell >= 0 && !done);
    update_status();

    if (done && !solved_seen) {
        solved_seen = true;
        sudoku_app::save(*G);
        if (!G->replay()) {                       // a solved puzzle replayed isn't a new result
            stats::Record r;
            r.difficulty = static_cast<uint8_t>(G->difficulty());
            r.result = stats::Result::Solved;
            r.seconds = G->elapsed_s();
            r.hints = static_cast<uint8_t>(G->hints_used());
            sudoku_app::record_stat(r);
        }
        sound(Sound::Win);
        celebrate();
    }
}

void changed()
{
    dirty = true;
    dirty_ms = now_cache;
}

// ---- Celebration --------------------------------------------------------------
// The panel's colour inversion is toggled a few times (about 1.2 s): an
// instant whole-screen flash with no redrawing. The solved board then stays
// on screen; nothing else opens.
lv_timer_t* flash_timer = nullptr;
int         flash_step  = 0;
constexpr int kFlashToggles = 6;              // on/off x3
constexpr uint32_t kFlashMs = 200;

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

void celebrate()
{
    if (flash_timer || !shell().flash_invert) return;
    flash_step = 0;
    shell().flash_invert(true);
    flash_timer = lv_timer_create(flash_cb, kFlashMs, nullptr);
}

void stop_flash()
{
    if (!flash_timer) return;
    lv_timer_delete(flash_timer);
    flash_timer = nullptr;
    if (shell().flash_invert) shell().flash_invert(false);
}

// A digit put down clicks; one that clashes buzzes. The win sound (from
// update()) replaces either when that digit finishes the puzzle.
void entry_sound(int cell)
{
    if (G->value(cell) && G->conflict(cell)) sound(Sound::Error);
    else                                      sound(Sound::Place);
}

// ---- Input ------------------------------------------------------------------
// Cell 1st:  tap a cell, then a digit. The same digit again clears it.
// Digit 1st: tap a digit, then cells. Tapping a cell that already holds
//            that digit clears it; another digit is replaced. Picking a
//            digit clears the cell highlight.
// Notes mode applies to both: digits toggle pencil marks instead.
void on_cell(int i)
{
    if (overlay_open() || finished()) return;
    hint_cell = -1;
    if (digit_first() && G->locked(i)) {
        // Tapping a fixed digit (given or hinted) picks that digit, the same
        // as tapping it in the digit row.
        brush_digit = G->value(i);
        selected = -1;
        update();
        return;
    }
    selected = i;
    if (digit_first() && brush_digit && G->enter(i, brush_digit, notes_mode)) {
        changed();
        entry_sound(i);
        // That was the last one: nothing left to place, so put the digit
        // down (its button greys out straight away).
        if (G->placed_correct(brush_digit) >= 9) brush_digit = 0;
    }
    update();
}

void on_digit(int d)
{
    if (finished()) return;
    hint_cell = -1;
    if (digit_first()) {
        brush_digit = (brush_digit == d) ? 0 : d;
        selected = -1;
    } else if (selected >= 0 && G->enter(selected, d, notes_mode)) {
        changed();
        entry_sound(selected);
    }
    update();
}

// First tap: point at the cell (selected + highlighted, top bar prompts).
// Second tap on the same cell: fill it with the correct digit.
void on_hint()
{
    if (finished()) return;
    if (hint_cell >= 0 && hint_cell == selected && G->hint_target(selected) == hint_cell) {
        if (G->apply_hint(hint_cell)) { changed(); sound(Sound::Hint); }
        hint_cell = -1;
    } else {
        const int t = G->hint_target(selected);
        if (t < 0) return;
        selected = t;
        hint_cell = t;
        brush_digit = 0;
    }
    update();
}

void toggle_input_mode()
{
    UiSettings& S = settings();
    S.input = digit_first() ? InputMode::CellFirst : InputMode::DigitFirst;
    brush_digit = 0;
    save_settings();
}

void digit_cb(lv_event_t* e) { on_digit(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)))); }

void tool_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (id == 4) { open_menu(); return; }
    if (finished()) return;
    if (id != 3) hint_cell = -1;
    switch (id) {
        case 0: if (G->undo()) changed(); break;
        case 1: notes_mode = !notes_mode; break;
        case 2: toggle_input_mode(); break;
        case 3: on_hint(); return;
    }
    update();
}

// ---- Menu -------------------------------------------------------------------
// A game counts as played once it has moves or 30 s on the clock; leaving
// a played, unsolved game for a new one is recorded as "Gave up".
bool game_in_progress()
{
    return G->active() && !G->solved() && !G->replay() && (G->can_undo() || G->elapsed_s() >= 30);
}

void start_new(sudoku::Difficulty d)
{
    // Leaving an unfinished puzzle that was actually played counts as giving up
    if (game_in_progress()) {
        stats::Record r;
        r.difficulty = static_cast<uint8_t>(G->difficulty());
        r.result = stats::Result::GaveUp;
        r.seconds = G->elapsed_s();
        r.hints = static_cast<uint8_t>(G->hints_used());
        sudoku_app::record_stat(r);
    }

    static sudoku::Grid puzzle, solution;
    if (sudoku_stock_take(d, puzzle, solution)) {
        G->start_with(d, puzzle, solution);           // ready-made: instant
    } else {
        // None ready: make one now. Hard/Expert can take a few seconds on
        // the ESP32, so say so before the work starts.
        overlay_begin("New game");
        char msg[48];
        snprintf(msg, sizeof msg, "Creating a %s puzzle...", sudoku::difficulty_name(d));
        overlay_text(msg, true);
        lv_refr_now(nullptr);
        sudoku::Rng rng(shell().random_seed ? shell().random_seed() : 1);
        G->start(d, rng);
    }
    stop_flash();
    selected = -1;
    brush_digit = 0;
    hint_cell = -1;
    notes_mode = false;
    solved_seen = false;
    close_overlays();
    sudoku_app::save(*G);
    last_save_ms = now_cache;
    dirty = false;
    update();
}

void new_game_cb(lv_event_t* e)
{
    start_new(static_cast<sudoku::Difficulty>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

enum MenuAction : intptr_t { kRestart, kSettings, kExitMenu, kStats, kExitGame, kHowToPlay };

void back_to_menu() { open_menu(); }

void menu_cb(lv_event_t* e)
{
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case kRestart:
            G->restart();
            stop_flash();
            selected = -1;
            hint_cell = -1;
            notes_mode = false;
            solved_seen = false;
            close_overlays();
            changed();
            update();
            break;
        case kSettings:   settings_open(back_to_menu); break;
        case kHowToPlay:  kit::how_to_play(back_to_menu); break;
        case kExitMenu:   close_overlays(); update(); break;
        case kStats:      open_stats(); break;
        case kExitGame:   app_go_home(); break;
    }
}

// ---- Stats screen -------------------------------------------------------------
void stats_back_cb(lv_event_t*) { open_menu(); }

// Delete last / Clear all act immediately, then the screen redraws.
void stats_action_cb(lv_event_t* e)
{
    const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const Shell& H = shell();
    if (which == 1 && H.stats_delete_last) H.stats_delete_last(sudoku_app::kId);
    if (which == 2 && H.stats_clear) H.stats_clear(sudoku_app::kId);
    open_stats();
}

// ---- Layout -------------------------------------------------------------------
// Top to bottom: top bar (clock, difficulty, menu), board, tool row (Undo,
// Notes, input mode, Hint), digit row. Sizes come from the screen resolution:
// the board gets the largest cell that leaves the controls their minimum
// height, then any spare height goes back to the controls.
void build_layout()
{
    metrics_update();
    const Palette& P = pal();
    const int scr_w = metrics().w, scr_h = metrics().h;
    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, P.screen, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    // Small screens drop the digit counters so the board can be bigger
    const bool counters = large();
    const int m = large() ? 2 : 1;        // outer margin
    const int g = large() ? 4 : 2;        // gap between rows
    int top_h  = large() ? 34 : 23;
    int tool_h = large() ? 40 : 28;
    int key_h  = large() ? 54 : 34;

    const int max_cell_w = (scr_w - 2 * m - 2) / 9;
    const int avail = scr_h - (top_h + tool_h + key_h + 3 * g + 2 * m);
    int cell = (avail - 2) / 9;
    if (cell > max_cell_w) cell = max_cell_w;
    const int board_px = 9 * cell + 2;
    int spare = avail - board_px;
    auto grow = [&spare](int& v, int cap) { const int add = (cap - v) < spare ? (cap - v) : spare; if (add > 0) { v += add; spare -= add; } };
    grow(key_h, 64);
    grow(tool_h, 48);
    grow(top_h, 40);
    const int extra_gap = spare / 4;      // whatever is left: a little air

    // Top bar
    int y = m;
    const lv_font_t* bf = bar_font();
    const int ty = y + (top_h - lv_font_get_line_height(bf)) / 2;
    clock_l = lv_label_create(scr);
    lv_obj_set_style_text_font(clock_l, bf, 0);
    lv_obj_set_style_text_color(clock_l, P.muted, 0);
    lv_obj_set_pos(clock_l, m + 6, ty);
    center_l = lv_label_create(scr);
    lv_obj_set_style_text_font(center_l, bf, 0);
    lv_obj_set_style_text_color(center_l, P.ink, 0);
    lv_obj_align(center_l, LV_ALIGN_TOP_MID, 0, ty);
    const int hb_w = top_h * 3 / 2;
    lv_obj_t* hb = make_hamburger(scr, hb_w, top_h, tool_cb, 4);
    lv_obj_set_pos(hb, scr_w - m - hb_w, y);
    y += top_h + g + extra_gap;

    // Board
    board = board_create(scr, G, cell, on_cell);
    lv_obj_set_pos(board, (scr_w - board_px) / 2, y);
    y += board_px + g + extra_gap;

    // Tool row: Undo | Notes | Cell 1st/Digit 1st | Hint, widths from the
    // labels. Uses the large font only if all four fit with some padding.
    const int row_w = scr_w - 2 * m;
    const int tg = large() ? 5 : 3;
    const char* names[4] = {"Undo", "Notes", "Digit 1st", "Hint"};   // [2] = widest mode label
    const lv_font_t* tf = large() ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    int nat[4], total = 0;
    for (int k = 0; k < 4; ++k) { nat[k] = text_width(names[k], tf); total += nat[k]; }
    if (total + 4 * 12 + 3 * tg > row_w && tf != &lv_font_montserrat_14) {
        tf = &lv_font_montserrat_14;
        total = 0;
        for (int k = 0; k < 4; ++k) { nat[k] = text_width(names[k], tf); total += nat[k]; }
    }
    const int pad_each = (row_w - total - 3 * tg) / 4;   // spread spare width evenly
    lv_obj_t* tb[4];
    int x = m;
    for (int k = 0; k < 4; ++k) {
        const int w = (k == 3) ? (m + row_w - x) : nat[k] + pad_each;
        tb[k] = make_key(scr, w, tool_h, tool_cb, k);
        lv_obj_set_pos(tb[k], x, y);
        lv_obj_t* l = key_label(tb[k], names[k], tf);
        if (k == 2) mode_lbl = l;
        x += w + tg;
    }
    undo_btn = tb[0]; notes_btn = tb[1]; mode_btn = tb[2]; hint_btn = tb[3];
    y += tool_h + g + extra_gap;

    // Digit row (with "left to place" counts on large screens)
    const int dgap = large() ? 4 : 2;
    const int key_w = (scr_w - 2 * m - 8 * dgap) / 9;
    const int keys_x = (scr_w - (9 * key_w + 8 * dgap)) / 2;
    const lv_font_t* kf = key_h >= 56 ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    const lv_font_t* cf = &lv_font_montserrat_12;
    const int cnt_h = counters ? lv_font_get_line_height(cf) : 0;
    char s[2] = {0, 0};
    for (int d = 1; d <= 9; ++d) {
        lv_obj_t* k = make_key(scr, key_w, key_h, digit_cb, d);
        lv_obj_set_pos(k, keys_x + (d - 1) * (key_w + dgap), y);
        s[0] = '0' + d;
        lv_obj_t* l = key_label(k, s, kf);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -cnt_h / 2);
        digit_btn[d] = k;
        digit_cnt[d] = nullptr;
        if (counters) {
            digit_cnt[d] = lv_label_create(k);
            lv_obj_set_style_text_font(digit_cnt[d], cf, 0);
            lv_obj_set_style_text_opa(digit_cnt[d], LV_OPA_80, 0);
            lv_obj_align(digit_cnt[d], LV_ALIGN_BOTTOM_MID, 0, -3);
        }
    }
}

void forget_widgets()
{
    board = clock_l = center_l = undo_btn = notes_btn = mode_btn = mode_lbl = hint_btn = nullptr;
    for (int d = 0; d < 10; ++d) digit_btn[d] = digit_cnt[d] = nullptr;
}

} // namespace

// ---- Public -------------------------------------------------------------------
void screen_create(sudoku::Game& g)
{
    G = &g;
    selected = -1;
    notes_mode = false;
    brush_digit = 0;
    hint_cell = -1;
    idle_paused = false;
    dirty = false;
    last_sec_ms = 0;
    last_save_ms = now_cache;
    solved_seen = g.solved();      // a finished saved game isn't celebrated again
    build_layout();
    update();
}

void screen_destroy()
{
    stop_flash();
    lv_obj_clean(lv_screen_active());
    forget_widgets();
    G = nullptr;
}

void screen_restyle()
{
    if (!G) return;
    build_layout();
    update();
}

void screen_tick(uint32_t now_ms)
{
    now_cache = now_ms;
    if (!G) return;
    // Idle pause: LVGL tracks the time since the last touch
    const bool idle = lv_display_get_inactive_time(lv_display_get_default()) >= kIdlePauseMs;
    if (idle != idle_paused) {
        idle_paused = idle;
        update_status();
    }
    // If the loop was blocked (puzzle generation, SD writes), don't bill the
    // player for that time: drop the backlog instead of catching up.
    if (last_sec_ms == 0 || now_ms - last_sec_ms > 3000) last_sec_ms = now_ms;
    if (now_ms - last_sec_ms >= 1000) {
        last_sec_ms += 1000;
        if (!overlay_open() && !idle_paused && G->active() && !G->solved()) {
            G->add_second();
            update_status();
        }
    }
    // Save 1.5 s after the last move, and every 30 s of play for the clock.
    const bool due = (dirty && now_ms - dirty_ms >= 1500)
                  || (now_ms - last_save_ms >= 30000 && !idle_paused && G->active() && !G->solved());
    if (due) {
        sudoku_app::save(*G);
        dirty = false;
        last_save_ms = now_ms;
    }
}

void tap_cell(int idx)            { on_cell(idx); }
void tap_digit(int d)             { on_digit(d); }
void set_notes(bool on)           { notes_mode = on; update(); }
void menu_tap_new_game(int d)     { start_new(static_cast<sudoku::Difficulty>(d)); }
void hint()                       { on_hint(); }
void set_input_mode(InputMode m)
{
    if (settings().input != m) toggle_input_mode();
    update();
}

void open_menu()
{
    overlay_begin("Sudoku");
    overlay_text("Start a New Game:", false);

    lv_obj_t* grid = lv_obj_create(overlay());
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, 6, 0);
    lv_obj_set_style_pad_row(grid, 6, 0);
    lv_obj_set_scrollable(grid, false);
    const int half = (metrics().w - 2 * (large() ? 16 : 10) - 6) / 2;
    for (int d = 0; d < 4; ++d) {
        lv_obj_t* b = make_key(grid, half, menu_btn_h(), new_game_cb, d);
        key_label(b, sudoku::difficulty_name(static_cast<sudoku::Difficulty>(d)), menu_font());
    }
    overlay_button(overlay(), "Restart This Puzzle", menu_cb, kRestart);
    kit::how_to_play_key(menu_cb, kHowToPlay);
    overlay_pair("Stats", menu_cb, kStats, "Settings", menu_cb, kSettings);
    overlay_exit_row(menu_cb, kExitMenu, kExitGame);
}

void open_stats()
{
    overlay_begin("Stats");
    static stats::Summary sum;               // ~200 bytes; static keeps it off the stack
    const bool ok = sudoku_app::load_stats(sum);
    const lv_font_t* tf = large() ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const lv_font_t* rf = &lv_font_montserrat_14;
    static const int8_t pct_large[4] = {34, 22, 24, 20};
    static const int8_t pct_small[4] = {30, 29, 24, 17};
    const int8_t* pct = large() ? pct_large : pct_small;

    if (!ok || sum.total == 0) {
        overlay_text("No games recorded yet. Solved puzzles, and puzzles you leave "
                     "for a new game, are listed here.", false);
    } else {
        Table& levels = scratch_table(0);
        Table& recent = scratch_table(1);
        table_clear(levels);
        table_clear(recent);
        for (int d = 0; d < 4; ++d) {
            const stats::PerDifficulty& p = sum.level[d];
            char n[12], avg[16] = "-", best[16] = "-";
            snprintf(n, sizeof n, "%lu", (unsigned long)p.solved);
            if (p.solved) {
                stats::format_time(avg, sizeof avg, sum.average_s(d));
                stats::format_time(best, sizeof best, p.best_s);
            }
            table_add(levels, sudoku::difficulty_name(static_cast<sudoku::Difficulty>(d)), n, avg, best);
        }
        const char* const head1[4] = {"Level", "Solved", large() ? "Average" : "Avg", "Best"};
        table_show(levels, head1, pct, rf, tf);

        const int max_rows = large() ? 6 : 4;   // leaves room for the action buttons
        const int show = sum.recent_n < max_rows ? sum.recent_n : max_rows;
        for (int i = 0; i < show; ++i) {
            const stats::Record& r = sum.newest(i);
            char t[16], h[8] = "";
            stats::format_time(t, sizeof t, r.seconds);
            if (r.hints) snprintf(h, sizeof h, "%u", (unsigned)r.hints);
            table_add(recent, sudoku::difficulty_name(static_cast<sudoku::Difficulty>(r.difficulty & 3)),
                      r.result == stats::Result::Solved ? "Solved" : "Gave Up", t, h);   // CSV keeps "Gave up"
        }
        const char* const head2[4] = {"Recent", "", "Time", "Hints"};
        table_show(recent, head2, pct, rf, rf);
    }
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.",
             shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);

    // Bottom: [Delete last | Clear all] above Back
    const int bh = menu_btn_h(), gap = large() ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && sum.total > 0 && shell().stats_delete_last && shell().stats_clear) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), bh);
        lv_obj_set_ignore_layout(row, true);
        lv_obj_align_to(row, back, LV_ALIGN_OUT_TOP_MID, 0, -gap);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_clickable(row, false);
        lv_obj_set_scrollable(row, false);
        const char* labels[2] = {"Delete Last", "Clear All"};
        for (int k = 0; k < 2; ++k) {
            lv_obj_t* b = make_key(row, 10, bh, stats_action_cb, k + 1);
            lv_obj_set_flex_grow(b, 1);
            key_label(b, labels[k], menu_font());
        }
    }
}

} // namespace sudoku_ui
