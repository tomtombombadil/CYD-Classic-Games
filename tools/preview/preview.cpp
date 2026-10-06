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
#include "games/common/wplay.h"
#include "games/common/net_games.h"
#include "net/names.h"
#include "ui/sysbar.h"
#include "games/common/puzzle_stats.h"
#include "games/common/two_player.h"
#include "games/lightswitch/lightswitch_core.h"
#include "games/minesweeper/minesweeper_core.h"
#include "games/registry.h"
#include "games/checkers/checkers_core.h"
#include "games/chess/chess_core.h"
#include "games/cyddle/cyddle_core.h"
#include "games/yahtcyd/yahtcyd_core.h"
#include "games/rpgdice/rpgdice_core.h"
#include "games/vpoker/vpoker_core.h"
#include "games/holdem/holdem_core.h"
#include "games/farkle/farkle_core.h"
#include "games/mancala/mancala_core.h"
#include "games/sank/sank_core.h"
#include "games/wheel/wheel_core.h"
#include "games/morris/morris_core.h"
namespace wheel_preview {
wheel::Game* game();
void key(char c);
void action(int k);
} // namespace wheel_preview
namespace sank_preview {
sank::Board* board();
void ready();
void page(int p);
void cover_ready();
void pass();
void undo();
void tap(int c);
void options();
bool idle();
bool shooting();
} // namespace sank_preview
namespace morris_preview {
morris::Game* game();
void tap_point(int p);
}
namespace mancala_preview {
mancala::Board* board();
void finish();
}
namespace farkle_preview {
farkle::Game* game();
void refresh();
void step(uint32_t ms);
}
namespace holdem_preview {
void run_to_you();
void deal(uint32_t seed);
void you(holdem::Act a);
holdem::Game* game();
void refresh();
}
namespace rpgdice_preview {
void set_pool(const int* counts, int mod);
void roll_pool(uint32_t seed);
void roll_preset(int i, uint32_t seed);
void presets();
void history();
void editor(int slot);
void line_editor(int slot, int line);
void keyboard(int slot);
}
#include "games/twenty48/twenty48_core.h"
#include "games/piperace/piperace_core.h"
#include "games/acquisitions/acquisitions_core.h"
namespace acq_preview { acq::Game* game(); void redraw(); void hold(bool on); void log(const char* t); void stocks(); }
#include "games/strategygo/strategygo_core.h"
namespace sgo_preview { sgo::Board* board(); void pick(int c); void battle(bool on); void ready(); void cover_ready(); void pass(); void pieces(); }
#include "games/dealcyd/dealcyd_core.h"
namespace dealcyd_preview { dealcyd::Game* game(); void reveal(bool on); void redraw(); }
#include "games/presscyd/presscyd_core.h"
#include "games/cardsharks/cardsharks_core.h"
#include "games/hollywood/hollywood_core.h"
namespace hollywood_preview { hcyd::Board* board(); void sync(); void reveal(bool on); }
#include "games/jeoparcyd/jeoparcyd_core.h"
namespace jeoparcyd_preview { jcyd::Game* game(); void hold(bool on); void refresh(); void status_line(const char* t); void final_clue(); }
#include "games/whowants/whowants_core.h"
namespace whowants_preview { whowants::Game* game(); void refresh(); void hold(bool on); void advance(int n); void ladder(); }
#include "games/escape/escape_core.h"
namespace escape_preview { escape::Game* game(); void hold(bool on); void select(int kind, int idx, int hex); void news_line(const char* t); void show_banner(bool on); }
#include "games/sorrycyd/sorrycyd_core.h"
namespace sorrycyd_preview { sorry::Game* game(); void pick(int p); void hold(bool on); void news_line(const char* t); void people(); }
namespace cardsharks_preview { csh::Board* board(); void redraw_all(); }
namespace presscyd_preview { presscyd::Game* game(); void set_light(int q); void result(bool on); void news_line(const char* t); void hold(); }
namespace piperace_preview { piperace::Game* game(); void unhold(); void redraw(); }
#include "games/mastercyd/mastercyd_core.h"
#include "games/pegs/pegs_core.h"
#include "games/memory/memory_core.h"
#include "games/nonogram/nonogram_core.h"
#include "games/solitaire/solitaire_core.h"
#include "games/golf/golf_core.h"
#include "games/pyramid/pyramid_core.h"
#include "games/spider/spider_core.h"
#include "games/freecell/freecell_core.h"
#include "games/blackjack/blackjack_core.h"
#include "games/reversi/reversi_core.h"
#include "games/sliding/sliding_core.h"
#include "games/sudoku/sudoku_game.h"
#include "games/sudoku/sudoku_screen.h"
#include "games/sudoku/sudoku_stats.h"
#include "games/common/board8.h"
#include "games/common/game_kit.h"
#include "ui/shell.h"
#include "ui/widgets.h"
#include "net/wireless.h"

void card_mockup(int w, const char* title, const char* status);
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

static void (*net_hook)(uint32_t now) = nullptr;     // made-up boards nearby (wireless shots)
static void run(int ms)
{
    for (int t = 0; t < ms; t += 10) {
        fake_ms += 10;
        if (net_hook) net_hook(fake_ms);
        lv_timer_handler();
        ui::app_tick(fake_ms);
    }
}

// keep = save what's on the panel as it is (the card win show's trails)
static void shot(const std::string& path, bool keep = false)
{
    if (!keep) run(50);
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    const unsigned used = mon.total_size - mon.free_size;
    if (used > peak_used) peak_used = used;
    fprintf(stderr, "%-34s LVGL heap used %3u%% (%6u B), biggest free %6u B\n", path.c_str(),
            (unsigned)mon.used_pct, used, (unsigned)mon.free_biggest_size);
    if (!keep) {
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
    }
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
static std::map<std::string, std::vector<uint8_t>> mid_saves; // part-way games for the left-handed shots
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
static void remove_game(const char* id) { files.erase(id); }

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

static bool fake_log_big = false;
// A sample device log: a few boots, then a crash report
static bool fake_log(void (*line)(const char*, void*), void* ctx)
{
    static const char* const L[] = {
        "0:00:00 === Boot: firmware v0.1.0, 2.8\" ST7789 Resistive",
        "0:00:00 Last reset: power on",
        "0:00:00 Memory: 212 KB free, largest block 108 KB",
        "0:00:09 Open sudoku", "0:04:51 Open solitaire", "0:21:13 Open spider",
        "0:00:00 === Boot: firmware v0.1.0, 2.8\" ST7789 Resistive",
        "0:00:00 Last reset: restart by the firmware",
        "0:00:00 Memory: 212 KB free, largest block 108 KB",
        "0:00:12 Open freecell", "0:08:40 Open chess", "0:09:02 AI stack tight: 1460 of 32768 bytes unused",
        "0:00:00 === Boot: firmware v0.1.0, 2.8\" ST7789 Resistive",
        "0:00:00 Last reset: CRASH",
        "0:00:00 Crash: Task watchdog got triggered. The following tasks did not reset the watchdog in time",
        "0:00:00 Task ai on core 0 at 0x400d8f3c",
        "0:00:00 Backtrace: 400d8f3c 400d9122 400da410",
        "0:00:00   400e0a6c 4008ff2d",
        "0:00:00 Last step: solitaire: deal search, draw 3",
        "0:00:00 Memory: 212 KB free, largest block 108 KB",
        "0:00:05 Open solitaire",
    };
    for (const char* l : L) line(l, ctx);
    if (fake_log_big) {                     // a full log: 16 KB of ordinary days
        static const char* const G[] = {"sudoku", "solitaire", "chess", "spider", "freecell", "minesweeper"};
        char b[96];
        for (int boot = 0; boot < 60; ++boot) {
            line("0:00:00 === Boot: firmware v0.9.0 (1a2b3c4), 2.8\" ST7789 Resistive", ctx);
            line(boot % 7 == 3 ? "0:00:00 Last reset: CRASH" : "0:00:00 Last reset: power on", ctx);
            line("0:00:00 Memory: 212 KB free, largest block 108 KB", ctx);
            for (int k = 0; k < 4; ++k) {
                const int t = 7 + boot * 97 % 600 + k * 431;
                snprintf(b, sizeof b, "%d:%02d:%02d Open %s", t / 3600, t / 60 % 60, t % 60, G[(boot * 5 + k) % 6]);
                line(b, ctx);
            }
        }
    }
    return true;
}
static void fake_memory(uint32_t* f, uint32_t* b) { *f = 187 * 1024; *b = 104 * 1024; }

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
[[maybe_unused]] static void preview_tap_square(int sq, int hold_ms = 80)
{
    int x, y;
    if (board8::square_center(sq, &x, &y)) preview_press(x, y, hold_ms);
}
static const char* const kSlideLevels[3] = {"3x3", "4x4", "5x5"};
// Press the overlay key whose label reads `text` (menus, Options)
static bool press_key_labelled(lv_obj_t* o, const char* text)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) {
        lv_obj_t* c = lv_obj_get_child(o, (int32_t)i);
        if (lv_obj_check_type(c, &lv_label_class) && strcmp(lv_label_get_text(c), text) == 0) {
            lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr);
            return true;
        }
        if (press_key_labelled(c, text)) return true;
    }
    return false;
}
[[maybe_unused]] static bool press_overlay_key(const char* text)
{
    return ui::overlay() && press_key_labelled(ui::overlay(), text);
}
// The same for a key whose label starts with `prefix` (labels with counts)
static bool press_key_prefix(lv_obj_t* o, const char* prefix)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) {
        lv_obj_t* c = lv_obj_get_child(o, (int32_t)i);
        // a key's label (not a title on the overlay or the screen itself)
        if (lv_obj_check_type(c, &lv_label_class) && o != ui::overlay() && o != lv_screen_active()
            && lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)
            && strncmp(lv_label_get_text(c), prefix, strlen(prefix)) == 0) {
            lv_obj_send_event(o, LV_EVENT_CLICKED, nullptr);
            return true;
        }
        if (press_key_prefix(c, prefix)) return true;
    }
    return false;
}
[[maybe_unused]] static bool press_overlay_prefix(const char* prefix)
{
    const bool ok = ui::overlay() && press_key_prefix(ui::overlay(), prefix);
    if (!ok) fprintf(stderr, "PREVIEW: no key \"%s...\"\n", prefix);
    return ok;
}
// The solo menu of whatever game is open: its ☰ key is on the screen; tap it
// The game's menu: the header's gear
[[maybe_unused]] static void kit_preview_menu() { ui::sysbar_gear(); }

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


// ---- Fake radio: made-up boards nearby for the wireless shots ---------------------
// Each one runs the real protocol (src/net/wireless.*): Bob plays Chess back
// with the computer's Easy move; Ann plays a few games; Cy runs another
// version; Di is in a game with someone else.
struct FakeBoard {
    net::Mac      mac;
    net::Presence pres;
    net::Link     link;
    net::Air      air;
    bool          on = false, in_game = false;
    bool          accept = true;             // says Play to a request
    int           offer_key = -1;            // asks the preview board to play this game (key)
    net::Profile  prof;
    bool          busy = false;              // in a game with someone else
    chess::Game*  chess = nullptr;
    uint32_t      move_at = 0;
};
struct FakePacket { net::Mac from; std::vector<uint8_t> d; };
static std::vector<FakePacket> to_preview;
static FakeBoard fake_boards[4];
static bool fake_radio_on = false;
static const net::Mac kPreviewMac = [] { net::Mac m; const uint8_t b[6] = {0x24, 0x6F, 0x28, 0x10, 0x3F, 0x2A}; memcpy(m.b, b, 6); return m; }();

