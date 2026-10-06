// Deal or No CYD: registry entry, save file and screen. Rules in dealcyd_core.*.
//
// Screen, like the show: the amounts down both sides (the small ones left,
// the big ones right; an amount turns grey once its case is opened), the
// 26 cases in the middle, your own case in gold. Tap a case to pick yours,
// then tap cases to open them; each shows what it held for a moment. After
// each round the Banker calls: Deal | No Deal at the bottom. With one case
// left besides yours: Keep or Swap.
//
// Sounds: the Banker's call (Call), a big amount gone ("aww"), a tiny one
// gone (Trill), the end: Win if you left with at least what your case held,
// else Lose. Picking and other taps are silent.
#include <cstdio>
#include <new>
#include "dealcyd_core.h"
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace dealcyd;
using namespace ui;

constexpr const char* kId = "dealcyd";
constexpr uint32_t kRevealMs = 1300;

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
int         vw = 50, vh = 16, cw = 30, ch = 26, gx = 0, gy = 0, gap = 4, msg_y = 0;
uint32_t    now_ms = 0, reveal_until = 0, last_save_ms = 0;
bool        revealing = false;

void open_menu();
void update();

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

void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

void record()
{
    const Game& g = S->g;
    Record r;
    r.won = g.won;
    r.dealt = g.dealt;
    r.held = kValues[g.value_of[g.mine]];
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

// ---- Drawing -------------------------------------------------------------------------------
// Case i's box on screen: 4 columns, the last row's two in the middle
void case_box(const lv_area_t& a, int i, int* x, int* y)
{
    const int row = i / 4, col = i % 4;
    const int off = row == 6 ? (cw + gap) : 0;
    *x = a.x1 + gx + off + col * (cw + gap);
    *y = a.y1 + gy + row * (ch + gap);
}

void draw_case(lv_layer_t* layer, int x, int y, int i, bool mine, bool open, uint32_t value)
{
    const Palette& P = pal();
    const bool large = metrics().large;
    if (open) {
        // An opened case: an empty frame with what it held
        kit::fill_rect(layer, x, y + ch / 6, x + cw - 1, y + ch - 1, lv_color_mix(P.key_border, P.screen, 90), 4);
        kit::fill_rect(layer, x + 2, y + ch / 6 + 2, x + cw - 3, y + ch - 3, P.screen, 3);
        char m[12];
        money(m, sizeof m, value, true);
        kit::text(layer, m, large ? &lv_font_montserrat_12 : &lv_font_montserrat_8, P.muted, x, y + ch / 6, cw, ch - ch / 6);
        return;
    }
    const lv_color_t body = mine ? P.piece_b : lv_color_mix(P.stone_light, P.stone_dark, 190);
    const lv_color_t edge = P.stone_dark;
    // the handle
    const int hw = cw / 3, hh = ch / 5;
    kit::fill_rect(layer, x + (cw - hw) / 2, y, x + (cw + hw) / 2, y + hh + 2, edge, 2);
    kit::fill_rect(layer, x + (cw - hw) / 2 + 2, y + 2, x + (cw + hw) / 2 - 2, y + hh + 2, P.screen, 1);
    // the body, a clasp line, the number
    kit::fill_rect(layer, x, y + hh, x + cw - 1, y + ch - 1, edge, 4);
    kit::fill_rect(layer, x + 1, y + hh + 1, x + cw - 2, y + ch - 2, body, 3);
    kit::fill_rect(layer, x + 2, y + hh + (ch - hh) / 3, x + cw - 3, y + hh + (ch - hh) / 3, lv_color_mix(edge, body, 90));
    char n[4];
    snprintf(n, sizeof n, "%d", i + 1);
    kit::text(layer, n, large ? &lv_font_montserrat_20 : &lv_font_montserrat_12, P.stone_dark, x, y + hh, cw, ch - hh);
}

void banner(lv_layer_t* layer, const lv_area_t& a, const char* l1, const char* l2, lv_color_t edge)
{
    const Palette& P = pal();
    const bool large = metrics().large;
    const lv_font_t* f1 = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    const lv_font_t* f2 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int h1 = lv_font_get_line_height(f1), h2 = l2 && l2[0] ? lv_font_get_line_height(f2) : 0;
    const int w = lv_area_get_width(&a) - 8, bh = h1 + h2 + (large ? 18 : 12);
    const int bx = a.x1 + (lv_area_get_width(&a) - w) / 2, by = a.y1 + gy + (7 * (ch + gap) - bh) / 2;
    kit::fill_rect(layer, bx - 3, by - 3, bx + w + 2, by + bh + 2, edge, 9);
    kit::fill_rect(layer, bx, by, bx + w - 1, by + bh - 1, P.cell, 7);
    kit::text(layer, l1, f1, P.ink, bx, by + (large ? 7 : 5), w, h1);
    if (h2) kit::text(layer, l2, f2, P.muted, bx, by + (large ? 7 : 5) + h1, w, h2);
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
    // The amounts, small ones left, big ones right
    const lv_font_t* vf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    for (int v = 0; v < kCases; ++v) {
        const bool left = v < kCases / 2;
        const int row = left ? v : v - kCases / 2;
        const int x = left ? a.x1 : a.x2 - vw + 1, y = a.y1 + gy + row * (vh + (large ? 3 : 2));
        const bool live = g.in_play(v) || g.phase == Phase::Pick;
        const lv_color_t c = !live ? lv_color_mix(P.key, P.screen, 120) : left ? P.frame : P.piece_b;
        kit::fill_rect(layer, x, y, x + vw - 1, y + vh - 1, c, vh / 2);
        char m[16];
        money(m, sizeof m, kValues[v], true);
        kit::text(layer, m, vf, !live ? lv_color_mix(P.muted, P.screen, 160) : left ? P.stone_light : P.stone_dark, x, y, vw, vh);
    }
    // The cases
    for (int i = 0; i < kCases; ++i) {
        int x, y;
        case_box(a, i, &x, &y);
        draw_case(layer, x, y, i, i == g.mine, g.opened[i] && !(revealing && i == g.last_opened), kValues[g.value_of[i]]);
    }
    // What's happening
    char l1[40], l2[64];
    l2[0] = 0;
    if (revealing && g.last_opened >= 0) {
        char m[24];
        money(m, sizeof m, kValues[g.value_of[g.last_opened]]);
        snprintf(l1, sizeof l1, "%s", m);
        snprintf(l2, sizeof l2, "was in case %d", g.last_opened + 1);
        banner(layer, a, l1, l2, g.value_of[g.last_opened] >= 20 ? P.piece_a : P.frame);
    } else if (g.phase == Phase::Offer) {
        char m[24];
        money(m, sizeof m, g.offer);
        snprintf(l1, sizeof l1, "%s", m);
        snprintf(l2, sizeof l2, "The Banker's offer");
        banner(layer, a, l1, l2, P.lit);
    } else if (g.phase == Phase::Done) {
        char m[24], h[24];
        money(m, sizeof m, g.won);
        money(h, sizeof h, kValues[g.value_of[g.mine]]);
        snprintf(l1, sizeof l1, "You won %s", m);
        if (g.dealt >= 0) snprintf(l2, sizeof l2, "Your case %d held %s", g.mine + 1, h);
        else if (g.swapped) snprintf(l2, sizeof l2, "You swapped; yours held %s", h);
        else snprintf(l2, sizeof l2, "Your case %d paid out", g.mine + 1);
        banner(layer, a, l1, l2, P.win);
    }
}

// ---- Flow ----------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void set_key(lv_obj_t* k, bool show, const char* t, bool primary)
{
    if (!k) return;
    lv_obj_set_hidden(k, !show);
    if (!show) return;
    lv_label_set_text(lv_obj_get_child(k, 0), t);
    set_checked(k, primary);
}

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    char s[48], sh[24], l[24] = "";
    sh[0] = 0;
    if (g.mine >= 0) snprintf(l, sizeof l, "Case %d", g.mine + 1);
    lv_label_set_text(bar.left, l);
    switch (g.phase) {
        case Phase::Pick: snprintf(s, sizeof s, "Pick your case"); break;
        case Phase::Open:
            snprintf(s, sizeof s, "Round %d: open %d more", g.round + 1, g.to_open);
            snprintf(sh, sizeof sh, "Open %d more", g.to_open);
            break;
        case Phase::Offer: snprintf(s, sizeof s, revealing ? "Round %d" : "Deal or No Deal?", g.round + 1); break;
        case Phase::Swap:  snprintf(s, sizeof s, "Keep your case or swap?"); snprintf(sh, sizeof sh, "Keep or swap?"); break;
        case Phase::Done:  snprintf(s, sizeof s, "Game over"); break;
    }
    kit::top_bar_status(bar, s, sh[0] ? sh : nullptr);
    const bool offer = g.phase == Phase::Offer && !revealing;
    if (offer) { set_key(key_l, true, "Deal", false); set_key(key_r, true, "No Deal", true); }
    else if (g.phase == Phase::Swap && !revealing) {
        char a[24], b[24];
        snprintf(a, sizeof a, "Keep %d", g.mine + 1);
        snprintf(b, sizeof b, "Swap for %d", g.last_case() + 1);
        set_key(key_l, true, a, true); set_key(key_r, true, b, false);
    } else { set_key(key_l, false, "", false); set_key(key_r, false, "", false); }
    set_key(key_w, g.phase == Phase::Done, "Play Again", true);
    if (area) lv_obj_invalidate(area);
}

