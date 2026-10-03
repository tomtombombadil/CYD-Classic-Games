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
#include "games/registry.h"
#include "games/sudoku/sudoku_game.h"
#include "games/sudoku/sudoku_screen.h"
#include "games/sudoku/sudoku_stats.h"
#include "ui/shell.h"
#include "ui/widgets.h"

static uint32_t fake_ms = 0;
static uint32_t tick() { return fake_ms; }
static void flush(lv_display_t* d, const lv_area_t*, uint8_t*) { lv_display_flush_ready(d); }

static std::vector<uint16_t> fb;
static int W, H;
static unsigned peak_used = 0;

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

static bool stats_read(const char*, void (*fn)(const char*, void*), void* ctx)
{
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

#ifdef CYD_PAGING_TEST
    ui::UiSettings fresh;
    ui::app_begin(sh, fresh);
    shot(out + "_page1.ppm");
    for (int p = 2; p <= 4; ++p) {
        ui::app_go_home_now();            // no-op switch keeps the page
        lv_obj_t* scr = lv_screen_active();
        // Tap the ">" pager key: the last child created on the picker
        lv_obj_t* next = lv_obj_get_child(scr, (int32_t)lv_obj_get_child_count(scr) - 2);
        lv_obj_send_event(next, LV_EVENT_CLICKED, nullptr);
        run(30);
        shot(out + "_page" + std::to_string(p) + ".ppm");
    }
    fprintf(stderr, "peak LVGL heap use: %u B\n", peak_used);
    return 0;
#else
    // 1. First boot: no game played yet
    ui::UiSettings fresh;
    ui::app_begin(sh, fresh);
    shot(out + "_light_0_picker_new.ppm");

    // 2. Picker with "Continue Sudoku" (light, dark) and its menu
    stage_sudoku_save();
    ui::UiSettings played;
    strcpy(played.last_game, "sudoku");
    for (int t = 0; t < 2; ++t) {
        played.theme = t ? ui::Theme::Dark : ui::Theme::Light;
        ui::app_begin(sh, played);
        shot(out + (t ? "_dark" : "_light") + "_0_picker.ppm");
    }
    ui::app_begin(sh, played);
    ui::picker_open_menu();
    shot(out + "_dark_0_picker_menu.ppm");
    ui::close_overlays();

    // 3. Sudoku, staged like CYD-Sudoku's screenshots
    played.theme = ui::Theme::Light;
    ui::app_begin(sh, played);
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

    // 4. "All games": back to the picker, which now offers the solved game
    ui::app_go_home_now();
    shot(out + "_light_10_picker_after.ppm");
    fprintf(stderr, "peak LVGL heap use: %u B\n", peak_used);
    return 0;
#endif
}