static void fake_board_send(const uint8_t* d, size_t n, void* ctx)
{
    if (!fake_radio_on) return;
    to_preview.push_back(FakePacket{static_cast<FakeBoard*>(ctx)->mac, std::vector<uint8_t>(d, d + n)});
}
static bool fake_radio_send(const uint8_t* d, size_t n)
{
    if (!fake_radio_on) return false;
    const uint32_t s = net::status_session(d, n);
    for (FakeBoard& b : fake_boards) {
        if (!b.on) continue;
        if (b.in_game && s && s == b.link.session()) b.link.receive(kPreviewMac, d, n, fake_ms);
        else b.pres.receive(kPreviewMac, d, n, fake_ms);
    }
    return true;
}
static size_t fake_radio_recv(uint8_t mac[6], uint8_t* buf, size_t cap, int8_t* rssi)
{
    if (to_preview.empty()) return 0;
    if (rssi) *rssi = -58;
    FakePacket p = to_preview.front();
    to_preview.erase(to_preview.begin());
    memcpy(mac, p.from.b, 6);
    const size_t n = p.d.size() < cap ? p.d.size() : cap;
    memcpy(buf, p.d.data(), n);
    return n;
}
static uint32_t fake_clock() { return fake_ms; }
static void fake_boards_tick(uint32_t now)
{
    for (FakeBoard& b : fake_boards) {
        if (!b.on) continue;
        b.prof.busy = b.busy || (b.in_game && !b.link.ended());
        b.pres.set_profile(b.prof, now);
        b.pres.tick(now);
        if (b.offer_key >= 0 && !b.pres.requesting() && !b.in_game) b.pres.request(kPreviewMac, 0, 0, b.offer_key, 0x5EED, now);
        if (b.accept && b.pres.asked()) b.pres.accept(now);
        if (b.pres.poll() == net::Presence::Event::Started) {
            b.link.begin(b.mac, b.pres.partner(), b.pres.partner_name_a(), b.pres.partner_name_b(), b.pres.session(),
                         b.pres.inviter(), b.pres.game_key(), b.pres.timer(), b.air, now);
            b.in_game = true;
            b.offer_key = -1;
            if (!b.chess) b.chess = new chess::Game();
            kit::renew(*b.chess);
            b.move_at = now + 900;
        }
        if (!b.in_game || b.link.ended()) continue;
        b.link.tick(now);
        for (uint32_t m; b.link.next_move(&m);) {
            b.chess->play(chess::find_key(*b.chess, m));
            b.link.played(m, now);
            b.move_at = now + 900;
        }
        if (b.chess->result() == -1 && b.chess->turn() == b.link.my_side() && b.link.up(now)
            && int32_t(now - b.move_at) >= 0) {
            const int i = chess::best_move(*b.chess, 0, 7, fake_clock);
            chess::MoveList l;
            b.chess->legal(l);
            const uint32_t key = chess::move_key(l.m[i]);
            b.chess->play(i);
            b.link.played(key, now);
        }
    }
}
[[maybe_unused]] static void fake_boards_begin(const char* fw)
{
    // Bob, Ann (three games), Cy (a later Chess), Di (busy in a game)
    const uint16_t first[4] = {12, 0, 33, 51}, second[4] = {13, 2, 60, 23};
    for (int i = 0; i < 4; ++i) {
        FakeBoard& b = fake_boards[i];
        const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x77, 0x00, uint8_t(0x10 + i)};
        memcpy(b.mac.b, mac, 6);
        b.air.send = fake_board_send;
        b.air.ctx = &b;
        b.pres.begin(b.mac, b.air, fake_ms);
        b.prof = net::Profile{};
        b.prof.name_a = first[i];
        b.prof.name_b = second[i];
        b.prof.fw = net::Version::parse(fw);
        b.prof.available = true;
        for (const netgames::Entry& e : netgames::kGames) {
            if (i == 1 && strcmp(e.id, "tictactoe") && strcmp(e.id, "checkers") && strcmp(e.id, "mancala")) continue;
            b.prof.games[b.prof.n_games].key = e.key;
            // Cy has an older Chess
            b.prof.games[b.prof.n_games].version = uint8_t(i == 2 && !strcmp(e.id, "chess") ? e.version + 1 : e.version);
            ++b.prof.n_games;
        }
        b.on = true;
        b.in_game = false;
        b.accept = true;
        b.offer_key = -1;
        b.busy = i == 3;
    }
    net_hook = fake_boards_tick;
}
// Index of a chess move in the move list, from "e2e4"
[[maybe_unused]] static int chess_index(const chess::Game& g, const char* uci)
{
    const int from = (uci[1] - '1') * 8 + (uci[0] - 'a'), to = (uci[3] - '1') * 8 + (uci[2] - 'a');
    chess::MoveList l;
    g.legal(l);
    for (int k = 0; k < l.n; ++k) if (l.m[k].from == from && l.m[k].to == to) return k;
    return -1;
}


// ---- Agent mode: one board of a two-board test (tools/preview/duo.py) ----------------
// `preview --agent <w> <h> <name> <mac byte> <files dir>` reads commands on
// stdin, one per line, and answers each with lines ending in "OK". The
// coordinator runs two agents in lockstep and carries their radio packets.
//   tick <ms>         run that long; prints each packet sent as "P <hex>"
//   rx <mac> <hex>    a packet arrives (delivered on the next tick)
//   press <prefix>    tap the key whose label starts so (overlay or screen)
//   wplay | twop 0/1 | timer <s> | back | icons | home | menu | open <game id> | move <n> | chess <e2e4>
//   dump              every label on screen: "D text | text | ..."
//   may               "M 1" if this player may move
//   stats             the stats lines recorded so far: "R <game> <line>"
//   shot <path>
static std::vector<std::vector<uint8_t>> agent_out;
static std::vector<std::pair<net::Mac, std::vector<uint8_t>>> agent_in;
static bool agent_radio = false;
static bool agent_doze = false;          // dozing: packets only arrive in the wake windows
static net::Mac agent_mac;
static std::string agent_dir;
static std::vector<std::string> agent_stats;

static std::string hex(const uint8_t* d, size_t n)
{
    static const char* h = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += h[d[i] >> 4]; s += h[d[i] & 15]; }
    return s;
}
static std::vector<uint8_t> unhex(const std::string& s)
{
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(uint8_t(std::stoi(s.substr(i, 2), nullptr, 16)));
    return v;
}
static void agent_save(const char* id, const uint8_t* buf, size_t len)
{
    files[id].assign(buf, buf + len);
    if (agent_dir.empty()) return;
    FILE* f = fopen((agent_dir + "/" + id + ".bin").c_str(), "wb");
    if (f) { fwrite(buf, 1, len, f); fclose(f); }
}
static void agent_load_dir()
{
    if (agent_dir.empty()) return;
    std::string cmd = "ls '" + agent_dir + "' 2>/dev/null";
    FILE* p = popen(cmd.c_str(), "r");
    char name[256];
    while (p && fgets(name, sizeof name, p)) {
        std::string n(name);
        while (!n.empty() && (n.back() == '\n' || n.back() == '\r')) n.pop_back();
        if (n.size() < 5 || n.substr(n.size() - 4) != ".bin") continue;
        FILE* f = fopen((agent_dir + "/" + n).c_str(), "rb");
        if (!f) continue;
        std::vector<uint8_t> d;
        int c;
        while ((c = fgetc(f)) != EOF) d.push_back(uint8_t(c));
        fclose(f);
        files[n.substr(0, n.size() - 4)] = d;
    }
    if (p) pclose(p);
}
static void dump_labels(lv_obj_t* o, std::string& out)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        std::string t = lv_label_get_text(o);
        for (char& c : t) if (c == '\n') c = ' ';
        if (!t.empty()) out += t + " | ";
    }
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); ++i) dump_labels(lv_obj_get_child(o, (int32_t)i), out);
}
static bool press_prefix_anywhere(const char* prefix)
{
    if (ui::overlay() && press_key_labelled(ui::overlay(), prefix)) return true;   // exact first
    if (ui::overlay() && press_key_prefix(ui::overlay(), prefix)) return true;
    return press_key_prefix(lv_screen_active(), prefix);
}

