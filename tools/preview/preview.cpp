// PC preview: runs the real app UI (LVGL + src/ui + src/games) into an
// in-memory framebuffer and writes screenshots as .ppm files, with the LVGL
// memory use of each screen on stderr.
// Build: see tools/preview/build.sh (used by Claude / CI, not needed on Windows).
//
//   preview <w> <h> <out prefix>          picker + Sudoku screens
//   preview_paging <w> <h> <out prefix>   picker pages with a long fake list
#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include "games/common/match.h"
#include "games/common/puzzle_stats.h"
#include "games/common/two_player.h"
#include "games/lightswitch/lightswitch_core.h"
#include "games/registry.h"
#include "games/checkers/checkers_core.h"
#include "games/chess/chess_core.h"
#include "games/reversi/reversi_core.h"
#include "games/sliding/sliding_core.h"
#include "games/sudoku/sudoku_game.h"
#include "games/sudoku/sudoku_screen.h"
#include "games/sudoku/sudoku_stats.h"
#include "games/common/board8.h"
#include "games/common/game_kit.h"
#include "ui/shell.h"
#include "ui/widgets.h"

static uint32_t fake_ms = 0;
static uint32_t tick() { return fake_ms; }
static void flush(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }

static std::vector<uint16_t> fb;
static int W, H;
static unsigned peak_used = 0;

