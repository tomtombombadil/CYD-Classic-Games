// Press Your CYD: registry entry, save file and screen. Rules in presscyd_core.*.
//
// Screen: 18 squares round the edge (5 across, 6 down), the three players in
// the middle (money, spins, Gremlins), one key row at the bottom: Spin |
// Pass, then STOP! while the light jumps, Next Round / Play Again. Each
// square keeps changing between its three things while the light runs.
// The light jumps every 140 ms and only the squares that change are
// redrawn, so it stays smooth on the slow panels.
//
// You play Max and Zoe; they spin, stop after a moment and sometimes pass.
// Sounds: money (Place), money + a spin (Hint), a Gremlin ("aww"), a pass
// (Turn), the end (Win / Lose). The jumping light is silent.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "presscyd_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace presscyd;
using namespace ui;

constexpr const char* kId = "presscyd";
const char* const kNames[kPlayers] = {"You", "Max", "Zoe"};
constexpr uint32_t kJumpMs = 140, kCycleMs = 700, kResultMs = 1400;

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;
lv_obj_t*   key_l = nullptr;
lv_obj_t*   key_r = nullptr;
lv_obj_t*   key_w = nullptr;
int         sq = 40, bx = 0, by = 0;
int         light = -1;                   // the lit square
uint8_t     shown[kSquares] = {};         // which of its three things each square shows
uint32_t    now_ms = 0, next_jump = 0, next_cycle = 0, result_until = 0, cpu_at = 0, cpu_stop_at = 0;
bool        showing_result = false;
char        news[48] = "";
uint32_t    last_save_ms = 0;
int         cycle_q = 0;

void open_menu();
void update();

// ---- Save / stats --------------------------------------------------------------------------
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

void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

int my_place()
{
    int place = 1;
    for (int i = 1; i < kPlayers; ++i) place += S->g.p[i].money > S->g.p[0].money;
    return place;
}