static int agent_main(int argc, char** argv)
{
    if (argc < 7) { fprintf(stderr, "usage: preview --agent <w> <h> <name> <mac byte> <dir>\n"); return 2; }
    W = atoi(argv[2]); H = atoi(argv[3]);
    const char* name = argv[4];
    const uint8_t mb = uint8_t(atoi(argv[5]));
    agent_dir = argv[6];
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x55, 0x00, mb};
    memcpy(agent_mac.b, mac, 6);
    lv_init();
    lv_tick_set_cb(tick);
    fb.assign(W * H, 0);
    lv_display_t* d = lv_display_create(W, H);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(d, flush);
    lv_display_set_buffers(d, fb.data(), nullptr, W * H * 2, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_indev_t* stylus = lv_indev_create();
    lv_indev_set_type(stylus, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(stylus, touch_read);
    agent_load_dir();
    if (!files.count("player")) {                    // the name to start with ("Wobbly Llama")
        uint16_t a = 0, b = 0;
        if (!names::parse(name, &a, &b)) { fprintf(stderr, "agent: %s is not a name from the lists\n", name); return 2; }
        const uint8_t p[13] = {'P', 'L', 'R', '3', uint8_t(a), uint8_t(a >> 8), uint8_t(b), uint8_t(b >> 8),
                               0xFF, 0xFF, 0, 30, 0};
        agent_save("player", p, sizeof p);
    }
    ui::Shell sh{};
    static uint32_t seed_state = 0;
    seed_state = 1234567u * mb + 89;
    sh.random_seed = [] { seed_state = seed_state * 1664525u + 1013904223u; return seed_state; };
    sh.load_game = load_game;
    sh.save_game = agent_save;
    sh.stats_read = [](const char*, void (*)(const char*, void*), void*) { return false; };
    sh.stats_append = [](const char* id, const char*, const char* body) {
        agent_stats.push_back(std::string(id) + " " + body);
        return true;
    };
    sh.memory = fake_memory;
    sh.radio_on = [] { agent_radio = true; return true; };
    sh.radio_off = [] { agent_radio = false; agent_in.clear(); };
    sh.radio_send = [](const uint8_t* d, size_t n) {
        if (!agent_radio) return false;
        agent_out.push_back(std::vector<uint8_t>(d, d + n));
        return true;
    };
    sh.radio_recv = [](uint8_t m[6], uint8_t* buf, size_t cap, int8_t* rssi) -> size_t {
        if (!agent_radio || agent_in.empty()) return 0;
        if (rssi) *rssi = -55;
        auto pk = agent_in.front();
        agent_in.erase(agent_in.begin());
        memcpy(m, pk.first.b, 6);
        const size_t n = pk.second.size() < cap ? pk.second.size() : cap;
        memcpy(buf, pk.second.data(), n);
        return n;
    };
    sh.radio_mac = [](uint8_t m[6]) { memcpy(m, agent_mac.b, 6); };
    sh.radio_doze = [](bool d) { agent_doze = d; };
    sh.firmware_version = "v9.9.9";
    sh.board_name = "agent";
    static ui::CustomThemes themes;
    ui::UiSettings st;
    ui::app_begin(sh, st, themes);
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t sp = l.find(' ');
        const std::string cmd = l.substr(0, sp), arg = sp == std::string::npos ? "" : l.substr(sp + 1);
        if (cmd == "tick") {
            const int ms = atoi(arg.c_str());
            agent_out.clear();
            run(ms);
            for (auto& pk : agent_out) printf("P %s\n", hex(pk.data(), pk.size()).c_str());
        } else if (cmd == "rx") {
            const size_t s2 = arg.find(' ');
            auto m = unhex(arg.substr(0, s2));
            net::Mac from;
            memcpy(from.b, m.data(), 6);
            // A dozing radio hears only during its window (phase differs per board)
            const bool hears = !agent_doze || (fake_ms + 337u * mb) % net::kDozeIntervalMs < net::kDozeWindowMs;
            if (agent_radio && hears) agent_in.push_back({from, unhex(arg.substr(s2 + 1))});
        } else if (cmd == "press") {
            printf("K %d\n", press_prefix_anywhere(arg.c_str()) ? 1 : 0);
            run(20);
        } else if (cmd == "wplay") { wplay::open_menu(); run(20); }
        else if (cmd == "twop") { wplay::set_two_player(arg == "1"); run(20); }
        else if (cmd == "timer") { wplay::set_move_timer(uint16_t(atoi(arg.c_str()))); run(20); }
        else if (cmd == "back") { ui::sysbar_back(); run(20); }
        else if (cmd == "home") { ui::app_go_home_now(); run(20); }
        else if (cmd == "menu") { kit_preview_menu(); run(20); }
        else if (cmd == "open") { ui::app_open_game_now(games::find(arg.c_str())); run(20); }
        else if (cmd == "move") { match::human_move(atoi(arg.c_str())); run(20); }
        else if (cmd == "chess") {
            chess::Game probe;
            ui::app_save_current();
            probe.deserialize(files["chess"].data(), chess::Game::kSaveBytes);
            const int k = chess_index(probe, arg.c_str());
            printf("K %d\n", k >= 0 ? 1 : 0);
            if (k >= 0) {
                chess::MoveList l;
                probe.legal(l);
                match::human_move(int(chess::move_key(l.m[k])));
            }
            run(20);
        } else if (cmd == "anymove") {
            // A random legal move (tests)
            static uint32_t r = 12345;
            int played = -1;
            if (match::human_may_move()) {
                static int moves[512];
                const int n = match::legal_moves(moves, 512);
                r = r * 1103515245u + 12345u + mb;
                if (n > 0 && match::try_move(moves[(r >> 8) % uint32_t(n)])) played = moves[(r >> 8) % uint32_t(n)];
            }
            printf("A %d\n", played);
            run(20);
        } else if (cmd == "board") {
            ui::app_save_current();
            const auto& f = files[arg];
            uint32_t h = 2166136261u;
            for (size_t i = 0; i + 8 < f.size(); ++i) h = (h ^ f[i]) * 16777619u;
            printf("B %08x %zu\n", h, f.size());
        } else if (cmd == "dump") {
            std::string o;
            dump_labels(lv_layer_sys(), o);      // the header bar
            dump_labels(lv_screen_active(), o);
            dump_labels(lv_layer_top(), o);
            printf("D %s\n", o.c_str());
        } else if (cmd == "wpstate") {
            char b[512];
            wplay::debug_state(b, sizeof b);
            printf("W %s\n", b);
        } else if (cmd == "radio") {
            printf("Z %s\n", wplay::radio_state());
        } else if (cmd == "icons") {           // the header's 2P and wifi icons
            printf("I 2p=%d wifi=%d\n", wplay::two_player_state(), wplay::wifi_level());
        } else if (cmd == "may") {
            printf("M %d\n", match::human_may_move() ? 1 : 0);
        } else if (cmd == "stats") {
            for (auto& r : agent_stats) {
                std::string t = r;
                while (!t.empty() && t.back() == '\n') t.pop_back();
                printf("R %s\n", t.c_str());
            }
        } else if (cmd == "shot") {
            shot(arg);
        } else if (cmd == "quit") {
            break;
        }
        printf("OK\n");
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && strcmp(argv[1], "--agent") == 0) return agent_main(argc, argv);
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
    sh.log_read = fake_log;
    sh.log_clear = [] {};
    sh.log_copy_sd = [] { return true; };
    sh.memory = fake_memory;
    sh.radio_on = [] { fake_radio_on = true; return true; };
    sh.radio_off = [] { fake_radio_on = false; to_preview.clear(); };
    sh.radio_send = fake_radio_send;
    sh.radio_recv = fake_radio_recv;
    sh.radio_mac = [](uint8_t mac[6]) { memcpy(mac, kPreviewMac.b, 6); };
    // Look like a real board so the README screenshots read naturally.
    sh.firmware_version = "v0.9.0";
    sh.firmware_build = "1a2b3c4";
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
        ui::picker_next_page();
        shot(out + (t ? "_dark" : "_light") + "_0_category2.ppm");
    }
    ui::app_begin(sh, played, themes);
    ui::settings_open(nullptr);                 // the header's gear
    shot(out + "_dark_0_settings.ppm");
    for (const char* page : {"Display", "Sound", "Touch", "Play", "About"}) {
        ui::settings_open(nullptr);
        press_overlay_key(page);
        run(20);
        shot(out + "_dark_0_settings_" + page + ".ppm");
    }
    ui::settings_open_display();
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
    {   // Each tap's first dot (9x9) must sit on the tap, in screen coordinates
        // (v0.19.0 drew them a header bar's height too low)
        int found = 0;
        lv_obj_t* ov = ui::overlay();
        for (uint32_t i = 0; ov && i < lv_obj_get_child_count(ov); ++i) {
            lv_obj_t* c = lv_obj_get_child(ov, (int32_t)i);
            lv_area_t a;
            lv_obj_get_coords(c, &a);
            if (lv_area_get_width(&a) != 9 || lv_area_get_height(&a) != 9) continue;
            const int cx = (a.x1 + a.x2) / 2, cy = (a.y1 + a.y2) / 2;
            bool ok = false;
            for (int t = 0; t < 3; ++t) {
                int16_t tx, ty;
                fake_step = t * 14;
                fake_touch(&tx, &ty);
                if (cx == tx && cy == ty) ok = true;
            }
            fake_step = -1;
            if (!ok) fprintf(stderr, "TOUCH TEST FAIL: a dot at %d,%d is off its tap\n", cx, cy);
            ++found;
        }
        if (found != 3) fprintf(stderr, "TOUCH TEST FAIL: %d first dots, expected 3\n", found);
    }
    ui::diagnostics_open();
    shot(out + "_light_9_diagnostics.ppm");
    ui::device_log_open();
    shot(out + "_light_9_device_log.ppm");
    ui::device_log_open(0);
    shot(out + "_light_9_device_log_1.ppm");
    ui::send_log_open();
    shot(out + "_light_9_send_log.ppm");
    fake_log_big = true;
    ui::diagnostics_open();                  // drops the loaded log
    ui::send_log_open();
    shot(out + "_light_9_send_log_full.ppm");
    fake_log_big = false;
    ui::close_overlays();

    // 4. The stage-2 games
    ui::app_go_home_now();
    ui::picker_open_category(0);
    shot(out + "_light_11_puzzles.ppm");
    ui::picker_open_category(1);
    shot(out + "_light_12_strategy.ppm");
    ui::picker_next_page();
    shot(out + "_light_12_strategy2.ppm");
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
        // then a tap on a white piece shows its moves; and the promotion question
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
        preview_tap_square(42, 550);                             // a slow, firm tap still picks
        shot(out + "_light_27_chess_pick.ppm");
        preview_tap_square(26);                                  // tap the white bishop on c4: its moves
        shot(out + "_light_28_chess_peek.ppm");
        preview_tap_square(26);                                  // the next tap clears the view
        shot(out + "_light_28_chess_after_peek.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("chess"));
        shot(out + "_dark_26_chess.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        delete g;
    }

    {   // CYD-dle, Normal: two guesses in, a third being typed; then solved
        cyddle::Game g;
        cyddle::Rng rng(21);
        g.start(1, rng);
        char a[5]; cyddle::answer_word(g.answer, a);
        auto put = [&](const char* w) { for (int k = 0; k < 5; ++k) g.type(w[k]); g.submit(); };
        put(memcmp(a, "slate", 5) ? "slate" : "crane");
        put(memcmp(a, "round", 5) ? "round" : "pithy");
        g.type(a[0]); g.type(a[1]);
        std::vector<uint8_t> buf(cyddle::Game::kSaveBytes + 5, 0);
        g.serialize(buf.data(), buf.size());
        buf[cyddle::Game::kSaveBytes + 1] = 74;
        save_game("cyddle", buf.data(), buf.size());
        ui::app_open_game_now(games::find("cyddle"));
        shot(out + "_light_29_cyddle.ppm");
        ui::app_go_home_now();
        g.typed = 0;
        put(a);
        g.serialize(buf.data(), buf.size());
        buf[cyddle::Game::kSaveBytes] = 1;                  // already recorded
        save_game("cyddle", buf.data(), buf.size());
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("cyddle"));
        shot(out + "_dark_29_cyddle_solved.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Yaht-CYD: turn 7, two rolls in, two dice held
        yahtcyd::Game g;
        yahtcyd::Rng rng(5);
        for (int t = 0; t < 6; ++t) {
            g.roll(rng);
            int best = -1, bv = -1;
            for (int b = 0; b < yahtcyd::kBoxes; ++b) if (g.can_score(b) && g.potential(b) > bv) { bv = g.potential(b); best = b; }
            g.score_box(best);
        }
        g.roll(rng); g.toggle_hold(0); g.toggle_hold(3); g.roll(rng);
        std::vector<uint8_t> buf(yahtcyd::Game::kSaveBytes + 6, 0);
        g.serialize(buf.data(), buf.size());
        buf[yahtcyd::Game::kSaveBytes + 2] = 0x2C; buf[yahtcyd::Game::kSaveBytes + 3] = 1;   // 5:00
        save_game("yahtcyd", buf.data(), buf.size());
        ui::app_open_game_now(games::find("yahtcyd"));
        shot(out + "_light_30_yahtcyd.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("yahtcyd"));
        shot(out + "_dark_30_yahtcyd.ppm");
        kit_preview_menu();
        shot(out + "_dark_31_yahtcyd_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Video Poker: a fresh table, a dealt hand with the hint's holds, a win
        ui::app_open_game_now(games::find("vpoker"));
        shot(out + "_light_54_vpoker_new.ppm");
        ui::app_go_home_now();
        vpoker::Game g;
        uint32_t seed = 1;
        // a deal whose hint holds a pair
        for (;; ++seed) { vpoker::Game t; t.deal(seed); if (vpoker::evaluate(t.hand) == vpoker::JacksOrBetter) { g = t; break; } }
        g.held = vpoker::hint(g.hand);
        uint8_t buf[vpoker::Game::kSaveBytes];
        g.serialize(buf, sizeof buf);
        save_game("vpoker", buf, sizeof buf);
        ui::app_open_game_now(games::find("vpoker"));
        shot(out + "_light_54_vpoker_held.ppm");
        ui::app_go_home_now();
        // a finished hand that won: a full house
        for (;; ++seed) { vpoker::Game t; t.deal(seed); t.held = vpoker::hint(t.hand); t.draw(); if (t.last_rank == vpoker::FullHouse) { g = t; break; } }
        g.serialize(buf, sizeof buf);
        save_game("vpoker", buf, sizeof buf);
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("vpoker"));
        shot(out + "_dark_54_vpoker_win.ppm");
        kit_preview_menu();
        shot(out + "_dark_55_vpoker_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Texas Hold'em: a new table, your turn on the flop, a showdown
        ui::app_open_game_now(games::find("holdem"));
        shot(out + "_light_56_holdem_new.ppm");
        // A deal where you see the flop
        uint32_t seed = 3;
        for (;; ++seed) {
            holdem_preview::deal(seed);
            holdem_preview::run_to_you();
            holdem::Game* g = holdem_preview::game();
            if (!g->hand_over() && g->to_act == 0 && g->street == holdem::Street::Preflop) {
                holdem_preview::you(holdem::Act::Call);
                holdem_preview::run_to_you();
                if (!g->hand_over() && g->street >= holdem::Street::Flop) break;
            }
            if (seed > 400) break;
        }
        shot(out + "_light_56_holdem_flop.ppm");
        // Play it out to a showdown (call everything)
        for (int guard = 0; guard < 50 && !holdem_preview::game()->hand_over(); ++guard) {
            holdem_preview::you(holdem::Act::Call);
            holdem_preview::run_to_you();
        }
        shot(out + "_light_56_holdem_end.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("holdem"));
        shot(out + "_dark_56_holdem.ppm");
        kit_preview_menu();
        shot(out + "_dark_57_holdem_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Nine Men's Morris: placing, a mill asking for a capture, moving
        ui::app_open_game_now(games::find("morris"));
        shot(out + "_light_62_morris_new.ppm");
        morris::Game* g = morris_preview::game();
        if (g) {
            auto code = [](int from, int to, int remove) { morris::Move m; m.from = int8_t(from); m.to = int8_t(to); m.remove = int8_t(remove); return m.code(); };
            const int placing[] = {4, 10, 7, 13, 19, 22, 3};       // White 4 7 19 3, Black 10 13 22
            for (int pt : placing) g->play(code(-1, pt, -1));
            // Black to place: Black 16? make it White's turn with a mill ready at 5
            g->play(code(-1, 16, -1));                            // Black
            match::refresh();
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("morris"));
            shot(out + "_light_62_morris_placing.ppm");
            morris_preview::tap_point(5);                         // 3-4-5: a mill
            shot(out + "_light_62_morris_take.ppm");
            // The moving phase: a full board, White picks a man
            *g = morris::Game{};
            g->pos.hand[0] = g->pos.hand[1] = 0;
            g->pos.men[0] = (1u << 0) | (1u << 1) | (1u << 4) | (1u << 9) | (1u << 11) | (1u << 16) | (1u << 20);
            g->pos.men[1] = (1u << 3) | (1u << 6) | (1u << 13) | (1u << 14) | (1u << 17) | (1u << 22) | (1u << 23);
            g->plies = 30;
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("morris"));
            morris_preview::tap_point(16);
            shot(out + "_light_62_morris_move.ppm");
        }
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("morris"));
        shot(out + "_dark_62_morris.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Mancala: the start, a game part-way (mid-sowing and settled), dark
        ui::app_open_game_now(games::find("mancala"));
        shot(out + "_light_60_mancala_new.ppm");
        mancala::Board* b = mancala_preview::board();
        if (b) {
            const int opening[] = {2, 5, 1, 3, 4, 0, 2};
            for (int m : opening) {
                if (b->over() || !b->can_play(m)) continue;
                const int side = b->side;
                b->play(m);
                (void)side;
            }
            // One more through the controller so it animates, caught mid-way
            if (match::human_may_move()) {
                int most = -1;                                // the fullest pit: a long sowing
                for (int p = 0; p < 6; ++p) if (b->can_play(p) && (most < 0 || b->pit[p] > b->pit[most])) most = p;
                match::human_move(most);
                run(3 * 130 + 40);
                shot(out + "_light_60_mancala_sowing.ppm");
                run(2000);
            }
            mancala_preview::finish();
            shot(out + "_light_60_mancala.ppm");
            mid_saves["mancala"] = files["mancala"];
        }
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("mancala"));
        shot(out + "_dark_60_mancala.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // You Sank My CYD!: shuffling a fleet, their waters part-way, my fleet, dark, pass-and-play's cover
        ui::app_open_game_now(games::find("sank"));
        run(30);
        shot(out + "_light_63_sank_new.ppm");                       // Manual: the Carrier first
        sank::Board* b = sank_preview::board();
        if (b) {
            // Manual placement: the Carrier's end at H3, two from the edge; aimed right
            // it slides in to F3-J3
            sank_preview::tap(27);
            shot(out + "_light_63_sank_aim.ppm");
            sank_preview::tap(28);
            // Battleship B5 down to B8, Cruiser A6 down, Submarine D8 across, Destroyer touching it
            const int ends[3][2] = {{41, 71}, {50, 70}, {73, 75}};
            for (auto& e : ends) { sank_preview::tap(e[0]); sank_preview::tap(e[1]); }
            sank_preview::tap(86);
            shot(out + "_light_63_sank_place.ppm");
            sank_preview::tap(87);
            shot(out + "_light_63_sank_placed.ppm");
            match::open_menu();
            shot(out + "_light_63_sank_menu.ppm");
            sank_preview::options();
            shot(out + "_light_63_sank_options.ppm");
            ui::close_overlays();
            sank_preview::ready();
            run(3000);                                    // the computer places its ships
            // A few misses, the Destroyer sunk, a hit on the Carrier
            const sank::Fleet& f = b->fleet[1];
            std::vector<int> aim;
            for (int c = 0; c < sank::kCells && aim.size() < 3; c += 7)
                if (!f.at[c] && b->can_play(uint32_t(c))) aim.push_back(c);
            const sank::Ship& d = f.ship[4];
            aim.push_back(d.cell_at(0, 2));
            aim.push_back(d.cell_at(1, 2));
            aim.push_back(f.ship[0].cell_at(2, 5));
            // Each shot: the shell falling, then the splash or the explosion with its banner
            for (size_t k = 0; k < aim.size(); ++k) {
                const int c = aim[k];
                for (int w = 0; w < 30 && !(match::human_may_move() && sank_preview::idle()); ++w) run(300);
                if (!b->can_play(uint32_t(c))) continue;
                match::human_move(c);
                if (k == 0) { run(450); shot(out + "_light_63_sank_fall.ppm"); }
                if (k == 0 || k == 4) { run(k == 0 ? 750 : 1200); shot(out + (k == 0 ? "_light_63_sank_miss.ppm" : "_light_63_sank_hit.ppm")); }
                if (k == 4) { sank_preview::page(1); }
                run(4500);                                   // the turn goes to the computer, then back
                if (k == 3) {                                // the computer's shot on your fleet
                    for (int w = 0; w < 10 && !sank_preview::shooting(); ++w) run(200);
                    run(1300);
                    shot(out + "_light_63_sank_incoming.ppm");
                }
            }
            for (int w = 0; w < 30 && !(match::human_may_move() && sank_preview::idle()); ++w) run(300);
            sank_preview::page(0);
            shot(out + "_light_63_sank.ppm");
            sank_preview::page(1);
            shot(out + "_light_63_sank_fleet.ppm");
            sank_preview::page(0);
            ui::app_save_current();
            mid_saves["sank"] = files["sank"];
        }
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("sank"));
        shot(out + "_dark_63_sank.ppm");
        match::state().mode = twoplayer::Mode::PassAndPlay;
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        ui::app_open_game_now(games::find("sank"));
        shot(out + "_light_63_sank_pass.ppm");
        if (sank::Board* p = sank_preview::board()) {     // Blue fires: the result, then Pass to Gold
            sank_preview::cover_ready();
            for (int c = 0; c < sank::kCells; ++c)
                if (p->can_play(uint32_t(c)) && p->fleet[1].at[c]) { match::human_move(c); break; }
            shot(out + "_light_63_sank_passed.ppm");
            // ... and on to the end: Blue sinks the rest
            for (int guard = 0; guard < 400 && p->result() == -1; ++guard) {
                int c = 0;
                if (p->turn() == 0) { while (c < sank::kCells && !(p->can_play(uint32_t(c)) && p->fleet[1].at[c])) ++c; }
                else { while (c < sank::kCells && !p->can_play(uint32_t(c))) ++c; }
                sank_preview::cover_ready();
                match::human_move(c);
                sank_preview::pass();
            }
            run(100);
            shot(out + "_light_63_sank_won.ppm");
        }
        ui::app_go_home_now();
        files["sank"] = mid_saves["sank"];
    }

    {   // Wheel of CYD: the start, the wheel turning, picking a consonant, letters showing, solving, a round won
        ui::app_open_game_now(games::find("wheel"));
        run(30);
        shot(out + "_light_27_wheel_new.ppm");
        wheel::Game* g = wheel_preview::game();
        bool spun_shot = false, pick_shot = false, reveal_shot = false;
        for (int tries = 0; g && tries < 40 && !reveal_shot; ++tries) {
            if (g->turn != 0 || g->phase == wheel::Phase::RoundOver) { run(1000); continue; }
            if (g->phase == wheel::Phase::Choose && g->can_spin()) {
                wheel_preview::action(0);                            // Spin
                run(1100);
                if (!spun_shot) { shot(out + "_light_27_wheel_spin.ppm"); spun_shot = true; }
                run(7500);
            }
            if (g->turn == 0 && g->phase == wheel::Phase::Consonant) {
                if (!pick_shot) { shot(out + "_light_27_wheel_pick.ppm"); pick_shot = true; }
                char c = 0;                                         // a consonant that is there
                for (const char* t = g->text(); *t && !c; ++t)
                    if (wheel::is_letter(*t) && !wheel::is_vowel(*t) && !g->called_letter(*t)) c = *t;
                wheel_preview::key(c);
                run(g->count(c) > 1 ? 400 : 120);
                shot(out + "_light_27_wheel_reveal.ppm");
                reveal_shot = true;
                run(1500);
            }
        }
        // Solving: a couple of letters typed in
        if (g && g->turn == 0 && g->phase == wheel::Phase::Choose) {
            wheel_preview::action(2);
            char want[64];
            int n = 0;
            for (const char* t = g->text(); *t; ++t) if (wheel::is_letter(*t) && !g->called_letter(*t)) want[n++] = *t;
            for (int k = 0; k < n && k < 3; ++k) wheel_preview::key(want[k]);
            shot(out + "_light_27_wheel_solve.ppm");
            for (int k = 3; k < n; ++k) wheel_preview::key(want[k]);
            wheel_preview::action(2);                              // Solve
            run(300);
            shot(out + "_light_27_wheel_solved.ppm");
            wheel_preview::action(2);                              // Next Round
            run(200);
            for (int k = 0; k < 40 && g->turn != 0; ++k) run(1000);   // the computers play
            shot(out + "_light_27_wheel_round2.ppm");
        }
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("wheel"));
        shot(out + "_dark_27_wheel.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Ultimate Tic-Tac-Toe and Gomoku: a game part-way against the computer, dark
        for (const char* id : {"ultimate", "gomoku"}) {
            ui::app_open_game_now(games::find(id));
            run(30);
            const bool ult = id[0] == 'u';
            // You (X / Black) play toward the middle; the computer answers
            for (int k = 0; k < (ult ? 14 : 4); ++k) {
                for (int w = 0; w < 30 && !match::human_may_move(); ++w) run(500);
                if (!match::human_may_move()) break;
                static int lm[256];
                const int n = match::legal_moves(lm, 256);
                if (n <= 0) break;
                int pick = lm[0];
                if (ult) pick = lm[(k * 7) % n];
                else {                                       // near the centre, a diagonal line
                    const int want[9] = {112, 96, 128, 80, 144, 98, 126, 110, 114};
                    for (int i = 0; i < n; ++i) if (lm[i] == want[k]) pick = want[k];
                    if (pick != want[k]) for (int i = 0; i < n; ++i) if (lm[i] / 15 >= 5 && lm[i] / 15 <= 9) { pick = lm[i]; break; }
                }
                match::human_move(pick);
                run(400);
            }
            for (int w = 0; w < 30 && !match::human_may_move(); ++w) run(500);
            shot(out + "_light_64_" + id + ".ppm");
            ui::app_save_current();
            mid_saves[id] = files[id];
            ui::app_go_home_now();
            ui::app_set_theme(ui::Theme::Dark);
            ui::app_open_game_now(games::find(id));
            shot(out + "_dark_64_" + id + ".ppm");
            ui::app_go_home_now();
            ui::app_set_theme(ui::Theme::Light);
        }
    }

    {   // Farkle: the start, a roll with scoring dice picked, a Farkle, the menu
        ui::app_open_game_now(games::find("farkle"));
        shot(out + "_light_58_farkle_new.ppm");
        farkle::Game* g = farkle_preview::game();
        if (g) {
            farkle::Rng rng(11);
            for (int t = 0; t < 40; ++t) {                    // a roll with a set to pick
                *g = farkle::Game{};
                g->roll(rng);
                uint8_t m;
                if (g->phase == farkle::Phase::Rolled && farkle::best_set(g->dice, 6, &m) >= 300 && m != 0x3F) {
                    for (int i = 0; i < 6; ++i) if ((m >> i) & 1) g->toggle(i);
                    break;
                }
            }
            g->score[0] = 2350; g->score[1] = 3100; g->turn_score = 0;
            farkle_preview::refresh();
            shot(out + "_light_58_farkle_pick.ppm");
            for (int t = 0; t < 200; ++t) {                   // a Farkle
                *g = farkle::Game{};
                g->score[0] = 2350; g->score[1] = 3100;
                g->roll(rng);
                if (g->phase == farkle::Phase::Farkle) break;
            }
            farkle_preview::refresh();
            shot(out + "_light_58_farkle_farkle.ppm");
        }
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("farkle"));
        shot(out + "_dark_58_farkle.ppm");
        kit_preview_menu();
        shot(out + "_dark_59_farkle_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // RPG Dice: the sample preset, a big pool, every die once, the screens
        ui::app_open_game_now(games::find("rpgdice"));
        shot(out + "_light_52_rpgdice_new.ppm");
        const int every[8] = {1, 1, 1, 1, 1, 1, 1, 1};
        rpgdice_preview::set_pool(every, 0);
        rpgdice_preview::roll_pool(11);
        shot(out + "_light_52_rpgdice_every.ppm");
        const int sixd6[8] = {0, 0, 6, 0, 0, 0, 0, 0};
        rpgdice_preview::set_pool(sixd6, 0);
        rpgdice_preview::roll_pool(5);
        shot(out + "_light_52_rpgdice_6d6.ppm");
        const int one12[8] = {0, 0, 0, 0, 0, 1, 0, 0};
        rpgdice_preview::set_pool(one12, 0);
        rpgdice_preview::roll_pool(3);
        shot(out + "_light_52_rpgdice_d12.ppm");
        const int mix[8] = {0, 0, 2, 1, 0, 0, 1, 0};
        rpgdice_preview::set_pool(mix, 3);
        shot(out + "_light_52_rpgdice_pool.ppm");
        for (uint32_t seed = 1; seed < 400; ++seed) {           // a roll with a natural 20
            rpgdice_preview::roll_preset(0, seed);
            if (seed == 399) break;
        }
        rpgdice_preview::roll_preset(0, 77);
        shot(out + "_light_52_rpgdice_preset.ppm");
        rpgdice_preview::presets();
        shot(out + "_light_53_rpgdice_presets.ppm");
        rpgdice_preview::editor(0);
        shot(out + "_light_53_rpgdice_editor.ppm");
        rpgdice_preview::line_editor(0, 1);
        shot(out + "_light_53_rpgdice_line.ppm");
        rpgdice_preview::keyboard(0);
        shot(out + "_light_53_rpgdice_keyboard.ppm");
        rpgdice_preview::history();
        shot(out + "_light_53_rpgdice_history.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("rpgdice"));
        shot(out + "_dark_52_rpgdice.ppm");
        const int big[8] = {0, 2, 3, 2, 2, 2, 2, 1};
        rpgdice_preview::set_pool(big, -1);
        rpgdice_preview::roll_pool(9);
        shot(out + "_dark_52_rpgdice_many.ppm");
        kit_preview_menu();
        shot(out + "_dark_53_rpgdice_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Minesweeper: Medium in progress (light), Hard lost (dark), Easy cleared
        auto stage = [&](int level, int first, int stop_after, bool flags, bool lose, uint16_t secs) {
            mines::Board b; b.start(level);
            mines::Rng rng(17);
            b.open(first, rng);
            int opened = 0;
            for (int i = 0; i < b.cells() && (stop_after < 0 || opened < stop_after); ++i)
                if (!b.mine[i] && b.cell[i] == mines::Cell::Hidden) {
                    int nb[8]; const int n = b.neighbors(i, nb);
                    bool frontier = false;
                    for (int k = 0; k < n; ++k) frontier |= b.cell[nb[k]] == mines::Cell::Open;
                    if (frontier || stop_after < 0) { b.open(i, rng); ++opened; }
                }
            int flagged = 0, wrong = 0;
            for (int i = 0; i < b.cells() && flags; ++i) {
                if (b.cell[i] != mines::Cell::Hidden) continue;
                int nb[8]; const int n = b.neighbors(i, nb);
                bool frontier = false;
                for (int k = 0; k < n; ++k) frontier |= b.cell[nb[k]] == mines::Cell::Open;
                if (!frontier) continue;
                if (b.mine[i] && flagged < 4) { b.toggle_flag(i); ++flagged; }
                else if (!b.mine[i] && lose && wrong < 1) { b.toggle_flag(i); ++wrong; }
            }
            if (lose)
                for (int i = 0; i < b.cells(); ++i)
                    if (b.mine[i] && b.cell[i] == mines::Cell::Hidden) { b.open(i, rng); break; }
            std::vector<uint8_t> buf(mines::Board::kSaveBytes + 6, 0);
            b.serialize(buf.data(), buf.size());
            buf[mines::Board::kSaveBytes + 1] = 1;                     // recorded
            buf[mines::Board::kSaveBytes + 2] = uint8_t(secs); buf[mines::Board::kSaveBytes + 3] = uint8_t(secs >> 8);
            save_game("minesweeper", buf.data(), buf.size());
            ui::app_open_game_now(games::find("minesweeper"));
        };
        stage(1, 45, 18, true, false, 107);
        shot(out + "_light_32_minesweeper.ppm");
        kit_preview_menu();
        shot(out + "_light_33_minesweeper_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(2, 60, 12, true, true, 95);
        shot(out + "_dark_32_minesweeper_lost.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        stage(0, 30, -1, false, false, 64);
        shot(out + "_light_34_minesweeper_won.ppm");
        ui::app_go_home_now();
    }

    {   // Hollywood CYDs: the board part-way, a star's answer to judge, a bluff revealed, dark
        using namespace hcyd;
        remove_game("hollywood");
        ui::app_open_game_now(games::find("hollywood"));
        run(30);
        Board* b = hollywood_preview::board();
        if (b) {
            for (int k = 0; k < 40 && b->result() < 0 && !(b->count(0) + b->count(1) >= 4 && match::human_may_move() && b->phase == Phase::Pick); ++k) {
                for (int w = 0; w < 40 && !match::human_may_move(); ++w) run(250);
                run(3000);
                if (!match::human_may_move() || b->result() >= 0) break;
                if (b->phase == Phase::Pick && b->count(0) + b->count(1) >= 4) break;
                match::human_move(best_move(*b, 1, 99 + k));
            }
            run(6000);
            hollywood_preview::sync();
            shot(out + "_light_47_hollywood.ppm");
            for (int s = 0; s < kSquares; ++s) if (b->owner[s] < 0 && b->phase == Phase::Pick && match::human_may_move()) { match::human_move(s); break; }
            run(300);
            hollywood_preview::sync();
            shot(out + "_light_47_hollywood_ask.ppm");
            ui::app_set_theme(ui::Theme::Dark);
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("hollywood"));
            run(50);
            b = hollywood_preview::board();
            hollywood_preview::sync();
            shot(out + "_dark_47_hollywood.ppm");
            ui::app_set_theme(ui::Theme::Light);
            if (b && b->phase == Phase::Judge) {
                b->star_says = 1;
                match::human_move(kAgree);
                hollywood_preview::reveal(true);
                shot(out + "_light_47_hollywood_bluff.ppm");
                hollywood_preview::reveal(false);
            }
        }
        ui::app_go_home_now();
    }

    {   // Jeopar-CYD!: the board part-way, a clue with a wrong answer, a Daily Double wager, the Final, the end
        using namespace jcyd;
        remove_game("jeoparcyd");
        ui::app_open_game_now(games::find("jeoparcyd"));
        jeoparcyd_preview::hold(true);
        Game* g = jeoparcyd_preview::game();
        if (g) {
            // play 9 clues: computers' plans, you take some
            for (int k = 0; k < 9; ++k) {
                int c, r; g->pick_cell(1, &c, &r);
                if (g->cell[c][r].daily) { r = (r + 1) % kRows; if (g->cell[c][r].used || g->cell[c][r].daily) continue; }
                if (!g->pick(c, r)) continue;
                g->answer(k % 3 == 0 ? 0 : 1 + k % 2, k % 4 == 3 ? (g->right_slot() + 1) % 4 : g->right_slot());
                if (g->phase == Phase::Clue) g->time_up();
                g->done_revealing();
            }
            g->chooser = 0;
            jeoparcyd_preview::refresh();
            shot(out + "_light_46_jeoparcyd.ppm");
            ui::app_set_theme(ui::Theme::Dark);
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("jeoparcyd"));
            jeoparcyd_preview::hold(true);
            g = jeoparcyd_preview::game();
            shot(out + "_dark_46_jeoparcyd.ppm");
            ui::app_set_theme(ui::Theme::Light);
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("jeoparcyd"));
            jeoparcyd_preview::hold(true);
            g = jeoparcyd_preview::game();
            // a clue: Max was wrong
            for (int c = 0; c < kCats; ++c) for (int r = 2; r < kRows; ++r)
                if (g->phase == Phase::Board && !g->cell[c][r].used && !g->cell[c][r].daily) g->pick(c, r);
            g->answer(1, (g->right_slot() + 2) % 4);
            jeoparcyd_preview::refresh();
            jeoparcyd_preview::status_line("Max was wrong");
            shot(out + "_light_46_jeoparcyd_clue.ppm");
            g->answer(0, g->right_slot());
            jeoparcyd_preview::refresh();
            jeoparcyd_preview::status_line("You got it!");
            shot(out + "_light_46_jeoparcyd_right.ppm");
            g->done_revealing();
            // a Daily Double
            for (int c = 0; c < kCats; ++c) for (int r = 0; r < kRows; ++r)
                if (g->phase == Phase::Board && !g->cell[c][r].used && g->cell[c][r].daily) { g->chooser = 0; g->pick(c, r); }
            jeoparcyd_preview::refresh();
            shot(out + "_light_46_jeoparcyd_wager.ppm");
            // the Final
            Game f = *g;
            f.phase = Phase::Reveal;
            for (auto& col : f.cell) for (Cell& x : col) x.used = 1;
            f.round = 1;
            f.score[0] = 4200; f.score[1] = 5600; f.score[2] = -400;
            f.done_revealing();
            *g = f;
            jeoparcyd_preview::refresh();
            shot(out + "_light_46_jeoparcyd_final.ppm");
            g->final_bet(0, 2100); g->final_bet(1, 3000);
            jeoparcyd_preview::status_line("");
            jeoparcyd_preview::final_clue();
            shot(out + "_light_46_jeoparcyd_final_clue.ppm");
            g->final_answer(0, g->right_slot()); g->final_answer(1, (g->right_slot() + 1) % 4);
            jeoparcyd_preview::refresh();
            shot(out + "_light_46_jeoparcyd_over.ppm");
            jeoparcyd_preview::hold(false);
        }
        ui::app_go_home_now();
    }

    {   // Who Wants To Be A CYD?: question 7, lifelines used, an answer locked, a wrong one, the ladder
        using namespace whowants;
        remove_game("whowants");
        ui::app_open_game_now(games::find("whowants"));
        whowants_preview::hold(true);
        Game* g = whowants_preview::game();
        if (g) {
            whowants_preview::advance(6);
            shot(out + "_light_45_whowants.ppm");
            g->use(kFifty); g->use(kAudience);
            whowants_preview::refresh();
            shot(out + "_light_45_whowants_audience.ppm");
            g->use(kPhone);
            whowants_preview::refresh();
            shot(out + "_light_45_whowants_phone.ppm");
            int pick = 0;
            while (pick == g->right_slot() || (g->hidden >> pick & 1)) ++pick;
            g->lock(pick);
            whowants_preview::refresh();
            shot(out + "_light_45_whowants_locked.ppm");
            g->reveal();
            whowants_preview::refresh();
            shot(out + "_light_45_whowants_wrong.ppm");
            whowants_preview::ladder();
            shot(out + "_light_45_whowants_ladder.ppm");
            ui::close_overlays();
            ui::app_go_home_now();
            ui::app_set_theme(ui::Theme::Dark);
            remove_game("whowants");
            ui::app_open_game_now(games::find("whowants"));
            whowants_preview::hold(true);
            whowants_preview::advance(3);
            shot(out + "_dark_45_whowants.ppm");
            ui::app_set_theme(ui::Theme::Light);
            whowants_preview::hold(false);
        }
        ui::app_go_home_now();
    }

    {   // Escape from CYD: placing, part-way with a boat picked, sinking, a creature roll, dark
        using namespace escape;
        auto stage = [&](const Game& g) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 7, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes + 1] = 1;
            buf[Game::kSaveBytes + 6] = 1;
            save_game("escape", buf.data(), buf.size());
            ui::app_open_game_now(games::find("escape"));
            escape_preview::hold(true);
        };
        Game g; g.start(321);
        for (int k = 0; k < 12; ++k) g.apply(g.ai(1));
        stage(g);
        shot(out + "_light_44_escape_place.ppm");
        ui::app_go_home_now();
        for (int k = 0; k < 4000 && !(g.turns >= 12 && g.turn == 0 && g.phase == Phase::Move && g.moves_left == 3); ++k)
            if (!g.apply(g.ai(1))) break;
        stage(g);
        {   // pick a piece that can move: a boat if one is yours to move
            Action l[200]; const int n = g.actions(l, 200);
            int kind = 0, idx = -1, hex = -1;
            for (int i = 0; i < n; ++i) if (l[i].act == kStepBoat) { kind = 2; idx = l[i].who; hex = g.boat[idx]; }
            if (idx < 0) for (int i = 0; i < n; ++i) if (l[i].act == kStepExplorer) { kind = 1; idx = l[i].who; hex = g.ex[idx].hex; }
            escape_preview::select(kind, idx, hex);
        }
        escape_preview::news_line("Ada's Shark took 1!");
        shot(out + "_light_44_escape.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(g);
        shot(out + "_dark_44_escape.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game s2 = g;
        { Action e; e.act = kEndMoves; s2.apply(e); }
        stage(s2);
        shot(out + "_light_44_escape_sink.ppm");
        ui::app_go_home_now();
        Game c = s2;
        { Action l[200]; const int n = c.actions(l, 200); if (n) c.apply(l[0]); }
        c.phase = Phase::Creature; c.die = kSerpent;
        stage(c);
        escape_preview::show_banner(true);
        shot(out + "_light_44_escape_banner.ppm");
        escape_preview::show_banner(false);
        shot(out + "_light_44_escape_creature.ppm");
        ui::app_go_home_now();
    }

    {   // Sorry-CYD!: part-way, a 10 with a pawn picked; a Sorry! card; dark
        using namespace sorry;
        auto stage = [&](const Game& g) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 7, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes + 1] = 1;                    // Medium
            buf[Game::kSaveBytes + 6] = 1;                    // you against three computers
            save_game("sorrycyd", buf.data(), buf.size());
            ui::app_open_game_now(games::find("sorrycyd"));
            sorrycyd_preview::hold(true);
        };
        Game g; g.start(4242);
        for (int k = 0; k < 400 && !(g.turns >= 44 && g.turn == 0 && g.phase == Phase::Draw); ++k) {
            if (g.phase == Phase::Draw) g.draw();
            Move ms[96];
            if (g.moves(ms, 96)) g.play(g.ai_move(1)); else g.lose_turn();
        }
        Game a = g;
        a.phase = Phase::Play; a.card = 10;
        stage(a);
        int pick = -1;
        { Move ms[96]; const int n = a.moves(ms, 96); for (int i = 0; i < n; ++i) if (a.pos[0][ms[i].pawn] != kStart) pick = ms[i].pawn; }
        sorrycyd_preview::pick(pick);
        shot(out + "_light_43_sorrycyd.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(a);
        sorrycyd_preview::pick(pick);
        shot(out + "_dark_43_sorrycyd.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game b = g;
        b.phase = Phase::Play; b.card = kSorry;
        stage(b);
        { Move ms[96]; const int n = b.moves(ms, 96); if (n) sorrycyd_preview::pick(ms[0].pawn); }
        shot(out + "_light_43_sorrycyd_sorry.ppm");
        ui::app_go_home_now();
        Game c = g;                                           // a computer's turn, its card showing
        for (int k = 0; k < 40 && !(c.turn != 0 && c.phase == Phase::Draw); ++k) {
            if (c.phase == Phase::Draw) c.draw();
            Move ms[96];
            if (c.moves(ms, 96)) c.play(c.ai_move(1)); else c.lose_turn();
        }
        c.draw();
        stage(c);
        sorrycyd_preview::news_line("Max sent your pawn back!");
        shot(out + "_light_43_sorrycyd_cpu.ppm");
        sorrycyd_preview::people();
        shot(out + "_light_43_sorrycyd_people.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
    }

    {   // Card Sharks CYD: vs computer part-way, a miss, dark theme
        ui::app_open_game_now(games::find("cardsharks"));
        run(30);
        csh::Board* b = cardsharks_preview::board();
        if (b) {
            uint32_t rs = 91;
            bool missed = false, main_done = false;
            for (int k = 0; k < 300 && b->result() < 0 && !(missed && main_done); ++k) {
                for (int w = 0; w < 40 && !match::human_may_move(); ++w) run(250);
                run(1000);
                if (!match::human_may_move() || b->result() >= 0) break;
                const int me = match::my_side();
                if (!missed && b->last == csh::Last::Wrong && b->last_side != me) {
                    preview_press(20, 120, 80);
                    shot(out + "_light_42_cardsharks_miss.ppm");
                    missed = true;
                }
                if (!main_done && b->row[me].pos >= 2 && b->row[me ^ 1].pos >= 1 && b->last == csh::Last::Right) {
                    preview_press(20, 120, 80);
                    shot(out + "_light_42_cardsharks.ppm");
                    ui::app_set_theme(ui::Theme::Dark);
                    ui::app_go_home_now();
                    ui::app_open_game_now(games::find("cardsharks"));
                    run(100);
                    b = cardsharks_preview::board();
                    preview_press(20, 120, 80);
                    shot(out + "_dark_42_cardsharks.ppm");
                    ui::app_set_theme(ui::Theme::Light);
                    ui::app_go_home_now();
                    ui::app_open_game_now(games::find("cardsharks"));
                    run(100);
                    b = cardsharks_preview::board();
                    main_done = true;
                }
                rs = rs * 1103515245u + 12345u;
                match::human_move(csh::best_move(*b, 1, rs));
            }
        }
        ui::app_go_home_now();
    }

    {   // Press Your CYD: your turn, a spin with the light on, a Gremlin
        using namespace presscyd;
        auto stage = [&](const Game& g) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            save_game("presscyd", buf.data(), buf.size());
            ui::app_open_game_now(games::find("presscyd"));
            presscyd_preview::hold();
        };
        Game g; g.start(77);
        g.p[0].money = 2750; g.p[1].money = 4100; g.p[2].money = 0; g.p[2].gremlins = 1;
        g.p[0].earned = 2; g.p[1].earned = 3; g.p[2].earned = 3; g.p[1].passed = 0;
        g.turn = 0;
        stage(g);
        presscyd_preview::news_line("Zoe hit a Gremlin!");
        shot(out + "_light_41_presscyd.ppm");
        g.spin();
        stage(g);
        g.spin();
        presscyd_preview::game()->spin();
        presscyd_preview::set_light(6);
        presscyd_preview::hold();
        shot(out + "_light_41_presscyd_spin.ppm");
        ui::app_go_home_now();
        Game h = g;
        h.phase = Phase::Ready;
        int q = 0, k = 0;
        for (int a = 0; a < kSquares; ++a) { const int b = a % kSlots; if (h.board[a][b].kind == kGremlin) { q = a; k = b; } }
        h.spin(); h.stop(q, k);
        stage(h);
        presscyd_preview::set_light(q);
        presscyd_preview::result(true);
        presscyd_preview::hold();
        shot(out + "_light_41_presscyd_gremlin.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(g);
        shot(out + "_dark_41_presscyd.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Deal or No CYD: picking, a case opened, the Banker's offer, the end
        using namespace dealcyd;
        auto stage = [&](const Game& g) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            save_game("dealcyd", buf.data(), buf.size());
            ui::app_open_game_now(games::find("dealcyd"));
        };
        Game g; g.start(2024);
        stage(g);
        shot(out + "_light_39_dealcyd_pick.ppm");
        ui::app_go_home_now();
        g.pick(11);
        const int order[] = {3, 17, 22, 0, 8, 25, 14, 5, 19, 2, 9};
        for (int c : order) if (g.phase == Phase::Open) g.open(c); else if (g.phase == Phase::Offer) { g.no_deal(); g.open(c); }
        stage(g);
        shot(out + "_light_39_dealcyd.ppm");
        dealcyd_preview::reveal(true);
        shot(out + "_light_39_dealcyd_open.ppm");
        dealcyd_preview::reveal(false);
        ui::app_go_home_now();
        Game o = g;
        for (int c = 0; c < kCases && o.phase != Phase::Offer; ++c) if (o.phase == Phase::Open) o.open(c);
        stage(o);
        shot(out + "_light_39_dealcyd_offer.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(o);
        shot(out + "_dark_39_dealcyd_offer.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        o.deal();
        stage(o);
        shot(out + "_light_39_dealcyd_done.ppm");
        ui::app_go_home_now();
    }

    {   // Strategy Go!: the setup (a piece picked to swap), play part-way, a battle, the Pieces page
        ui::app_open_game_now(games::find("strategygo"));
        run(30);
        sgo::Board* b = sgo_preview::board();
        if (b) {
            sgo_preview::pick(5);
            shot(out + "_light_38_strategygo_setup.ppm");
            sgo_preview::pick(-1);
            sgo_preview::ready();
            run(3000);                                        // the computer sets up
            uint32_t rs = 777;
            int battles = 0;
            for (int k = 0; k < 400 && b->result() < 0 && battles < 7; ++k) {
                for (int w = 0; w < 40 && !match::human_may_move(); ++w) run(250);
                if (!match::human_may_move() || b->result() >= 0) break;
                rs = rs * 1103515245u + 12345u;
                const uint32_t key = sgo::best_move(*b, 2, rs);
                match::human_move(int(key));
                if (b->last.outcome != sgo::kNoBattle) ++battles;
                run(2000);
            }
            for (int w = 0; w < 40 && !match::human_may_move(); ++w) run(250);
            run(2000);                                        // any battle banner goes
            // pick a piece that can move, so its squares light up
            for (int c = sgo::kCells - 1; c >= 0; --c) {
                uint8_t t[20];
                if (b->sq[c].side == match::my_side() && b->targets(c, t) > 0) { sgo_preview::pick(c); break; }
            }
            shot(out + "_light_38_strategygo.ppm");
            sgo_preview::pick(-1);
            if (b->last.outcome != sgo::kNoBattle) { sgo_preview::battle(true); shot(out + "_light_38_strategygo_battle.ppm"); sgo_preview::battle(false); }
            sgo_preview::pieces();
            shot(out + "_light_38_strategygo_pieces.ppm");
            ui::close_overlays();
            ui::app_set_theme(ui::Theme::Dark);
            ui::app_go_home_now();
            ui::app_open_game_now(games::find("strategygo"));
            run(100);
            shot(out + "_dark_38_strategygo.ppm");
            ui::app_set_theme(ui::Theme::Light);
        }
        ui::app_go_home_now();
    }

    {   // Acquisitions: your turn to lay a tile, buying, a merger's shares, the Stocks page, the end
        using namespace acq;
        auto stage = [&](const Game& g, const char* msg) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 7, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes] = 1;
            save_game("acquisitions", buf.data(), buf.size());
            ui::app_open_game_now(games::find("acquisitions"));
            acq_preview::hold(true);
            if (msg) acq_preview::log(msg);
            acq_preview::redraw();
        };
        Game g; g.start(77);
        for (int k = 0; k < 4000 && !(g.turns >= 26 && g.phase == Phase::Play && g.turn == 0); ++k) g.ai_act(1);
        stage(g, "Zoe buys a Harbor share");
        shot(out + "_light_37_acquisitions.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(g, "Zoe buys a Harbor share");
        shot(out + "_dark_37_acquisitions.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game b = g;
        b.ai_act(1);
        for (int k = 0; k < 20 && b.phase != Phase::Buy; ++k) b.ai_act(1);
        if (b.phase == Phase::Buy && b.turn == 0) {
            if (b.ai_buy(2) >= 0) b.buy(b.ai_buy(2));
            stage(b, "You lay a tile");
            shot(out + "_light_37_acquisitions_buy.ppm");
            ui::app_go_home_now();
        }
        stage(g, nullptr);
        acq_preview::stocks();
        shot(out + "_light_37_acquisitions_stocks.ppm");
        ui::app_go_home_now();
        // A merger where you hold shares of the smaller hotel
        Game m;
        bool found_dispose = false;
        for (uint32_t seed = 1; seed < 200 && !found_dispose; ++seed) {
            m.start(seed);
            for (int k = 0; k < 4000 && m.phase != Phase::Over; ++k) {
                if (m.phase == Phase::Dispose && m.disposer == 0) { found_dispose = true; break; }
                m.ai_act(1);
            }
        }
        if (found_dispose) {
            stage(m, "Max lays 6D: a merger!");
            shot(out + "_light_37_acquisitions_dispose.ppm");
            ui::app_go_home_now();
        }
        Game o = g;
        for (int k = 0; k < 6000 && o.phase != Phase::Over; ++k) o.ai_act(1);
        stage(o, "Ada calls the end of the game");
        shot(out + "_light_37_acquisitions_over.ppm");
        ui::app_go_home_now();
    }

    {   // Pipe Race: the countdown, the water on its way, a level cleared, the game over
        using namespace piperace;
        auto stage = [&](const Game& g) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            save_game("piperace", buf.data(), buf.size());
            ui::app_open_game_now(games::find("piperace"));
            piperace_preview::unhold();
        };
        Game g; g.start(42);
        memset(g.cell, 0, sizeof g.cell); memset(g.fill, 0, sizeof g.fill);
        const int s0 = 2 * kCols + 2;
        g.cell[s0] = kStartE; g.head = int8_t(s0);
        const uint8_t path[][2] = {{19, kAcross}, {20, kAcross}, {21, kSW}, {29, kUpDown}, {37, kCross}, {45, kWN},
                                   {44, kAcross}, {43, kNE}, {35, kES}, {36, kCross}, {38, kSW}, {46, kUpDown}};
        for (auto& pc : path) g.cell[pc[0]] = pc[1];
        g.cell[9] = kRock; g.cell[54] = kRock; g.cell[60] = kES;
        const uint8_t q[kQueue] = {kNE, kAcross, kCross, kSW, kUpDown};
        memcpy(g.queue, q, sizeof q);
        g.level = 2; g.score = 0;
        g.phase = Phase::Waiting; g.wait_ms = g.wait_total() * 2 / 5;
        stage(g);
        shot(out + "_light_36_piperace_wait.ppm");
        ui::app_go_home_now();
        // the water on its way: through the first pipes, in the cross
        g.go(); g.advance(1);
        for (int i = 0; i < 400 && !(g.head == 37 && g.progress() > 0.6f); ++i) g.advance(50);
        stage(g);
        shot(out + "_light_36_piperace.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        stage(g);
        shot(out + "_dark_36_piperace.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game p2 = g;
        for (int i = 0; i < 2000 && p2.phase == Phase::Flowing; ++i) p2.advance(100);
        stage(p2);
        if (p2.phase != Phase::Passed) fprintf(stderr, "PIPERACE STAGE: level not passed\n");
        shot(out + "_light_36_piperace_passed.ppm");
        ui::app_go_home_now();
        Game o = g; o.cell[38] = kEmpty;
        for (int i = 0; i < 2000 && o.phase == Phase::Flowing; ++i) o.advance(100);
        stage(o);
        shot(out + "_light_36_piperace_over.ppm");
        ui::app_go_home_now();
    }

    {   // 2048: a game in progress (light), the four tap zones checked
        // against the rules engine, a finished game (dark)
        using namespace twenty48;
        auto stage = [&](const Game& g, uint32_t secs) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            for (int k = 0; k < 4; ++k) buf[Game::kSaveBytes + 1 + k] = uint8_t(secs >> (8 * k));
            save_game("twenty48", buf.data(), buf.size());
            ui::app_open_game_now(games::find("twenty48"));
        };
        Game mid;
        const uint8_t cells[kCells] = {1, 0, 2, 1,  0, 3, 1, 0,  2, 4, 5, 2,  7, 6, 8, 9};
        for (int i = 0; i < kCells; ++i) mid.cell[i] = cells[i];
        mid.score = 3112; mid.moves = 241; mid.spawned = 3;
        stage(mid, 512);
        shot(out + "_light_35_twenty48.ppm");
        // Tap points: just under the top bar, far left, bottom, far right
        const int pts[4][2] = {{W / 2, H * 13 / 100}, {6, H / 2}, {W / 2, H - 6}, {W - 6, H / 2}};
        const Dir dirs[4] = {Dir::Up, Dir::Left, Dir::Down, Dir::Right};
        for (int k = 0; k < 4; ++k) {
            ui::app_go_home_now();
            stage(mid, 512);
            run(100);
            preview_press(pts[k][0], pts[k][1], 60);
            ui::app_go_home_now();                       // saves
            Game got, want = mid;
            got.deserialize(files["twenty48"].data(), files["twenty48"].size());
            Rng rng(seed());
            want.slide(dirs[k], rng);
            if (memcmp(got.cell, want.cell, kCells) != 0 || got.moves != want.moves)
                fprintf(stderr, "2048 ZONE FAIL: tap %d,%d did not slide %d (moves %u want %u, cell0 %u want %u)\n", pts[k][0], pts[k][1], k, got.moves, want.moves, got.cell[0], want.cell[0]);
        }
        ui::app_set_theme(ui::Theme::Dark);
        Game done;
        const uint8_t full[kCells] = {1, 2, 3, 4,  5, 6, 7, 8,  9, 10, 11, 1,  2, 3, 4, 5};
        for (int i = 0; i < kCells; ++i) done.cell[i] = full[i];
        done.score = 27460; done.moves = 1290; done.won = true;
        stage(done, 2210);
        shot(out + "_dark_35_twenty48_over.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // MasterCYD: Normal in progress (light), Hard solved (dark)
        using namespace mastercyd;
        auto stage = [&](const Game& g, uint32_t secs) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes] = g.over() ? 1 : 0;
            for (int k = 0; k < 4; ++k) buf[Game::kSaveBytes + 1 + k] = uint8_t(secs >> (8 * k));
            save_game("mastercyd", buf.data(), buf.size());
            ui::app_open_game_now(games::find("mastercyd"));
        };
        Game g; Rng rng(11); g.start(1, rng);
        const uint8_t tries[4][4] = {{0, 0, 1, 1}, {2, 2, 3, 3}, {0, 2, 4, 4}, {5, 0, 2, 1}};
        for (auto& tr : tries) { for (int i = 0; i < 4; ++i) g.place(tr[i]); g.submit(); }
        g.place(g.secret[0]); g.place(3);
        stage(g, 154);
        shot(out + "_light_36_mastercyd.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        Game h; Rng r2(29); h.start(2, r2);
        const uint8_t t5[3][5] = {{0, 1, 2, 3, 4}, {1, 1, 5, 5, 0}, {2, 3, 0, 4, 1}};
        for (auto& tr : t5) { for (int i = 0; i < 5; ++i) h.place(tr[i]); h.submit(); }
        for (int i = 0; i < 5; ++i) h.place(h.secret[i]);
        h.submit();
        stage(h, 402);
        shot(out + "_dark_36_mastercyd_solved.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Peg Solitaire: English part-way with a peg picked (light), Triangle (dark), European
        using namespace pegs;
        auto stage = [&](const Game& g, uint32_t secs) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            for (int k = 0; k < 4; ++k) buf[Game::kSaveBytes + 1 + k] = uint8_t(secs >> (8 * k));
            save_game("pegs", buf.data(), buf.size());
            ui::app_open_game_now(games::find("pegs"));
        };
        Game g; g.start(English);
        const uint8_t eng[][2] = {{10,24},{15,17},{2,16},{4,2},{17,15},{14,16},{18,4},{20,18},{23,9}};
        for (auto& mv : eng) g.play(mv[0], mv[1]);
        stage(g, 131);
        run(50);
        int x = 0, y = 0;
        {   // tap the peg on hole 30 (row 4, col 2)
            const int m = W >= 320 ? 8 : 4;
            (void)m;
        }
        (void)x; (void)y;
        shot(out + "_light_37_pegs.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        Game t; t.start(Triangle);
        t.play(14, 0); t.play(16, 14);
        stage(t, 20);
        shot(out + "_dark_37_pegs_triangle.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game e; e.start(European);
        stage(e, 0);
        shot(out + "_light_37_pegs_european.ppm");
        ui::app_go_home_now();
    }

    {   // Memory Match: 4x5 with pairs found and a miss showing (light), 5x6 (dark)
        using namespace memory;
        auto stage = [&](const Game& g, uint32_t secs) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            for (int k = 0; k < 4; ++k) buf[Game::kSaveBytes + 1 + k] = uint8_t(secs >> (8 * k));
            save_game("memory", buf.data(), buf.size());
            ui::app_open_game_now(games::find("memory"));
        };
        Game g; Rng rng(9); g.start(1, rng);
        int done = 0;
        for (int a = 0; a < g.tiles() && done < 4; ++a)
            for (int b = a + 1; b < g.tiles(); ++b)
                if (!g.matched[a] && g.pic[a] == g.pic[b]) { g.tap(a); g.tap(b); ++done; break; }
        int x = 0; while (g.matched[x]) ++x;
        int y = x + 1; while (g.matched[y] || g.pic[y] == g.pic[x]) ++y;
        g.tap(x); g.tap(y);
        g.turns = 9;
        stage(g, 74);
        shot(out + "_light_38_memory.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        Game h; Rng r2(3); h.start(2, r2);
        for (int a = 0, n = 0; a < h.tiles() && n < 9; ++a)
            for (int b = a + 1; b < h.tiles(); ++b)
                if (!h.matched[a] && h.pic[a] == h.pic[b]) { h.tap(a); h.tap(b); ++n; break; }
        h.tap(h.tiles() - 1);
        stage(h, 140);
        shot(out + "_dark_38_memory.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Nonograms: 10x10 part-way (light), 8x8 (dark), 5x5 solved
        using namespace nonogram;
        auto stage = [&](const Game& g, uint32_t secs, bool mark) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 6, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes] = mark ? 1 : 0;
            buf[Game::kSaveBytes + 1] = g.solved() ? 1 : 0;
            for (int k = 0; k < 4; ++k) buf[Game::kSaveBytes + 2 + k] = uint8_t(secs >> (8 * k));
            save_game("nonogram", buf.data(), buf.size());
            ui::app_open_game_now(games::find("nonogram"));
        };
        Game g; Rng rng(77); g.start(2, rng);
        for (int r = 0; r < 6; ++r)
            for (int c = 0; c < g.n; ++c) {
                if (g.picture[r] >> c & 1) { if ((r + c) % 3) g.tap(r, c, Filled); }
                else if (r < 3) g.tap(r, c, Marked);
            }
        stage(g, 251, false);
        run(30);
        shot(out + "_light_39_nonogram.ppm");
        mid_saves["nonogram"] = files["nonogram"];
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        Game h; Rng r2(5); h.start(1, r2);
        for (int r = 0; r < 4; ++r) for (int c = 0; c < h.n; ++c) if (h.picture[r] >> c & 1) h.tap(r, c, Filled);
        stage(h, 96, true);
        shot(out + "_dark_39_nonogram.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        Game s; Rng r3(2); s.start(0, r3);
        for (int r = 0; r < s.n; ++r) for (int c = 0; c < s.n; ++c) if (s.picture[r] >> c & 1) s.tap(r, c, Filled);
        stage(s, 48, false);
        shot(out + "_light_39_nonogram_solved.ppm");
        ui::app_go_home_now();
    }

    {   // Solitaire: a deal part-way with a card picked (light), Options, the win show (dark)
        using namespace solitaire;
        auto stage = [&](const Game& g, uint32_t secs, uint8_t scoring_opt) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 11, 0);
            g.serialize(buf.data(), buf.size());
            uint8_t* q = buf.data() + Game::kSaveBytes;
            for (int k = 0; k < 4; ++k) q[1 + k] = uint8_t(secs >> (8 * k));
            q[5] = g.draw; q[6] = scoring_opt;
            save_game("solitaire", buf.data(), buf.size());
            ui::app_open_game_now(games::find("solitaire"));
        };
        Game* g = new Game();
        g->deal(20261003, 3, Scoring::Standard);
        for (int step = 0; step < 40; ++step) {
            int f, i, to;
            if (!g->hint(&f, &i, &to)) break;
            if (f == Stock) g->draw_stock(); else g->move(f, i, to);
        }
        stage(*g, 223, 0);
        run(30);
        shot(out + "_light_45_solitaire.ppm");
        mid_saves["solitaire"] = files["solitaire"];
        kit_preview_menu();
        shot(out + "_light_45_solitaire_menu.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        // Options screen
        stage(*g, 223, 0);
        kit_preview_menu();
        press_overlay_key("Options");
        run(30);
        shot(out + "_light_45_solitaire_options.ppm");
        press_overlay_key("Card Back");
        run(30);
        shot(out + "_light_45_card_back.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        {   // Options -> Draw 1 must change the deal being played (Tom found it didn't)
            stage(*g, 223, 0);
            kit_preview_menu();
            press_overlay_key("Options");
            press_overlay_key("Draw 1");
            run(20);
            ui::close_overlays();
            ui::app_go_home_now();
            Game* chk = new Game();
            chk->deserialize(files["solitaire"].data(), files["solitaire"].size());
            if (chk->draw != 1) fprintf(stderr, "SOLITAIRE DRAW FAIL: draw %d after picking Draw 1\n", chk->draw);
            const uint8_t* q = files["solitaire"].data() + Game::kSaveBytes;
            if (q[5] != 1) fprintf(stderr, "SOLITAIRE DRAW FAIL: option %d\n", q[5]);
            delete chk;
        }
        // A deal one step from done: foundations to Queen, the Kings on columns
        ui::app_set_theme(ui::Theme::Dark);
        Game* w = new Game();
        w->deal(1, 3, Scoring::Standard);
        for (auto& s : w->pile) s = Stack{};
        for (int f = 0; f < 4; ++f) {
            for (int r = 1; r <= 12; ++r) w->pile[Found0 + f].c[w->pile[Found0 + f].n++] = uint8_t(kFoundSuit[f] * 13 + r - 1);
            w->pile[Tab0 + f].c[0] = uint8_t(kFoundSuit[f] * 13 + 12);
            w->pile[Tab0 + f].n = 1;
        }
        w->score = 640; w->moves = 151;
        stage(*w, 412, 0);
        run(800);                           // the last cards go up, then the show starts
        shot(out + "_dark_45_solitaire_win.ppm", true);
        run(2500);
        shot(out + "_dark_45_solitaire_win2.ppm", true);
        preview_press(W / 2, H / 2, 60);    // a tap ends it
        run(100);
        shot(out + "_dark_45_solitaire_won.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        delete g; delete w;
    }

    {   // Golf and Pyramid part-way (light), Pyramid with a pick (dark)
        {
            using namespace golf;
            Game g; g.deal(4242);
            for (int k = 0; k < 9; ++k) { const int h = g.hint(); if (h < 0) break; if (h == 7) g.draw(); else g.play(h); }
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes + 1] = 140;
            save_game("golf", buf.data(), buf.size());
            ui::app_open_game_now(games::find("golf"));
            shot(out + "_light_46_golf.ppm");
            ui::app_go_home_now();
        }
        {
            using namespace pyramid;
            Game g; g.deal(31337);
            for (int k = 0; k < 6; ++k) { int a, b; if (!g.hint(&a, &b)) break; if (a == -2) g.draw(); else g.pair(a, b); }
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes + 1] = 95;
            save_game("pyramid", buf.data(), buf.size());
            ui::app_open_game_now(games::find("pyramid"));
            shot(out + "_light_47_pyramid.ppm");
            ui::app_go_home_now();
            ui::app_set_theme(ui::Theme::Dark);
            ui::app_open_game_now(games::find("pyramid"));
            shot(out + "_dark_47_pyramid.ppm");
            ui::app_go_home_now();
            ui::app_set_theme(ui::Theme::Light);
        }
    }

    {   // Spider: 2 suits part-way (light), 4 suits (dark)
        using namespace spider;
        Game* g = new Game();
        for (int lv : {1, 2}) {
            g->deal(lv == 1 ? 777 : 2027, lv);
            for (int k = 0; k < 10; ++k) { int f, i, to; if (!g->hint(&f, &i, &to) || f < 0) break; g->move(f, i, to); }
            g->deal_row();
            std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
            g->serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes + 1] = 200;
            save_game("spider", buf.data(), buf.size());
            if (lv == 2) ui::app_set_theme(ui::Theme::Dark);
            ui::app_open_game_now(games::find("spider"));
            shot(out + (lv == 2 ? "_dark" : "_light") + "_48_spider.ppm");
            ui::app_go_home_now();
        }
        ui::app_set_theme(ui::Theme::Light);
        delete g;
    }

    {   // FreeCell part-way (light, then dark)
        using namespace freecell;
        Game* g = new Game();
        g->deal(1941);
        for (int k = 0; k < 12; ++k) { int f, i, to; if (!g->hint(&f, &i, &to)) break; g->move(f, i, to); }
        for (int c = 0; c < 8 && g->free_cells() > 2; ++c) g->move(Col0 + c, g->n[c] - 1, Cell0 + (4 - g->free_cells()));
        std::vector<uint8_t> buf(Game::kSaveBytes + 5, 0);
        g->serialize(buf.data(), buf.size());
        buf[Game::kSaveBytes + 1] = 180;
        save_game("freecell", buf.data(), buf.size());
        ui::app_open_game_now(games::find("freecell"));
        shot(out + "_light_49_freecell.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("freecell"));
        shot(out + "_dark_49_freecell.ppm");
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
        delete g;
    }

    {   // Blackjack: betting, a hand in play, a won round, a split (dark)
        using namespace blackjack;
        auto stage = [&](const Game& g, const char* name) {
            std::vector<uint8_t> buf(Game::kSaveBytes + 1, 0);
            g.serialize(buf.data(), buf.size());
            buf[Game::kSaveBytes] = 1;
            save_game("blackjack", buf.data(), buf.size());
            ui::app_open_game_now(games::find("blackjack"));
            shot(out + name);
            ui::app_go_home_now();
        };
        Game* g = new Game();
        g->new_shoe(5); g->bet = 25; g->chips = 480;
        stage(*g, "_light_51_blackjack_bet.ppm");
        const uint8_t rig1[] = {9, 5, 5 + 13, 12 + 26};     // you 10+6, dealer 6 + hole K
        memcpy(g->shoe, rig1, 4);
        g->deal();
        stage(*g, "_light_51_blackjack.ppm");
        g->hit();                                           // a card from the shoe
        if (g->phase == Phase::Playing) g->stand();
        stage(*g, "_light_51_blackjack_done.ppm");
        ui::app_set_theme(ui::Theme::Dark);
        g->new_shoe(9);
        const uint8_t rig2[] = {7, 9 + 13, 7 + 26, 6 + 39, 2, 12};   // a pair of 8s; dealer 10 + 7
        memcpy(g->shoe, rig2, 6);
        g->chips = 500; g->bet = 20;
        g->deal(); g->split();
        stage(*g, "_dark_51_blackjack_split.ppm");
        ui::app_set_theme(ui::Theme::Light);
        delete g;
    }

    {   // Card games: mockups of the shared card graphics (not games yet)
        static const char* const names[5][2] = {{"Cards", "Faces and backs"}, {"0:42", "Klondike"},
                                                {"1:10", "FreeCell"}, {"Bet 10", "Blackjack"},
                                                {"Credits 95", "Poker"}};
        for (int t = 0; t < 2; ++t) {
            ui::app_set_theme(t ? ui::Theme::Dark : ui::Theme::Light);
            for (int k = 0; k < 5; ++k) {
                card_mockup(k, names[k][0], names[k][1]);
                run(20);
                shot(out + (t ? "_dark" : "_light") + "_50_cards_" + std::to_string(k) + ".ppm");
            }
        }
        ui::app_set_theme(ui::Theme::Light);
        card_mockup(5, "Undo  Hint", "Score 45");
        run(20);
        shot(out + "_light_50_cards_5.ppm");
        ui::app_go_home_now();
    }

    {   // How To Play: every page of every game (light), a few also dark.
        // kit::how_to_play() reports any page whose text runs into the keys.
        for (int g = 0; g < games::count(); ++g) {
            const games::GameInfo& gi = games::get(g);
            ui::app_open_game_now(g);
            for (int p = 0; p < gi.help->count; ++p) {
                kit::how_to_play(nullptr, p);
                shot(out + "_light_40_help_" + gi.id + "_" + std::to_string(p + 1) + ".ppm");
            }
            ui::close_overlays();
            ui::app_go_home_now();
        }
        ui::app_set_theme(ui::Theme::Dark);
        ui::app_open_game_now(games::find("chess"));
        kit::how_to_play(nullptr, 0);
        shot(out + "_dark_40_help_chess_1.ppm");
        ui::close_overlays();
        ui::app_go_home_now();
        ui::app_set_theme(ui::Theme::Light);
    }

    {   // Wireless Play: the Play page, Games I'll Play, the name pages,
        // Move Timer, finding players, a player's games, requesting,
        // connecting, a game with Bob, its menu, leaving, Bob gone quiet,
        // and a request popping up over the picker
        fake_boards_begin("v0.9.0");
        char bob[24], ann[24], cy[24];
        names::format(fake_boards[0].prof.name_a, fake_boards[0].prof.name_b, bob, sizeof bob);
        names::format(fake_boards[1].prof.name_a, fake_boards[1].prof.name_b, ann, sizeof ann);
        names::format(fake_boards[2].prof.name_a, fake_boards[2].prof.name_b, cy, sizeof cy);
        ui::app_go_home_now();
        run(100);
        wplay::open_menu();
        run(50);
        shot(out + "_light_80_wl_off.ppm");
        wplay::set_two_player(true);                  // Play Mode 1P -> 2P
        run(1500);
        shot(out + "_light_80_wl_main.ppm");
        press_overlay_prefix("Games I'll Play");
        run(50);
        press_overlay_key("Mancala");                 // switch one off
        run(50);
        shot(out + "_light_80_wl_games.ppm");
        press_overlay_key("Mancala");
        ui::sysbar_back();
        press_overlay_prefix("Name:");
        run(50);
        shot(out + "_light_80_wl_name.ppm");
        press_overlay_key("Pick From List");
        run(50);
        shot(out + "_light_80_wl_words1.ppm");
        for (int k = 0; k < 12 && !press_overlay_key("Goofy"); ++k) { press_overlay_key(LV_SYMBOL_RIGHT); run(20); }
        run(50);
        shot(out + "_light_80_wl_words2.ppm");
        for (int k = 0; k < 12 && !press_overlay_key("Penguin"); ++k) { press_overlay_key(LV_SYMBOL_RIGHT); run(20); }
        run(50);
        if (strcmp(wplay::my_name(), "Goofy Penguin") != 0) fprintf(stderr, "WIRELESS FAIL: the name is %s, not Goofy Penguin\n", wplay::my_name());
        ui::sysbar_back();
        press_overlay_prefix("Move Timer");
        run(50);
        shot(out + "_light_80_wl_timer.ppm");
        press_overlay_key("1 Minute");
        run(50);
        press_overlay_prefix("Find Players");
        run(200);
        shot(out + "_light_80_wl_players.ppm");
        press_overlay_prefix(cy);
        run(50);
        shot(out + "_light_80_wl_player_update.ppm");
        ui::sysbar_back();
        fake_boards[0].accept = false;
        press_overlay_prefix(bob);
        run(50);
        shot(out + "_light_80_wl_player.ppm");
        press_overlay_key("Chess");
        run(600);
        shot(out + "_light_80_wl_asking.ppm");
        fake_boards[0].accept = true;
        run(1500);
        if (ui::app_current_game() != games::find("chess")) fprintf(stderr, "WIRELESS FAIL: chess didn't open\n");
        shot(out + "_light_80_wl_start.ppm");
        if (fake_boards[0].chess) {   // a few moves each (Bob, the asked player, moves first)
            const char* mine[3] = {"e7e5", "b8c6", "g8f6"};
            for (const char* mv : mine) {
                for (int t = 0; t < 400 && !match::human_may_move(); ++t) run(10);
                chess::Game probe;              // this board's position, from its save
                ui::app_save_current();
                probe.deserialize(files["chess"].data(), chess::Game::kSaveBytes);
                const int k = chess_index(probe, mv);
                chess::MoveList l;
                probe.legal(l);
                if (k >= 0) match::human_move(int(chess::move_key(l.m[k])));
                else fprintf(stderr, "WIRELESS STAGING: %s not legal\n", mv);
            }
            run(1500);
            if (!fake_boards[0].chess || fake_boards[0].chess->plies != 7) fprintf(stderr, "WIRELESS FAIL: Bob's board has %d plies\n", fake_boards[0].chess->plies);
        }
        shot(out + "_light_80_wl_game.ppm");
        wplay::open_menu();                     // the Play page during a session: Clear lit
        run(20);
        shot(out + "_light_80_wl_main_session.ppm");
        ui::close_overlays();
        run(20);
        kit_preview_menu();
        shot(out + "_light_80_wl_game_menu.ppm");
        ui::close_overlays();
        ui::sysbar_back();                       // leaving asks first
        run(20);
        shot(out + "_light_80_wl_leave.ppm");
        press_overlay_key("Keep Playing");
        fake_boards[0].on = false;              // Bob's board goes quiet
        run(3500);
        shot(out + "_light_80_wl_waiting.ppm");
        run(28000);                              // a whole move time (30 s): no reply
        shot(out + "_light_80_wl_noreply.ppm");
        press_overlay_key("Keep Waiting");
        // Bob's board comes back and he forfeits: a win here
        fake_boards[0].on = true;
        run(1000);
        fake_boards[0].link.forfeit(fake_ms);
        run(1000);
        shot(out + "_light_80_wl_forfeit.ppm");
        ui::app_go_home_now();
        fake_boards[0].in_game = false;
        run(5000);
        // Ann asks this board to play Tic-Tac-Toe: the question pops up over the picker
        fake_boards[1].accept = false;
        fake_boards[1].offer_key = netgames::by_id("tictactoe")->key;
        run(1500);
        shot(out + "_light_80_wl_offer.ppm");
        press_overlay_key("No Thanks");
        fake_boards[1].offer_key = -1;
        run(200);
        shot(out + "_light_80_wl_picker.ppm");
        (void)ann;
        net_hook = nullptr;
        for (FakeBoard& b : fake_boards) b.on = b.in_game = false;
        wplay::open_menu();
        wplay::set_two_player(false);                 // 1P again for the shots that follow
        ui::close_overlays();
        run(200);
        files.erase("chess");
    }

    {   // Left-handed: the games whose layout follows the stylus hand
        ui::settings().left_handed = true;
        for (const char* id : {"solitaire", "golf", "pyramid", "spider", "freecell", "mancala", "nonogram", "yahtcyd", "sank"}) {
            if (mid_saves.count(id)) files[id] = mid_saves[id];
            ui::app_open_game_now(games::find(id));
            run(30);
            shot(out + "_light_70_lh_" + id + ".ppm");
            ui::app_go_home_now();
        }
        ui::settings().left_handed = false;
    }

    // 5. "All games": back to the picker, which now offers the last game
    ui::app_go_home_now();
    shot(out + "_light_10_picker_after.ppm");
    fprintf(stderr, "peak LVGL heap use: %u B\n", peak_used);
    return 0;
#endif
}
