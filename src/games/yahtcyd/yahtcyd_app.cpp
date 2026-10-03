// Yaht-CYD: registry entry, save file and screen. Top to bottom: top bar,
// five dice (tap one to hold it between rolls; held dice turn gold), the
// Roll key, and the score card in two columns (upper boxes left, lower boxes
// right). After a roll, empty boxes show what they would score; tap one to
// take it. The dice row and the score card are each one custom-drawn object.
#include <cstdio>
#include <new>
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "yahtcyd_core.h"

namespace {

using namespace yahtcyd;
using namespace ui;

constexpr const char* kId = "yahtcyd";

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint8_t  yahts = 0;                // Yaht-CYDs rolled and scored as 50 or bonus
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 6;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   dice_obj = nullptr;
lv_obj_t*   card_obj = nullptr;
lv_obj_t*   roll_k = nullptr;
lv_obj_t*   roll_l = nullptr;
int         die = 0, die_gap = 0, row_h = 0;
uint32_t    last_save_ms = 0;

void build();
void open_menu();

// ---- Save --------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = S->recorded;
    buf[n + 1] = S->yahts;
    for (int k = 0; k < 4; ++k) buf[n + 2 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, sizeof buf);
}

bool load(State& st)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    if (n != kSaveBytes || !st.g.deserialize(buf, n)) return false;
    st.recorded = buf[Game::kSaveBytes] ? 1 : 0;
    st.yahts = buf[Game::kSaveBytes + 1];
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 2 + k]) << (8 * k);
    return true;
}

// ---- Game flow --------------------------------------------------------------------------------
void update_status()
{
    if (!bar.center) return;
    const Game& g = S->g;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (g.over())            snprintf(s, sizeof s, "Final score %d", g.total());
    else if (clock_.paused)  snprintf(s, sizeof s, "Paused");
    else if (g.rolls == 0)   snprintf(s, sizeof s, "Turn %d of 13", g.turn());
    else if (g.rolls < 3)    snprintf(s, sizeof s, "Turn %d, roll %d of 3", g.turn(), g.rolls);
    else                     snprintf(s, sizeof s, "Turn %d: pick a box", g.turn());
    kit::top_bar_status(bar, s);
    char r[24];
    if (g.over())            snprintf(r, sizeof r, "Play Again");
    else if (g.rolls == 0)   snprintf(r, sizeof r, "Roll");
    else if (g.rolls < 3)    snprintf(r, sizeof r, "Roll Again (%d Left)", 3 - g.rolls);
    else                     snprintf(r, sizeof r, "No Rolls Left");
    lv_label_set_text(roll_l, r);
    set_dim(roll_k, !g.over() && g.rolls >= 3);
    set_checked(roll_k, g.over() || g.rolls == 0);
}

void redraw_all()
{
    lv_obj_invalidate(dice_obj);
    lv_obj_invalidate(card_obj);
    update_status();
}

void record()
{
    const Game& g = S->g;
    Record r;
    r.score = static_cast<uint16_t>(g.total());
    r.upper = static_cast<uint16_t>(g.upper());
    r.bonus = static_cast<uint16_t>(g.bonus());
    r.yahts = S->yahts;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
}

void new_game()
{
    kit::flash_stop();
    // A game left after a few turns isn't recorded: only finished games count
    *S = State{};
    build();
    save();
}

void roll_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.over()) { new_game(); return; }
    if (!g.can_roll()) { sound(Sound::Error); return; }
    Rng rng(shell().random_seed ? shell().random_seed() : lv_tick_get());
    g.roll(rng);
    sound(g.is_yaht() ? Sound::Hint : Sound::Move);
    save();
    redraw_all();
}

void take_box(int box)
{
    Game& g = S->g;
    if (!g.can_score(box)) { if (g.rolls) sound(Sound::Error); return; }
    const bool yaht = g.is_yaht() && (box == YahtCyd ? g.potential(box) == 50 : g.score[YahtCyd] == 50);
    g.score_box(box);
    if (yaht) { ++S->yahts; sound(Sound::Win); kit::flash(); }
    else sound(Sound::Place);
    if (g.over() && !S->recorded) {
        S->recorded = 1;
        record();
        sound(Sound::Win);
        kit::flash();
    }
    save();
    redraw_all();
}