void record()
{
    Record r;
    r.place = uint8_t(my_place());
    r.money = S->g.p[0].money;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

void money(char* buf, size_t cap, int32_t v)
{
    if (v >= 1000) snprintf(buf, cap, "$%ld,%03ld", long(v / 1000), long(v % 1000));
    else snprintf(buf, cap, "$%ld", long(v));
}

// ---- Geometry ------------------------------------------------------------------------------
// Square q's place on the 5 x 6 ring, clockwise from the top left
void square_xy(int q, int* col, int* row)
{
    if (q < 5)       { *col = q; *row = 0; }
    else if (q < 9)  { *col = 4; *row = q - 4; }
    else if (q < 14) { *col = 13 - q; *row = 5; }
    else             { *col = 0; *row = 18 - q; }
}

void square_area(const lv_area_t& a, int q, int* x, int* y)
{
    int c, r;
    square_xy(q, &c, &r);
    *x = a.x1 + bx + c * sq;
    *y = a.y1 + by + r * sq;
}

// ---- Drawing -------------------------------------------------------------------------------
// The Gremlin: a green imp with pointed ears, big eyes and a grin (our own -
// nothing like the show's creature)
void draw_gremlin(lv_layer_t* layer, int cx, int cy, int r)
{
    const Palette& P = pal();
    const lv_color_t body = lv_color_mix(P.felt, P.stone_light, 200);
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = body;
    d.opa = LV_OPA_COVER;
    for (int s = -1; s <= 1; s += 2) {                       // ears
        d.p[0].x = cx + s * r * 5 / 10; d.p[0].y = cy - r * 4 / 10;
        d.p[1].x = cx + s * r * 13 / 10; d.p[1].y = cy - r * 11 / 10;
        d.p[2].x = cx + s * r * 9 / 10; d.p[2].y = cy;
        lv_draw_triangle(layer, &d);
    }
    kit::fill_circle(layer, cx, cy, r, body);
    const int er = r * 3 / 10 > 2 ? r * 3 / 10 : 2;
    for (int s = -1; s <= 1; s += 2) {
        kit::fill_circle(layer, cx + s * r * 4 / 10, cy - r / 5, er, P.stone_light);
        kit::fill_circle(layer, cx + s * r * 4 / 10 + s, cy - r / 5 + 1, er / 2 > 1 ? er / 2 : 1, P.stone_dark);
    }
    kit::fill_rect(layer, cx - r / 2, cy + r * 3 / 10, cx + r / 2, cy + r * 3 / 10 + (r / 6 > 1 ? r / 6 : 1), P.stone_dark, 2);
}

void draw_square(lv_layer_t* layer, int x, int y, int q, bool lit)
{
    const Palette& P = pal();
    const bool large = metrics().large;
    const Slot& s = S->g.board[q][shown[q]];
    const int m = 2;
    kit::fill_rect(layer, x + 1, y + 1, x + sq - 2, y + sq - 2, lit ? P.lit : P.stone_dark, 5);
    kit::fill_rect(layer, x + 1 + m, y + 1 + m, x + sq - 2 - m, y + sq - 2 - m, lit ? lv_color_mix(P.lit, P.cell, 120) : P.frame, 4);
    if (s.kind == kGremlin) { draw_gremlin(layer, x + sq / 2, y + sq / 2 + sq / 12, sq * 28 / 100); return; }
    char t[12];
    money(t, sizeof t, s.dollars);
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    const lv_color_t ink = lit ? P.stone_dark : P.stone_light;
    if (s.kind == kMoneySpin) {
        const int h = lv_font_get_line_height(f);
        kit::text(layer, t, f, ink, x, y + sq / 2 - h, sq, h);
        kit::text(layer, "+1 Spin", large ? &lv_font_montserrat_12 : &lv_font_montserrat_8, lit ? P.stone_dark : P.piece_b, x, y + sq / 2, sq, h);
    } else {
        kit::text(layer, t, f, ink, x, y, sq, sq);
    }
}

void area_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    for (int q = 0; q < kSquares; ++q) {
        int x, y;
        square_area(a, q, &x, &y);
        draw_square(layer, x, y, q, q == light);
    }
    // The middle: the round, the players, what just happened
    const int ix = a.x1 + bx + sq + 4, iy = a.y1 + by + sq + 4, iw = 3 * sq - 8, ih = 4 * sq - 8;
    kit::fill_rect(layer, ix, iy, ix + iw - 1, iy + ih - 1, P.cell, 6);
    const lv_font_t* f1 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const lv_font_t* f2 = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const lv_font_t* f3 = large ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    const int h1 = lv_font_get_line_height(f1), h2 = lv_font_get_line_height(f2), h3 = lv_font_get_line_height(f3);
    char t[48];
    snprintf(t, sizeof t, "Round %d of %d", g.round + 1, kRounds);
    kit::text(layer, t, f3, P.muted, ix, iy + 2, iw, h3);
    const int rowh = h1 + h2 + h3 + 4;
    int y = iy + h3 + 6;
    for (int i = 0; i < kPlayers; ++i) {
        const Player& pl = g.p[i];
        const bool on = g.phase != Phase::Over && g.phase != Phase::RoundOver && i == g.turn;
        if (on) kit::fill_rect(layer, ix + 3, y - 1, ix + iw - 4, y + rowh - 3, lv_color_mix(P.lit, P.cell, 110), 5);
        char m[16];
        money(m, sizeof m, pl.money);
        kit::text(layer, kNames[i], f1, pl.out() ? P.muted : P.ink, ix, y, iw, h1);
        kit::text(layer, m, f2, pl.out() ? P.muted : P.ink, ix, y + h1, iw, h2);
        if (pl.out()) snprintf(t, sizeof t, "Out");
        else if (pl.passed) snprintf(t, sizeof t, "%d spin%s (%d passed)", pl.spins(), pl.spins() == 1 ? "" : "s", pl.passed);
        else snprintf(t, sizeof t, "%d spin%s", pl.spins(), pl.spins() == 1 ? "" : "s");
        kit::text(layer, t, f3, P.muted, ix, y + h1 + h2, iw, h3);
        for (int k = 0; k < pl.gremlins; ++k)                 // a little Gremlin for each one hit
            draw_gremlin(layer, ix + iw - 10 - k * (h3 + 4), y + h1 / 2 + 2, h3 / 2 > 3 ? h3 / 2 : 3);
        y += rowh;
    }
    if (news[0] && y + h3 < iy + ih) kit::text(layer, news, f3, P.ink, ix, iy + ih - h3 - 4, iw, h3);
    // The stop's result, big over the middle
    if (showing_result && g.last.square >= 0) {
        const Slot& s = g.last.slot;
        char l1[24], l2[32];
        l2[0] = 0;
        if (s.kind == kGremlin) snprintf(l1, sizeof l1, "GREMLIN!");
        else { money(l1, sizeof l1, s.dollars); if (s.kind == kMoneySpin) snprintf(l2, sizeof l2, "and one more spin!"); }
        const lv_font_t* bf = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const int bh = lv_font_get_line_height(bf) + (l2[0] ? h3 : 0) + 12;
        const int byy = iy + (ih - bh) / 2;
        kit::fill_rect(layer, ix - 2, byy - 2, ix + iw + 1, byy + bh + 1, s.kind == kGremlin ? P.piece_a : P.lit, 8);
        kit::fill_rect(layer, ix, byy, ix + iw - 1, byy + bh - 1, P.cell, 7);
        kit::text(layer, l1, bf, s.kind == kGremlin ? P.piece_a : P.ink, ix, byy + 5, iw, lv_font_get_line_height(bf));
        if (l2[0]) kit::text(layer, l2, f3, P.ink, ix, byy + 5 + lv_font_get_line_height(bf), iw, h3);
    }
}