// A fake stylus for staging taps
static bool    touch_down = false;
static int16_t touch_x = 0, touch_y = 0;
static void touch_read(lv_indev_t*, lv_indev_data_t* d)
{
    d->point.x = touch_x;
    d->point.y = touch_y;
    d->state = touch_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run(int ms)
{
    for (int t = 0; t < ms; t += 10) { fake_ms += 10; lv_timer_handler(); ui::app_tick(fake_ms); }
}

static void shot(const std::string& path)
{
    run(50);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const unsigned used = mon.total_size - mon.free_size;
    if (used > peak_used) peak_used = used;
    fprintf(stderr, "%-34s LVGL heap used %3u%% (%6u B), biggest free %6u B\n", path.c_str(),
            (unsigned)mon.used_pct, used, (unsigned)mon.free_biggest_size);
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
    FILE* f = fopen(path.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) {
        const uint16_t p = fb[i];
        const uint8_t rgb[3] = {uint8_t((p >> 11) << 3), uint8_t(((p >> 5) & 63) << 2), uint8_t((p & 31) << 3)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

// ---- Fake device services --------------------------------------------------------
static std::map<std::string, std::vector<uint8_t>> files;    // game saves
static uint32_t seed() { return 4242; }

static size_t load_game(const char* id, uint8_t* buf, size_t cap)
{
    auto it = files.find(id);
    if (it == files.end()) return 0;
    const size_t n = it->second.size() < cap ? it->second.size() : cap;
    memcpy(buf, it->second.data(), n);
    return n;
}
static void save_game(const char* id, const uint8_t* buf, size_t len) { files[id].assign(buf, buf + len); }

// Made-up history for the stats screens of the newer games
static bool other_stats(const char* id, void (*fn)(const char*, void*), void* ctx)
{
    char line[120], body[96];
    uint32_t seq = 0;
    if (!strcmp(id, "fourconnect") || !strcmp(id, "tictactoe")) {
        const twoplayer::Sides sides = !strcmp(id, "tictactoe") ? twoplayer::Sides{"X", "O"}
                                                                : twoplayer::Sides{"Red", "Yellow"};
        fn(twoplayer::kCsvHeader, ctx);
        const int data[][4] = {{0,0,0,19},{0,1,0,23},{0,1,1,31},{0,2,1,27},{0,2,2,42},{1,0,0,30},{1,0,1,25},{1,0,2,42}};
        for (auto& d : data) {
            twoplayer::Record r;
            r.mode = static_cast<twoplayer::Mode>(d[0]);
            r.level = static_cast<twoplayer::Level>(d[1]);
            r.result = static_cast<twoplayer::Result>(d[2]);
            r.moves = static_cast<uint16_t>(d[3]);
            r.seconds = 60 + 7 * d[3];
            twoplayer::format_body(body, sizeof body, r, sides);
            snprintf(line, sizeof line, "%lu,%s", (unsigned long)++seq, body);
            fn(line, ctx);
        }
        return true;
    }
    const char* const lv_slide[3] = {"3x3", "4x4", "5x5"};
    const char* const lv_light[3] = {"Easy", "Medium", "Hard"};
    const char* const* names = !strcmp(id, "sliding") ? lv_slide : lv_light;
    fn(puzzle::kCsvHeader, ctx);
    const int data[][4] = {{0,1,24,41},{1,1,131,212},{1,0,40,95},{1,1,98,170},{2,1,402,733},{0,1,19,30}};
    for (auto& d : data) {
        puzzle::Record r;
        r.level = static_cast<uint8_t>(d[0]);
        r.solved = d[1];
        r.moves = static_cast<uint16_t>(d[2]);
        r.seconds = static_cast<uint32_t>(d[3]);
        puzzle::format_body(body, sizeof body, r, names);
        snprintf(line, sizeof line, "%lu,%s", (unsigned long)++seq, body);
        fn(line, ctx);
    }
    return true;
}

static bool stats_read(const char* id, void (*fn)(const char*, void*), void* ctx)
{
    if (strcmp(id, "sudoku") != 0) return other_stats(id, fn, ctx);
    using namespace sudoku::stats;
    const uint32_t data[][4] = {   // difficulty, result, seconds, hints
        {0,0,301,0},{0,0,275,0},{1,0,512,0},{1,1,840,1},{1,0,468,2},{2,0,1104,0},
        {0,0,250,0},{2,1,1500,0},{1,0,455,0},{3,0,1922,3},{1,0,430,0}};
    fn(kCsvHeader, ctx);
    uint32_t seq = 0;
    for (auto& d : data) {
        Record r; r.difficulty = d[0]; r.result = d[1] ? Result::GaveUp : Result::Solved;
        r.seconds = d[2]; r.hints = d[3];
        char line[96];
        format_line(line, sizeof line, ++seq, r);
        fn(line, ctx);
    }
    return true;
}
static const char* fake_location() { return "SD card"; }

// Simulated stylus taps for the touch-test screenshot: the first reading of
// each tap lands one cell low, the rest on target (Tom's 4.0" symptom).
static int fake_step = -1;
static bool fake_touch(int16_t* x, int16_t* y)
{
    static const int16_t taps[3][2] = {{100, 200}, {200, 300}, {60, 380}};
    if (fake_step < 0) return false;
    const int tap = fake_step / 14, k = fake_step % 14;
    if (tap >= 3 || k >= 9) return false;     // 9 readings, then 5 empty
    *x = taps[tap][0] + (k ? (k % 3) - 1 : 0);
    *y = taps[tap][1] + (k ? (k % 2) : 35);
    return true;
}

// A Medium puzzle part-way through: some right digits, two empty cells kept
// free for staging, notes in a few cells, 2:05 on the clock.
static int first_empty = -1, second_empty = -1;
// Tap (or long-press) a point with the fake stylus
static void preview_press(int x, int y, int hold_ms)
{
    touch_x = static_cast<int16_t>(x); touch_y = static_cast<int16_t>(y);
    touch_down = true;
    run(hold_ms);
    touch_down = false;
    run(80);
}
static void preview_tap_square(int sq, int hold_ms = 80)
{
    int x, y;
    if (board8::square_center(sq, &x, &y)) preview_press(x, y, hold_ms);
}
static const char* const kSlideLevels[3] = {"3x3", "4x4", "5x5"};
// The solo menu of whatever game is open: its ☰ key is on the screen; tap it
[[maybe_unused]] static void kit_preview_menu()
{
    lv_obj_t* scr = lv_screen_active();
    for (uint32_t i = 0; i < lv_obj_get_child_count(scr); ++i) {
        lv_obj_t* c = lv_obj_get_child(scr, (int32_t)i);
        if (lv_obj_get_child_count(c) == 3 && lv_obj_is_clickable(c)) {   // the 3 bars
            lv_obj_send_event(c, LV_EVENT_CLICKED, nullptr);
            return;
        }
    }
}

[[maybe_unused]] static void stage_sudoku_save()
{
    static sudoku::Game g;
    sudoku::Rng rng(20261001);
    g.start(sudoku::Difficulty::Medium, rng);
    sudoku::Grid p{}, sol{};
    for (int i = 0; i < 81; ++i) p.c[i] = g.given(i) ? g.value(i) : 0;
    sudoku::count_solutions(p, 2, &sol);
    int filled = 0;
    for (int i = 0; i < 81 && filled < 14; ++i) {
        if (g.given(i)) continue;
        if (first_empty < 0) { first_empty = i; continue; }
        if (second_empty < 0) { second_empty = i; continue; }
        if (i % 3 == 0) { g.enter(i, sol.c[i], false); ++filled; }
    }
    for (int i = 0, n = 0; i < 81 && n < 6; ++i) {
        if (g.given(i) || g.value(i)) continue;
        if (i == first_empty || i == second_empty) continue;
        for (int dd = 1; dd <= 9; ++dd) if ((dd + i) % 3 == 0) g.enter(i, dd, true);
        ++n;
    }
    for (int k = 0; k < 125; ++k) g.add_second();
    std::vector<uint8_t> buf(sudoku::Game::max_serialized_size());
    const size_t n = g.serialize(buf.data(), buf.size());
    save_game("sudoku", buf.data(), n);
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: preview <w> <h> <out prefix>\n"); return 2; }
    W = atoi(argv[1]); H = atoi(argv[2]);
    const std::string out = argv[3];
    lv_init();
    lv_tick_set_cb(tick);
    lv_log_register_print_cb([](lv_log_level_t, const char* m) { fputs(m, stderr); });
    fb.assign(W * H, 0);
    lv_display_t* d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(d, flush);
    lv_display_set_buffers(d, fb.data(), nullptr, W * H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_indev_t* stylus = lv_indev_create();
    lv_indev_set_type(stylus, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(stylus, touch_read);

    ui::Shell sh{};
    sh.random_seed = seed;
    sh.load_game = load_game;
    sh.save_game = save_game;
    sh.raw_touch = fake_touch;
    sh.stats_read = stats_read;
    sh.stats_append = [](const char*, const char*, const char*) { return true; };
    sh.stats_location = fake_location;
    sh.stats_delete_last = [](const char*) { return true; };
    sh.stats_clear = [](const char*) { return true; };
    // Look like a real board so the README screenshots read naturally.
    sh.firmware_version = "v0.1.0";
    sh.board_name = W == 240 ? "3.2\" ST7789 Resistive" : "4.0\" ST7796 Resistive";

    static ui::CustomThemes themes;      // slot 0: a green/brown custom theme
    {
        ui::CustomTheme& c = themes.slot[0];
        c.base = 0;
        const uint32_t pick[ui::kRoles] = {0xE1F9DC, 0xFFFFFF, 0xB3F2A6, 0x24282D, 0x174482,
                                           0xE49258, 0xF2E3A6, 0xDCF9F4, 0xFFFFFF, 0x39C91D};
        for (int r = 0; r < ui::kRoles; ++r) { c.color[r] = pick[r]; c.set |= 1u << r; }
    }

#ifdef CYD_PAGING_TEST
    ui::UiSettings fresh;
    ui::app_begin(sh, fresh, themes);
    shot(out + "_list.ppm");
    for (int c = 0; c < games::kCategories; ++c) {
        ui::picker_open_category(c);
        for (int p = 1; p <= 3; ++p) {
            shot(out + "_cat" + std::to_string(c) + "_page" + std::to_string(p) + ".ppm");
            ui::picker_next_page();
        }
    }
    fprintf(stderr, "peak LVGL heap use: %u B\n", peak_used);
    return 0;
#else
    // 1. First boot: no game played yet
    ui::UiSettings fresh;
    ui::app_begin(sh, fresh, themes);
    shot(out + "_light_0_picker_new.ppm");

    // 2. Picker with "Continue Sudoku" (light, dark) and its menu
    stage_sudoku_save();
    ui::UiSettings played;
    strcpy(played.last_game, "sudoku");
    for (int t = 0; t < 2; ++t) {
        played.theme = t ? ui::Theme::Dark : ui::Theme::Light;
        ui::app_begin(sh, played, themes);
        shot(out + (t ? "_dark" : "_light") + "_0_picker.ppm");
        ui::picker_open_category(0);
        shot(out + (t ? "_dark" : "_light") + "_0_category.ppm");
    }
    ui::app_begin(sh, played, themes);
    ui::picker_open_menu();
    shot(out + "_dark_0_picker_menu.ppm");
    ui::settings_open(ui::picker_open_menu);
    shot(out + "_dark_0_settings.ppm");
    ui::theme_open();
    shot(out + "_dark_0_theme.ppm");
    ui::app_set_theme(ui::Theme::Custom1);
    ui::theme_open();
    shot(out + "_custom_0_theme.ppm");
    ui::theme_open_editor(0);
    shot(out + "_custom_0_editor.ppm");
    ui::theme_open_palette(0, ui::Role::Selected);
    shot(out + "_custom_0_palette.ppm");
    ui::close_overlays();
    ui::app_open_game_now(games::find("sudoku"));
    sudoku_ui::tap_cell(40);
    shot(out + "_custom_1_sudoku.ppm");
    ui::app_go_home_now();
    shot(out + "_custom_0_picker.ppm");
    ui::app_set_theme(ui::Theme::Light);

    // 3. Sudoku, staged like CYD-Sudoku's screenshots
    played.theme = ui::Theme::Light;
    ui::app_begin(sh, played, themes);
    ui::app_open_game_now(games::find("sudoku"));
    run(30);

    sudoku::Game probe;                       // to find cells to stage
    {
        std::vector<uint8_t> b(sudoku::Game::max_serialized_size());
        probe.deserialize(b.data(), load_game("sudoku", b.data(), b.size()));
    }
    int with_value = -1;
    for (int i = 40; i < 81; ++i) if (probe.value(i) && !probe.given(i)) { with_value = i; break; }
    int clash = 0;
    for (int j = 0; j < 81; ++j) if (probe.given(j) && sudoku::same_unit(first_empty, j)) { clash = probe.value(j); break; }

    for (int t = 0; t < 2; ++t) {
        const std::string pre = out + (t ? "_dark" : "_light");
        ui::settings().theme = t ? ui::Theme::Dark : ui::Theme::Light;
        ui::set_theme(ui::settings().theme);
        ui::styles_apply();
        ui::app_theme_changed();
        sudoku_ui::set_input_mode(ui::InputMode::CellFirst);

        sudoku_ui::tap_cell(with_value);                 // row/col/box + same digits
        shot(pre + "_1_select.ppm");
        sudoku_ui::tap_cell(first_empty);                // a clashing entry
        sudoku_ui::tap_digit(clash);
        shot(pre + "_2_conflict.ppm");
        sudoku_ui::tap_digit(clash);                     // same digit again clears it
        sudoku_ui::set_input_mode(ui::InputMode::DigitFirst);
        sudoku_ui::tap_digit(5);                         // digit first, notes on
        sudoku_ui::set_notes(true);
        shot(pre + "_3_digit_first_notes.ppm");
        sudoku_ui::set_notes(false);
        sudoku_ui::tap_digit(5);
        sudoku_ui::hint();                               // hint pointing at a cell
        shot(pre + "_4_hint.ppm");
        sudoku_ui::hint();                               // second tap fills it
        sudoku_ui::open_menu();
        shot(pre + "_5_menu.ppm");
        sudoku_ui::open_stats();
        shot(pre + "_6_stats.ppm");
        ui::settings_open(sudoku_ui::open_menu);
        shot(pre + "_7_settings.ppm");
        ui::close_overlays();
        run(30);
    }

    // Solve with hints: mid-flash and after
    ui::settings().theme = ui::Theme::Light;
    ui::set_theme(ui::Theme::Light);
    ui::styles_apply();
    ui::app_theme_changed();
    for (int k = 0; k < 200; ++k) sudoku_ui::hint();
    shot(out + "_light_8_solved.ppm");

    ui::settings_open_touch_test();
    for (fake_step = 0; fake_step < 3 * 14; ++fake_step) { fake_ms += 10; lv_timer_handler(); }
    fake_step = -1;
    shot(out + "_light_9_touch_test.ppm");
    ui::close_overlays();

    // 4. The stage-2 games
    ui::app_go_home_now();
    ui::picker_open_category(0);
    shot(out + "_light_11_puzzles.ppm");
    ui::picker_open_category(1);
    shot(out + "_light_12_strategy.ppm");
    {   // Sliding Tiles: a 4x4 two slides from solved
        sliding::Puzzle p; p.reset(4); p.tap(14); p.tap(10);
        sliding::Puzzle st; st.reset(4); sliding::Rng rng(5); st.shuffle(rng);
        uint8_t buf[2 * sliding::Puzzle::kSaveBytes + 6] = {};
        size_t n = st.serialize(buf, sizeof buf);       // a shuffled board on screen
        n += st.serialize(buf + n, sizeof buf - n);
        buf[n++] = 1; buf[n++] = 0;
        buf[n] = 95;                                    // 1:35 on the clock
        save_game("sliding", buf, sizeof buf);
        ui::app_open_game_now(games::find("sliding"));
        shot(out + "_light_13_sliding.ppm");
        ui::app_go_home_now();
        n = p.serialize(buf, sizeof buf);               // two slides from solved
        n += st.serialize(buf + n, sizeof buf - n);
        save_game("sliding", buf, sizeof buf);
        ui::app_open_game_now(games::find("sliding"));
        shot(out + "_light_14_sliding_near.ppm");
        ui::app_go_home_now();
    }
    {   // Light Switch, Medium
        lightswitch::Puzzle p; lightswitch::Rng rng(11); p.generate(1, rng);
        p.press(p.hint());
        uint8_t buf[lightswitch::Puzzle::kSaveBytes + 6] = {};
        size_t n = p.serialize(buf, sizeof buf);
        buf[n++] = 1; buf[n++] = 0; buf[n] = 42;
        save_game("lightswitch", buf, sizeof buf);
        ui::app_open_game_now(games::find("lightswitch"));
        shot(out + "_light_15_lightswitch.ppm");
        kit_preview_menu();
        shot(out + "_light_16_lightswitch_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
    }
    {   // FourConnect vs the computer (Medium), a few moves in
        ui::app_open_game_now(games::find("fourconnect"));
        const int mine[] = {3, 3, 2, 4};
        for (int c : mine) { match::human_move(c); run(600); }
        shot(out + "_light_17_fourconnect.ppm");
        match::open_menu();
        shot(out + "_light_18_twoplayer_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::settings().theme = ui::Theme::Dark;
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("fourconnect"));
        shot(out + "_dark_17_fourconnect.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }
    {   // Tic-Tac-Toe, pass and play: X wins
        ui::app_open_game_now(games::find("tictactoe"));
        match::state().mode = twoplayer::Mode::PassAndPlay;
        match::restart_view();
        const int seq[] = {4, 0, 2, 1, 6};             // X takes the 2-4-6 diagonal
        for (int i : seq) { match::human_move(i); run(50); }
        shot(out + "_light_19_tictactoe.ppm");
        ui::app_go_home_now();
        ui::app_open_game_now(games::find("tictactoe"));
        shot(out + "_light_20_tictactoe_again.ppm");
        match::open_menu();
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_open_game_now(games::find("fourconnect"));
        kit::stats_two_player("fourconnect", {"Red", "Yellow"}, match::open_menu);
        shot(out + "_light_21_fourconnect_stats.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_open_game_now(games::find("sliding"));
        kit::stats_solo("sliding", kSlideLevels, nullptr);
        shot(out + "_light_22_sliding_stats.ppm");
        ui::close_overlays();
    }

    {   // Reversi vs the computer, mid-game, player's turn
        reversi::Board b;
        for (int k = 0; k < 14; ++k) b.play(reversi::best_move(b, 1, 77 + k));
        std::vector<uint8_t> buf(reversi::Board::kSaveBytes + match::kStateBytes);
        b.serialize(buf.data(), buf.size());
        match::State st; st.human_side = b.side; st.seconds = 183;
        { match::State keep = match::state(); match::state() = st;
          match::save_state(buf.data() + reversi::Board::kSaveBytes, match::kStateBytes);
          match::state() = keep; }
        save_game("reversi", buf.data(), buf.size());
        ui::app_open_game_now(games::find("reversi"));
        shot(out + "_light_23_reversi.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("reversi"));
        shot(out + "_dark_23_reversi.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Checkers vs the computer: player is White (board turned), mid-game,
        // then a long-press view and a picked piece
        checkers::Game g;
        for (int k = 0; k < 17; ++k) g.play(checkers::best_move(g, 1, 300 + k));
        std::vector<uint8_t> buf(checkers::Game::kSaveBytes + match::kStateBytes);
        g.serialize(buf.data(), buf.size());
        match::State st; st.human_side = g.turn(); st.seconds = 401;
        { match::State keep = match::state(); match::state() = st;
          match::save_state(buf.data() + checkers::Game::kSaveBytes, match::kStateBytes);
          match::state() = keep; }
        save_game("checkers", buf.data(), buf.size());
        ui::app_open_game_now(games::find("checkers"));
        shot(out + "_light_24_checkers.ppm");
        checkers::MoveList l; g.legal(l);
        preview_tap_square(l.m[0].from);
        shot(out + "_light_25_checkers_pick.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("checkers"));
        shot(out + "_dark_24_checkers.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Chess vs the computer: player is White, an opening, a knight picked,
        // then a long-press on a black piece; and the promotion question
        chess::Game* g = new chess::Game();
        const char* opening[] = {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "d2d3", "f8c5", "c2c3"};
        for (const char* mv : opening) {
            chess::MoveList l; g->legal(l);
            const int from = (mv[1] - '1') * 8 + (mv[0] - 'a'), to = (mv[3] - '1') * 8 + (mv[2] - 'a');
            for (int k = 0; k < l.n; ++k) if (l.m[k].from == from && l.m[k].to == to) { g->play(k); break; }
        }
        std::vector<uint8_t> buf(chess::Game::kSaveBytes + match::kStateBytes);
        g->serialize(buf.data(), buf.size());
        match::State st; st.human_side = 1; st.seconds = 254;     // the player has Black
        { match::State keep = match::state(); match::state() = st;
          match::save_state(buf.data() + chess::Game::kSaveBytes, match::kStateBytes);
          match::state() = keep; }
        save_game("chess", buf.data(), buf.size());
        ui::app_open_game_now(games::find("chess"));
        shot(out + "_light_26_chess.ppm");
        preview_tap_square(57);                                  // b8 knight... moved: c6
        preview_tap_square(42);
        shot(out + "_light_27_chess_pick.ppm");
        preview_tap_square(42);
        preview_tap_square(26, 700);                             // long-press the white bishop on c4
        shot(out + "_light_28_chess_peek.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("chess"));
        shot(out + "_dark_26_chess.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        delete g;
    }

    // 5. "All games": back to the picker, which now offers the last game
    ui::app_go_home_now();
    shot(out + "_light_10_picker_after.ppm");
    fprintf(stderr, "peak LVGL heap use: %u B\n", peak_used);
    return 0;
#endif
}