void finish()
{
    const Game& g = S->g;
    if (!S->recorded) record();
    sound(g.won >= kValues[g.value_of[g.mine]] ? Sound::Win : Sound::Lose);
    if (g.won >= 100000000 / 4) kit::flash();
}

void new_game()
{
    kit::flash_stop();
    *S = State{};
    S->g.start(seed_now());
    revealing = false;
    save();
    update();
}

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open() || revealing) return;
    lv_obj_t* k = lv_event_get_target_obj(e);
    Game& g = S->g;
    if (k == key_w) { new_game(); return; }
    if (g.phase == Phase::Offer) {
        if (k == key_l) { g.deal(); finish(); }
        else g.no_deal();
    } else if (g.phase == Phase::Swap) {
        g.keep_or_swap(k == key_r);
        finish();
    }
    save();
    update();
}

void area_tap_cb(lv_event_t*)
{
    if (!S || overlay_open() || revealing) return;
    Game& g = S->g;
    if (g.phase != Phase::Pick && g.phase != Phase::Open) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    for (int i = 0; i < kCases; ++i) {
        int x, y;
        case_box(a, i, &x, &y);
        if (p.x < x - gap / 2 || p.x > x + cw + gap / 2 || p.y < y - gap / 2 || p.y > y + ch + gap / 2) continue;
        if (g.phase == Phase::Pick) { g.pick(i); save(); update(); return; }
        if (!g.open(i)) return;
        // Shown a moment; then the Banker may call
        revealing = true;
        reveal_until = now_ms + kRevealMs;
        const int v = g.value_of[i];
        if (v >= 20) sound(Sound::Error);                  // $200,000 and up gone
        else if (v <= 3) sound(Sound::Trill);              // $10 or less gone
        save();
        update();
        return;
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
    gap = m.large ? 6 : 4;
    vw = m.large ? 70 : 50;
    vh = m.large ? 22 : 15;
    const int room_h = h - kh - 3 * pad;
    cw = (m.w - 2 * vw - 2 * gap - 2 * pad - 3 * gap) / 4;
    ch = (room_h - 6 * gap) / 7;
    if (ch > cw) ch = cw;
    gx = vw + gap + pad + ((m.w - 2 * vw - 2 * gap - 2 * pad) - (4 * cw + 3 * gap)) / 2;
    const int vcol = 13 * vh + 12 * (m.large ? 3 : 2), ccol = 7 * ch + 6 * gap;
    gy = ((room_h - (vcol > ccol ? vcol : ccol)) / 2) + 2;
    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w - 2 * pad, room_h);
    lv_obj_set_pos(area, pad, top + pad);
    gx -= pad;
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, area_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, area_tap_cb, LV_EVENT_CLICKED, nullptr);
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
        char a[12], b[12], c[20], d[20];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.deals);
        money(c, sizeof c, sum.best, true);
        money(d, sizeof d, sum.average(), true);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Deals", "Best", "Average"};
        static const int8_t pct[4] = {22, 22, 28, 28};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[20], s2[8], s3[20], tm[16];
            money(s1, sizeof s1, r.won, true);
            if (r.dealt >= 0) snprintf(s2, sizeof s2, "%d", r.dealt + 1); else snprintf(s2, sizeof s2, "-");
            money(s3, sizeof s3, r.held, true);
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, s3, tm);
        }
        const char* const head2[4] = {"Won", "Deal", "Case Held", "Time"};
        static const int8_t pct2[4] = {26, 18, 32, 24};
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
    kit::menu_solo("Deal or No CYD", nullptr, h, false);
}