void invalidate_square(int q)
{
    if (!area || q < 0) return;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    int x, y;
    square_area(a, q, &x, &y);
    lv_area_t r{x, y, x + sq - 1, y + sq - 1};
    lv_obj_invalidate_area(area, &r);
}

// ---- Flow ----------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void set_key(lv_obj_t* k, bool show, const char* t, bool primary, bool dim = false)
{
    if (!k) return;
    lv_obj_set_hidden(k, !show);
    if (!show) return;
    lv_label_set_text(lv_obj_get_child(k, 0), t);
    set_checked(k, primary);
    set_dim(k, dim);
}

bool my_turn() { return S && S->g.phase != Phase::Over && S->g.phase != Phase::RoundOver && S->g.turn == 0 && !showing_result; }

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    char s[48];
    if (g.phase == Phase::Over) {
        const int pl = my_place();
        snprintf(s, sizeof s, pl == 1 ? "You win!" : "Game over");
    } else if (g.phase == Phase::RoundOver) snprintf(s, sizeof s, "Round %d over", g.round + 1);
    else if (g.turn == 0) snprintf(s, sizeof s, g.phase == Phase::Spinning ? "Press STOP!" : "Your turn");
    else snprintf(s, sizeof s, "%s's turn", kNames[g.turn]);
    kit::top_bar_status(bar, s);
    if (g.phase == Phase::Over) {
        set_key(key_l, false, "", false); set_key(key_r, false, "", false); set_key(key_w, true, "Play Again", true);
    } else if (g.phase == Phase::RoundOver) {
        set_key(key_l, false, "", false); set_key(key_r, false, "", false); set_key(key_w, true, "Next Round", true);
    } else if (g.turn == 0 && g.phase == Phase::Spinning) {
        set_key(key_l, false, "", false); set_key(key_r, false, "", false); set_key(key_w, true, "STOP!", true);
    } else if (my_turn()) {
        set_key(key_w, false, "", false);
        set_key(key_l, true, "Spin", true);
        set_key(key_r, true, "Pass", false, !g.can_pass());
    } else {
        set_key(key_l, false, "", false); set_key(key_r, false, "", false); set_key(key_w, false, "", false);
    }
    if (area) lv_obj_invalidate(area);
}

void game_over()
{
    if (!S->recorded) record();
    sound(my_place() == 1 ? Sound::Win : Sound::Lose);
    if (my_place() == 1) kit::flash();
}