// ---- Dice ---------------------------------------------------------------------------------------
void draw_die(lv_layer_t* layer, int x, int y, int size, int value, bool held, bool blank)
{
    const Palette& P = pal();
    kit::fill_rect(layer, x, y, x + size - 1, y + size - 1, held ? P.selected : P.key_border, size / 5);
    kit::fill_rect(layer, x + 2, y + 2, x + size - 3, y + size - 3, held ? P.lit : P.cell, size / 5 - 1);
    if (blank) return;
    // Pip positions on a 3x3 grid
    static const uint16_t kPips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};
    const int r = size / 11 > 2 ? size / 11 : 2, step = size * 27 / 100;
    for (int k = 0; k < 9; ++k) {
        if (!((kPips[value] >> k) & 1)) continue;
        const int cx = x + size / 2 + (k % 3 - 1) * step, cy = y + size / 2 + (k / 3 - 1) * step;
        kit::fill_circle(layer, cx, cy, r, P.ink);
    }
}

void dice_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const Game& g = S->g;
    const bool blank = g.rolls == 0 && !g.over();          // nothing rolled this turn yet
    for (int k = 0; k < 5; ++k)
        draw_die(lv_event_get_layer(e), a.x1 + k * (die + die_gap), a.y1, die, g.dice[k],
                 g.rolls > 0 && ((g.held >> k) & 1), blank);
}

void dice_press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev || !S) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int k = (p.x - a.x1) / (die + die_gap);
    k = k < 0 ? 0 : k > 4 ? 4 : k;
    Game& g = S->g;
    if (g.rolls == 0 || g.rolls >= 3 || g.over()) return;
    g.toggle_hold(k);
    save();
    lv_obj_invalidate(dice_obj);
}

// ---- Score card ------------------------------------------------------------------------------
// 8 rows x 2 columns: left Ones..Sixes, Bonus, Upper; right the 7 lower
// boxes and Total. Row r, column c -> box number (or -1 for a sum line).
int box_at(int r, int c)
{
    if (c == 0) return r < 6 ? r : -1;
    return r < 7 ? ThreeKind + r : -1;
}

void card_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Game& g = S->g;
    const int w = lv_area_get_width(&a), gap = 4;
    const int cw = (w - gap) / 2;
    const lv_font_t* f = metrics().large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int pad = metrics().large ? 6 : 5;
    for (int c = 0; c < 2; ++c)
        for (int r = 0; r < 8; ++r) {
            const int x = a.x1 + c * (cw + gap), y = a.y1 + r * row_h;
            const int box = box_at(r, c);
            char name[24], val[16] = "";
            lv_color_t bg = P.key, ink = P.ink, vink = P.ink;
            if (box >= 0) {
                snprintf(name, sizeof name, "%s", box_name(box));
                if (g.score[box] >= 0) {
                    // Used: filled in solid blue, so open boxes stand out
                    // (Tom: tell used from unused at a glance)
                    snprintf(val, sizeof val, "%d", g.score[box]);
                    bg = P.frame;
                    ink = vink = contrast_text(P.frame);
                } else if (g.can_score(box)) {
                    snprintf(val, sizeof val, "%d", g.potential(box));
                    vink = g.potential(box) ? P.win : P.muted;     // scorable: show what it gives
                    bg = P.key;
                } else {
                    bg = P.key;
                }
            } else {
                bg = P.screen;
                ink = vink = P.muted;
                if (c == 0 && r == 6) {          // the bonus, or how far along it is
                    snprintf(name, sizeof name, "Bonus");
                    if (g.bonus()) snprintf(val, sizeof val, "%d", g.bonus());
                    else           snprintf(val, sizeof val, "%d/63", g.upper());
                }
                else if (c == 0 && r == 7) { snprintf(name, sizeof name, "Upper"); snprintf(val, sizeof val, "%d", g.upper() + g.bonus()); }
                else { snprintf(name, sizeof name, g.extra ? "Total +%d" : "Total", 100 * g.extra);
                       snprintf(val, sizeof val, "%d", g.total()); ink = vink = P.ink; }
            }
            if (box >= 0) {
                kit::fill_rect(layer, x, y + 1, x + cw - 1, y + row_h - 2, P.key_border, 4);
                kit::fill_rect(layer, x + 1, y + 2, x + cw - 2, y + row_h - 3, bg, 3);
            }
            lv_draw_label_dsc_t d;
            lv_draw_label_dsc_init(&d);
            d.font = f;
            d.text_local = 1;
            const int lh = lv_font_get_line_height(f);
            const int ty = y + (row_h - lh) / 2;
            // Value on the right, name in what is left (one line, never wrapped)
            const int vw = text_width(val, f);
            d.text = val;
            d.color = vink;
            lv_area_t va{x + cw - pad - vw, ty, x + cw - pad - 1, ty + lh - 1};
            lv_draw_label(layer, &d, &va);
            d.text = name;
            d.color = ink;
            d.flag = LV_TEXT_FLAG_EXPAND;
            lv_area_t na{x + pad, ty, x + cw - pad - vw - 2, ty + lh - 1};
            lv_draw_label(layer, &d, &na);
        }
}