// ---- Registry entry -----------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now()); }
    revealing = false;
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
    if (revealing && int32_t(now - reveal_until) >= 0) {
        revealing = false;
        if (S->g.phase == Phase::Offer) sound(Sound::Call);       // the Banker calls
        update();
    }
    if (clock_.tick(now, S->g.phase != Phase::Done, S->seconds)) {}
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    char m[24];
    if (st->g.phase == Phase::Done) { money(m, sizeof m, st->g.won); snprintf(buf, cap, "Won %s", m); }
    else if (st->g.phase == Phase::Pick) snprintf(buf, cap, "Pick your case");
    else snprintf(buf, cap, "Round %d, case %d", st->g.round + 1, st->g.mine + 1);
    return true;
}

void save_now() { save(); }

// Icon: three briefcases, the middle one gold
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int s = lv_area_get_width(&a);
    const int ocw = cw, och = ch;
    cw = s * 30 / 100; ch = s * 26 / 100;
    draw_case(layer, a.x1 + s * 2 / 100, a.y1 + s * 40 / 100, 6, false, false, 0);
    draw_case(layer, a.x1 + s * 68 / 100, a.y1 + s * 40 / 100, 20, false, false, 0);
    cw = s * 38 / 100; ch = s * 34 / 100;
    draw_case(layer, a.x1 + s * 31 / 100, a.y1 + s * 20 / 100, 12, true, false, 0);
    cw = ocw; ch = och;
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

namespace dealcyd_preview {
dealcyd::Game* game() { return S ? &S->g : nullptr; }
void reveal(bool on) { revealing = on; reveal_until = now_ms + (on ? 600000 : 0); update(); }
void redraw() { update(); }
}

namespace games {
extern const GameOps dealcyd_ops;
const GameOps dealcyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