void start_spin()
{
    if (!S->g.spin()) return;
    light = int(S->g.rand_next() % kSquares);
    next_jump = now_ms + kJumpMs;
    snprintf(news, sizeof news, "%s spins...", kNames[S->g.turn]);
    if (S->g.turn != 0) cpu_stop_at = now_ms + 1200 + S->g.rand_next() % 1800;
    update();
}

void do_stop()
{
    Game& g = S->g;
    const int who = g.turn;
    if (!g.stop(light, shown[light])) return;
    const Slot& s = g.last.slot;
    char m[16];
    money(m, sizeof m, s.dollars);
    if (s.kind == kGremlin) { snprintf(news, sizeof news, "%s hit a Gremlin!", kNames[who]); sound(Sound::Error); }
    else if (s.kind == kMoneySpin) { snprintf(news, sizeof news, "%s won %s + a spin", kNames[who], m); sound(Sound::Hint); }
    else { snprintf(news, sizeof news, "%s won %s", kNames[who], m); sound(Sound::Place); }
    showing_result = true;
    result_until = now_ms + kResultMs;
    save();
    update();
}

void do_pass()
{
    Game& g = S->g;
    const int who = g.turn, n = g.p[who].earned;
    if (!g.pass()) return;
    snprintf(news, sizeof news, "%s passes %d to %s", kNames[who], n, kNames[g.turn]);
    sound(Sound::Turn);
    cpu_at = now_ms + 900;
    save();
    update();
}

void new_game()
{
    kit::flash_stop();
    *S = State{};
    S->g.start(seed_now());
    light = -1;
    showing_result = false;
    news[0] = 0;
    cpu_at = now_ms + 900;
    save();
    update();
}

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open() || showing_result) return;
    lv_obj_t* k = lv_event_get_target_obj(e);
    Game& g = S->g;
    if (k == key_w) {
        if (g.phase == Phase::Over) { new_game(); return; }
        if (g.phase == Phase::RoundOver) {
            g.next_round();
            light = -1;
            news[0] = 0;
            cpu_at = now_ms + 900;
            save(); update();
            return;
        }
        if (g.phase == Phase::Spinning && g.turn == 0) do_stop();
        return;
    }
    if (!my_turn() || g.phase != Phase::Ready) return;
    if (k == key_l) start_spin();
    else if (g.can_pass()) do_pass();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int top = bar.h, h = m.h - top;
    const int room = h - kh - 3 * pad;
    sq = (m.w - 2 * pad) / 5;
    if (sq * 6 > room) sq = room / 6;
    bx = (m.w - 5 * sq) / 2;
    by = (room - 6 * sq) / 2;
    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, room);
    lv_obj_set_pos(area, 0, top + pad);
    lv_obj_add_event_cb(area, area_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    const int half = (m.w - 3 * pad) / 2, ky = m.h - pad - kh;
    key_l = make_key(scr, half, kh, key_cb, 0); key_label(key_l, "", menu_font()); lv_obj_set_pos(key_l, pad, ky);
    key_r = make_key(scr, half, kh, key_cb, 1); key_label(key_r, "", menu_font()); lv_obj_set_pos(key_r, 2 * pad + half, ky);
    key_w = make_key(scr, m.w - 2 * pad, kh, key_cb, 2); key_label(key_w, "", menu_font()); lv_obj_set_pos(key_w, pad, ky);
    clock_ = kit::Clock{};
    update();
}

// ---- Stats ---------------------------------------------------------------------------------
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
        overlay_text("No games recorded yet. Every finished game is listed here.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[12], b[12], c[12], d[16];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.wins);
        snprintf(c, sizeof c, "%lu%%", (unsigned long)(sum.wins * 100 / sum.games));
        money(d, sizeof d, sum.best);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Wins", "Won", "Best"};
        static const int8_t pct[4] = {22, 20, 22, 36};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[8], s2[16], tm[16];
            snprintf(s1, sizeof s1, "%u", unsigned(r.place));
            money(s2, sizeof s2, r.money);
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, tm, "");
        }
        const char* const head2[4] = {"Place", "Money", "Time", ""};
        static const int8_t pct2[4] = {24, 40, 36, 0};
        table_show(rt, head2, pct2, hf, hf);
    }
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.", shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    const int bh = menu_btn_h(), g2 = large ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && sum.games > 0 && shell().stats_delete_last && shell().stats_clear) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), bh);
        lv_obj_set_ignore_layout(row, true);
        lv_obj_align_to(row, back, LV_ALIGN_OUT_TOP_MID, 0, -g2);
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
    open_stats();
}