void card_press_cb(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev || !S || overlay_open()) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int c = (p.x - a.x1) < lv_area_get_width(&a) / 2 ? 0 : 1;
    int r = (p.y - a.y1) / row_h;
    r = r < 0 ? 0 : r > 7 ? 7 : r;
    const int box = box_at(r, c);
    if (box >= 0) take_box(box);
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = 4;
    int y = bar.h + (m.large ? 6 : 3);
    // Dice
    die_gap = m.large ? 10 : 6;
    die = (m.w - 2 * pad - 4 * die_gap) / 5;
    const int cap = m.large ? 56 : 40;
    if (die > cap) die = cap;
    dice_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(dice_obj);
    lv_obj_set_size(dice_obj, 5 * die + 4 * die_gap, die);
    lv_obj_set_pos(dice_obj, (m.w - (5 * die + 4 * die_gap)) / 2, y);
    lv_obj_set_clickable(dice_obj, true);
    lv_obj_add_event_cb(dice_obj, dice_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(dice_obj, dice_press_cb, LV_EVENT_PRESSED, nullptr);
    y += die + (m.large ? 10 : 6);
    // Roll key
    roll_k = make_key(scr, m.w - 2 * pad, menu_btn_h(), roll_cb, 0);
    lv_obj_set_pos(roll_k, pad, y);
    roll_l = key_label(roll_k, "Roll", menu_font());
    y += menu_btn_h() + (m.large ? 10 : 5);
    // Score card fills the rest
    row_h = (m.h - pad - y) / 8;
    card_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(card_obj);
    lv_obj_set_size(card_obj, m.w - 2 * pad, 8 * row_h);
    lv_obj_set_pos(card_obj, pad, y);
    lv_obj_set_clickable(card_obj, true);
    lv_obj_add_event_cb(card_obj, card_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(card_obj, card_press_cb, LV_EVENT_SHORT_CLICKED, nullptr);
    clock_ = kit::Clock{};
    update_status();
}

// ---- Stats ---------------------------------------------------------------------------------
void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

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
        char a[12], b[12], c[12], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.best);
        snprintf(c, sizeof c, "%lu", (unsigned long)sum.average());
        snprintf(d, sizeof d, "%lu", (unsigned long)sum.yahts);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Best", "Average", "Yaht-CYDs"};
        static const int8_t pct[4] = {22, 22, 26, 30};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[12], s2[12], s3[12], tm[16];
            snprintf(s1, sizeof s1, "%u", (unsigned)r.score);
            snprintf(s2, sizeof s2, "%u", (unsigned)(r.upper + r.bonus));
            snprintf(s3, sizeof s3, "%u", (unsigned)r.yahts);
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, s3, tm);
        }
        const char* const head2[4] = {"Recent", "Upper", "Yaht-CYDs", "Time"};
        static const int8_t pct2[4] = {24, 22, 30, 24};
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
    open_stats();
}

// ---- Menu -------------------------------------------------------------------------------------
void menu_pick(int id) { if (id == kit::kLevel0) new_game(); }
void menu_back()       { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Yaht-CYD", nullptr, h, false);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) *S = State{};
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    dice_obj = card_obj = roll_k = roll_l = nullptr;
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
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    if (st->g.over()) snprintf(buf, cap, "Final score %d", st->g.total());
    else              snprintf(buf, cap, "Turn %d of 13, score %d", st->g.turn(), st->g.total());
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), d = size * 46 / 100;
    draw_die(layer, a.x1, a.y1, d, 5, false, false);
    draw_die(layer, a.x1 + size - d, a.y1 + size / 8, d, 5, true, false);
    draw_die(layer, a.x1 + (size - d) / 2, a.y1 + size - d, d, 5, false, false);
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
extern const GameOps yahtcyd_ops;
const GameOps yahtcyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