void menu_pick(int id) { if (id == kit::kLevel0) new_game(); }
void menu_back()       { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Press Your CYD", nullptr, h, false);
}

// ---- Registry entry -----------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now()); }
    for (int q = 0; q < kSquares; ++q) shown[q] = uint8_t(q % kSlots);
    light = S->g.last.square;
    if (light >= 0)                                    // the last stop shows what it landed on
        for (int k = 0; k < kSlots; ++k)
            if (S->g.board[light][k].kind == S->g.last.slot.kind && S->g.board[light][k].dollars == S->g.last.slot.dollars) shown[light] = uint8_t(k);
    showing_result = false;
    news[0] = 0;
    cpu_at = 0;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    area = key_l = key_r = key_w = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    now_ms = now;
    if (cpu_at == 0) cpu_at = now + 900;
    Game& g = S->g;
    const bool paused = overlay_open();
    // The squares keep changing (one square at a time, round and round)
    if (g.phase == Phase::Spinning && int32_t(now - next_cycle) >= 0) {
        next_cycle = now + kCycleMs / kSquares;
        shown[cycle_q] = uint8_t((shown[cycle_q] + 1) % kSlots);
        invalidate_square(cycle_q);
        cycle_q = (cycle_q + 1) % kSquares;
    }
    // The light jumps
    if (g.phase == Phase::Spinning && int32_t(now - next_jump) >= 0) {
        next_jump = now + kJumpMs;
        const int old = light;
        int q = int(g.rand_next() % kSquares);
        if (q == old) q = (q + 7) % kSquares;
        light = q;
        invalidate_square(old);
        invalidate_square(light);
    }
    if (showing_result && int32_t(now - result_until) >= 0) {
        showing_result = false;
        cpu_at = now + 700;
        if (g.phase == Phase::Over) game_over();
        update();
    }
    // The computers
    if (!paused && !showing_result && g.turn != 0) {
        if (g.phase == Phase::Ready && int32_t(now - cpu_at) >= 0) {
            if (g.ai_pass(2)) do_pass(); else start_spin();
        } else if (g.phase == Phase::Spinning && int32_t(now - cpu_stop_at) >= 0) {
            do_stop();
        }
    }
    clock_.tick(now, g.phase != Phase::Over, S->seconds);
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    char m[16];
    money(m, sizeof m, st->g.p[0].money);
    if (st->g.phase == Phase::Over) snprintf(buf, cap, "Game over: %s", m);
    else snprintf(buf, cap, "Round %d, you have %s", st->g.round + 1, m);
    return true;
}

void save_now() { save(); }

// Icon: a ring of little squares, one lit, a Gremlin in the middle
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a), c = s / 4;
    for (int i = 0; i < 12; ++i) {
        const int col = i < 4 ? i : i < 6 ? 3 : i < 10 ? 9 - i : 0, row = i < 4 ? 0 : i < 6 ? i - 3 : i < 10 ? 3 : 12 - i;
        const int x = a.x1 + col * c, y = a.y1 + row * c;
        kit::fill_rect(layer, x + 1, y + 1, x + c - 2, y + c - 2, i == 5 ? P.lit : P.frame, 3);
    }
    draw_gremlin(layer, a.x1 + s / 2, a.y1 + s / 2 + s / 16, s / 7);
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

namespace presscyd_preview {
presscyd::Game* game() { return S ? &S->g : nullptr; }
void set_light(int q) { light = q; update(); }
void result(bool on) { showing_result = on; result_until = now_ms + (on ? 600000 : 0); update(); }
void news_line(const char* t) { snprintf(news, sizeof news, "%s", t); update(); }
void hold() { cpu_at = now_ms + 600000; cpu_stop_at = now_ms + 600000; }
}

namespace games {
extern const GameOps presscyd_ops;
const GameOps presscyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
