// Texas Hold'em: registry entry, save file and screen. Rules and the
// computer players in holdem_core.*; cards in common/cards.*.
//
// Screen (felt): the three computer players across the top (name, chips,
// two cards - face down until a showdown - and what they just did; the one
// to act has a gold edge, the button a "D"); the pot and the five community
// cards; your two cards with your chips and your best hand so far; keys:
//   your turn:   [Fold] [Check / Call 20] / [Raise 40] [Pot 120] [All In]
//   between:     [Next Hand] (or [New Chips +1000] when you're out)
// The computer thinks on the AI task and acts at a readable pace.
// Sounds: quiet, like every card game; a pot you win trills, a showdown
// you lose says "aww".
#include <cstdio>
#include <new>
#include "games/common/ai_task.h"
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "holdem_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace holdem;
using namespace ui;

constexpr const char* kId = "holdem";
const char* const kNames[kSeats] = {"You", "Ada", "Max", "Zoe"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};

struct State {
    Game    g;
    uint8_t recorded = 1;               // the finished hand is in the stats
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 1;

kit::TopBar bar;
lv_obj_t*   table = nullptr;
lv_obj_t*   keys[5] = {};
lv_obj_t*   key_l[5] = {};
intptr_t    key_id[5] = {};
int         kh = 0, rows_y = 0;

// The computer's move, worked out on the AI task
struct Think { Game g; uint32_t seed; Decision d; volatile bool done; };
Think*   think = nullptr;
bool     thinking = false;
uint32_t next_at = 0, now_ms = 0;

void build();
void open_menu();
void update_status();

// ---- Save -------------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = S->g.serialize(buf, kSaveBytes);
    buf[n] = S->recorded;
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) st.recorded = buf[Game::kSaveBytes];
    delete[] buf;
    return ok;
}

void record_hand()
{
    if (S->recorded) return;
    S->recorded = 1;
    const Game& g = S->g;
    const Seat& you = g.seat[0];
    Record r;
    int others_won = 0;
    for (int s = 1; s < kSeats; ++s) others_won += g.seat[s].won > 0;
    if (you.folded) r.result = Result::Folded;
    else if (you.won == 0) r.result = Result::Lost;
    else r.result = others_won ? Result::Split : Result::Won;
    r.net = you.won - you.total;
    r.chips = you.stack;
    char body[64];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
}

// ---- Drawing ----------------------------------------------------------------------------------
struct Layout { int opp_y, opp_w, opp_h, ocw, och, pot_y, board_y, bcw, bch, you_y, ycw, ych; };
Layout L;

void compute_layout(int top, int bottom, int w)
{
    const Metrics& M = metrics();
    const int pad = M.large ? 6 : 3, gap = M.large ? 6 : 3;
    const int small_h = lv_font_get_line_height(M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    const int avail = bottom - top - 2 * pad;
    // Shares of the height: opponents 30 %, pot + board 32 %, you 30 %
    L.opp_w = (w - 2 * pad - 2 * gap) / 3;
    L.opp_h = avail * 30 / 100;
    L.och = L.opp_h - 2 * small_h - 4;
    L.ocw = L.och * 5 / 7;
    if (2 * L.ocw + 4 > L.opp_w - 4) { L.ocw = (L.opp_w - 8) / 2; L.och = L.ocw * 7 / 5; }
    L.opp_y = top + pad;
    L.pot_y = L.opp_y + L.opp_h + gap;
    L.bch = avail * 32 / 100 - small_h - gap;
    L.bcw = L.bch * 5 / 7;
    if (5 * L.bcw + 4 * gap > w - 2 * pad) { L.bcw = (w - 2 * pad - 4 * gap) / 5; L.bch = L.bcw * 7 / 5; }
    L.board_y = L.pot_y + small_h + 2;
    L.you_y = L.board_y + L.bch + gap * 2;
    L.ych = bottom - pad - L.you_y;
    L.ycw = L.ych * 5 / 7;
}

void label_text(lv_layer_t* layer, const char* t, const lv_font_t* f, lv_color_t c, int x, int y, int w, bool centre = true)
{
    const int lh = lv_font_get_line_height(f);
    if (centre) kit::text(layer, t, f, c, x, y, w, lh);
    else kit::text(layer, t, f, c, x, y, text_width(t, f) + 2, lh);
}

void draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Metrics& M = metrics();
    const Game& g = S->g;
    const lv_color_t felt = cards::felt(), ink = contrast_text(felt);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, felt);
    const lv_font_t* sf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int sh = lv_font_get_line_height(sf);
    const int pad = M.large ? 6 : 3, gap = M.large ? 6 : 3;
    const bool show_all = g.hand_over() && g.showdown;
    // Opponents
    for (int s = 1; s < kSeats; ++s) {
        const Seat& p = g.seat[s];
        const int x = a.x1 + pad + (s - 1) * (L.opp_w + gap), y = L.opp_y;
        const bool turn = !g.hand_over() && g.to_act == s;
        const lv_color_t panel = lv_color_darken(felt, 30);
        if (turn) kit::fill_rect(layer, x - 2, y - 2, x + L.opp_w + 1, y + L.opp_h + 1, P.lit, 6);
        kit::fill_rect(layer, x, y, x + L.opp_w - 1, y + L.opp_h - 1, panel, 5);
        char t[32];
        snprintf(t, sizeof t, "%s %ld", kNames[s], long(p.stack));
        label_text(layer, t, sf, ink, x, y + 1, L.opp_w);
        const int cy = y + sh + 2, cx = x + (L.opp_w - (2 * L.ocw + 3)) / 2;
        for (int k = 0; k < 2; ++k) {
            const int xx = cx + k * (L.ocw + 3);
            if (p.folded && !show_all) continue;
            if (show_all && !p.folded) cards::draw_face(layer, xx, cy, L.ocw, L.och, p.cards[k]);
            else cards::draw_back(layer, xx, cy, L.ocw, L.och);
        }
        // What they did, or what they won
        if (g.hand_over() && p.won) snprintf(t, sizeof t, "Wins %ld", long(p.won));
        else if (show_all && !p.folded) snprintf(t, sizeof t, "%s", category_name(category(p.value)));
        else if (p.folded) snprintf(t, sizeof t, "Folded");
        else if (p.last == Act::Call || p.last == Act::Bet || p.last == Act::Raise || p.last == Act::AllIn || p.last == Act::Blind)
            snprintf(t, sizeof t, "%s %ld", act_name(p.last), long(p.last_amount));
        else snprintf(t, sizeof t, "%s", act_name(p.last));
        label_text(layer, t, sf, g.hand_over() && p.won ? P.lit : ink, x, y + L.opp_h - sh - 1, L.opp_w);
        if (g.dealer == s && g.hand_no) {               // the button, beside the cards
            const int r = sh / 2 + 1, bx = cx + 2 * L.ocw + 3 + (x + L.opp_w - (cx + 2 * L.ocw + 3)) / 2;
            kit::fill_circle(layer, bx, cy + L.och / 2, r, P.stone_light);
            kit::text(layer, "D", sf, P.stone_dark, bx - r, cy + L.och / 2 - sh / 2, 2 * r, sh);
        }
    }
    // Pot and board
    char pt[32];
    snprintf(pt, sizeof pt, "Pot %ld", long(g.pot()));
    label_text(layer, pt, sf, ink, a.x1, L.pot_y, lv_area_get_width(&a));
    const int bx = a.x1 + (lv_area_get_width(&a) - (5 * L.bcw + 4 * gap)) / 2;
    for (int k = 0; k < 5; ++k) {
        const int x = bx + k * (L.bcw + gap);
        if (k < g.board_n) cards::draw_face(layer, x, L.board_y, L.bcw, L.bch, g.board[k]);
        else cards::draw_slot(layer, x, L.board_y, L.bcw, L.bch);
    }
    // You
    const Seat& you = g.seat[0];
    const int yx = a.x1 + pad + 2;
    for (int k = 0; k < 2; ++k)
        if (g.hand_no == 0) cards::draw_slot(layer, yx + k * (L.ycw + gap), L.you_y, L.ycw, L.ych);   // nothing dealt yet
        else if (!you.folded || g.hand_over()) cards::draw_face(layer, yx + k * (L.ycw + gap), L.you_y, L.ycw, L.ych, you.cards[k],
                                                          !g.hand_over() && g.to_act == 0 && !you.folded);
        else cards::draw_back(layer, yx + k * (L.ycw + gap), L.you_y, L.ycw, L.ych);
    const lv_font_t* bf = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int tx = yx + 2 * (L.ycw + gap) + gap, tw = a.x2 - pad - tx;
    char t[40];
    snprintf(t, sizeof t, "You %ld", long(you.stack));
    label_text(layer, t, bf, ink, tx, L.you_y, tw, false);
    int ty = L.you_y + lv_font_get_line_height(bf) + 2;
    // Your best hand so far
    if (g.board_n >= 3 && !you.folded) {
        uint8_t c[7] = {you.cards[0], you.cards[1]};
        for (int k = 0; k < g.board_n; ++k) c[2 + k] = g.board[k];
        snprintf(t, sizeof t, "%s", category_name(category(score(c, 2 + g.board_n))));
        label_text(layer, t, sf, P.lit, tx, ty, tw, false);
        ty += sh + 2;
    }
    if (g.hand_over() && you.won) snprintf(t, sizeof t, "Wins %ld", long(you.won));
    else if (you.folded) snprintf(t, sizeof t, "Folded");
    else if (you.bet) snprintf(t, sizeof t, "Bet %ld", long(you.bet));
    else t[0] = 0;
    if (t[0]) label_text(layer, t, sf, g.hand_over() && you.won ? P.lit : ink, tx, ty, tw, false);
    if (g.dealer == 0 && g.hand_no) {
        const int r = sh / 2 + 1;
        kit::fill_circle(layer, a.x2 - pad - r - 2, L.you_y + r + 1, r, P.stone_light);
        kit::text(layer, "D", sf, P.stone_dark, a.x2 - pad - 2 * r - 2, L.you_y + 1, 2 * r, sh);
    }
}

// ---- Keys ---------------------------------------------------------------------------------------
enum KeyId : intptr_t { kFold = 0, kCall, kMinRaise, kPotRaise, kAllIn, kNext, kChips };

bool your_turn() { return S && !S->g.hand_over() && S->g.to_act == 0 && !thinking; }

int32_t pot_raise_to()
{
    const Game& g = S->g;
    int32_t to = g.current_bet + g.pot() + g.to_call();
    if (to < g.min_raise_to()) to = g.min_raise_to();
    if (to > g.max_raise_to()) to = g.max_raise_to();
    return to;
}

void set_key(int k, const char* text, intptr_t id, bool on, bool primary, int x, int y, int w)
{
    lv_obj_set_hidden(keys[k], text == nullptr);
    if (!text) return;
    lv_label_set_text(key_l[k], text);
    key_id[k] = id;
    set_dim(keys[k], !on);
    set_checked(keys[k], primary && on);
    lv_obj_set_size(keys[k], w, kh);
    lv_obj_set_pos(keys[k], x, y);
}

void update_keys()
{
    const Game& g = S->g;
    const Metrics& m = metrics();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    const int W = m.w - 2 * pad, half = (W - gap) / 2, third = (W - 2 * gap) / 3;
    const int y1 = rows_y, y2 = rows_y + kh + gap;
    if (g.hand_over()) {
        if (g.seat[0].stack <= 0) set_key(0, "New Chips (+1000)", kChips, true, true, pad, y1, W);
        else set_key(0, g.hand_no ? "Next Hand" : "Deal", kNext, true, true, pad, y1, W);
        for (int k = 1; k < 5; ++k) set_key(k, nullptr, 0, false, false, 0, 0, 0);
        return;
    }
    const bool on = your_turn();
    static char call_t[24], raise_t[24], pot_t[24];
    const int32_t call = on ? g.to_call() : 0;
    if (call) snprintf(call_t, sizeof call_t, "Call %ld", long(call));
    else snprintf(call_t, sizeof call_t, "Check");
    set_key(0, "Fold", kFold, on && call > 0, false, pad, y1, half);
    set_key(1, call_t, kCall, on, true, pad + half + gap, y1, half);
    const bool can_raise = on && g.max_raise_to() > g.current_bet;
    snprintf(raise_t, sizeof raise_t, "%s %ld", g.current_bet ? "Raise" : "Bet", long(on ? g.min_raise_to() : 0));
    snprintf(pot_t, sizeof pot_t, "Pot %ld", long(on ? pot_raise_to() : 0));
    set_key(2, on ? raise_t : (g.current_bet ? "Raise" : "Bet"), kMinRaise, can_raise, false, pad, y2, third);
    set_key(3, on ? pot_t : "Pot", kPotRaise, can_raise && pot_raise_to() > g.min_raise_to(), false, pad + third + gap, y2, third);
    set_key(4, "All In", kAllIn, on && g.seat[0].stack > 0, false, pad + 2 * (third + gap), y2, third);
}

void after_change()
{
    Game& g = S->g;
    if (g.hand_over() && !S->recorded) {
        const Seat& you = g.seat[0];
        if (you.won > 0) sound(Sound::Trill);
        else if (g.showdown && !you.folded) sound(Sound::Error);
        record_hand();
    }
    save();
    if (table) lv_obj_invalidate(table);
    update_status();
}

void start_hand()
{
    Game& g = S->g;
    if (g.seat[0].stack <= 0) return;
    g.new_hand(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u);
    S->recorded = 0;
    next_at = now_ms + 500;
    after_change();
}

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    const int k = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    switch (key_id[k]) {
        case kNext:  start_hand(); return;
        case kChips: g.seat[0].stack += kStartStack; ++g.seat[0].rebuys; after_change(); return;
        default: break;
    }
    if (!your_turn()) return;
    switch (key_id[k]) {
        case kFold:      g.act({Act::Fold, 0}); break;
        case kCall:      g.act({Act::Call, 0}); break;
        case kMinRaise:  g.act({g.current_bet ? Act::Raise : Act::Bet, g.min_raise_to()}); break;
        case kPotRaise:  g.act({g.current_bet ? Act::Raise : Act::Bet, pot_raise_to()}); break;
        case kAllIn:     g.act({Act::AllIn, g.max_raise_to()}); break;
    }
    next_at = now_ms + 450;
    after_change();
}

void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char s[48];
    snprintf(s, sizeof s, "%s", "");
    if (g.hand_over()) {
        if (g.hand_no == 0) snprintf(s, sizeof s, "%s table", kLevels[g.level]);
        else {
            int best = 0;
            for (int i = 1; i < kSeats; ++i) if (g.seat[i].won > g.seat[best].won) best = i;
            if (g.seat[0].won > 0 && g.seat[0].won >= g.seat[best].won) snprintf(s, sizeof s, "You win %ld", long(g.seat[0].won));
            else snprintf(s, sizeof s, "%s wins %ld", kNames[best], long(g.seat[best].won));
        }
    } else if (g.to_act == 0) {
        snprintf(s, sizeof s, "%s", g.to_call() ? "Your turn: call, raise or fold" : "Your turn");
    } else {
        snprintf(s, sizeof s, "%s is thinking", kNames[g.to_act]);
    }
    kit::top_bar_status(bar, s);
    char left[16];
    if (g.hand_no) snprintf(left, sizeof left, "#%lu", (unsigned long)g.hand_no);
    else left[0] = 0;
    lv_label_set_text(bar.left, left);
    update_keys();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    kh = menu_btn_h();
    rows_y = m.h - pad - 2 * kh - gap;
    table = lv_obj_create(scr);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, m.w, rows_y - gap / 2 - bar.h);
    lv_obj_set_pos(table, 0, bar.h);
    lv_obj_add_event_cb(table, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    compute_layout(bar.h, rows_y - gap / 2, m.w);
    for (int k = 0; k < 5; ++k) {
        keys[k] = make_key(scr, 10, kh, key_cb, k);
        key_l[k] = key_label(keys[k], "", menu_font());
    }
    update_status();
}

// ---- The computer, on the AI task --------------------------------------------------------
void think_job(void* ctx, volatile bool*)
{
    Think* t = static_cast<Think*>(ctx);
    Rng rng(t->seed);
    t->d = t->g.decide(rng);
    t->done = true;
}

void computer_tick()
{
    Game& g = S->g;
    if (thinking) {
        if (!think->done) return;
        thinking = false;
        if (!g.hand_over() && g.to_act != 0) g.act(think->d);
        next_at = now_ms + (g.street != think->g.street ? 900 : 650);   // a pause when a street is dealt
        after_change();
        return;
    }
    if (g.hand_over() || g.to_act == 0 || int32_t(now_ms - next_at) < 0 || overlay_open()) return;
    if (!think) think = new (std::nothrow) Think();
    if (!think) return;
    think->g = g;
    think->seed = shell().random_seed ? shell().random_seed() : now_ms;
    think->done = false;
    thinking = ai_start(think_job, think, 12 * 1024);
    if (!thinking) {                                      // no task: decide here
        Rng rng(think->seed);
        g.act(g.decide(rng));
        next_at = now_ms + 650;
        after_change();
    } else {
        update_status();
    }
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
    Summary sum;
    const bool ok = shell().stats_read && shell().stats_read(kId, stats_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum.hands == 0) {
        overlay_text("No hands recorded yet. Every hand you're dealt is listed here.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[16], b[16], c[16], d[16];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.hands);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.won);
        snprintf(c, sizeof c, "%lu", (unsigned long)sum.folded);
        snprintf(d, sizeof d, "%ld", long(sum.net));
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Hands", "Won", "Folded", "Net"};
        static const int8_t pct[4] = {25, 22, 27, 26};
        table_show(t, head, pct, hf, bf);
        Table& t2 = scratch_table(1);
        table_clear(t2);
        snprintf(a, sizeof a, "%ld", long(sum.biggest));
        snprintf(b, sizeof b, "%ld", long(sum.best_chips));
        snprintf(c, sizeof c, "%u", unsigned(S ? S->g.seat[0].rebuys : 0));
        table_add(t2, a, b, c, "");
        const char* const head2[4] = {"Best Pot", "Most Chips", "Refills", ""};
        static const int8_t pct2[4] = {32, 38, 30, 0};
        table_show(t2, head2, pct2, hf, bf);
    }
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.", shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    const int bh = menu_btn_h(), gap = large ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && sum.hands > 0 && shell().stats_delete_last && shell().stats_clear) {
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
void stop_thinking()
{
    if (thinking) { ai_stop(); thinking = false; }
}

void menu_pick(int id)
{
    if (id >= kit::kLevel0 && id <= kit::kLevel2) {       // New game: fresh stacks at that level
        stop_thinking();
        record_hand();
        const uint16_t rebuys = S->g.seat[0].rebuys;
        S->g = Game{};
        S->g.level = uint8_t(id);
        S->g.seat[0].rebuys = rebuys;
        S->recorded = 1;
        save();
        build();
    } else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_back() { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Texas Hold'em", kLevels, h, false, "Card Back");
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { S->g = Game{}; S->recorded = 1; }
    thinking = false;
    next_at = 0;
    build();
}

void close()
{
    if (!S) return;
    stop_thinking();
    record_hand();
    save();
    bar = kit::TopBar{};
    table = nullptr;
    for (auto& k : keys) k = nullptr;
    for (auto& k : key_l) k = nullptr;
    delete think;
    think = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    now_ms = now;
    if (S) computer_tick();
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State* tmp = nullptr;
    const State* st = S;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    snprintf(buf, cap, "%s, %ld chips", kLevels[st->g.level > 2 ? 1 : st->g.level], long(st->g.seat[0].stack));
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a pocket pair of Aces
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 58 / 100, h = size * 80 / 100;
    cards::draw_face(layer, a.x1, a.y1, w, h, cards::make(1, cards::Spades));
    cards::draw_face(layer, a.x1 + size - w, a.y1 + size - h, w, h, cards::make(1, cards::Diamonds));
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
extern const GameOps holdem_ops;
const GameOps holdem_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace holdem_preview {
// Play on until it's your turn (or the hand ends)
void run_to_you()
{
    if (!S) return;
    Rng rng(99);
    while (!S->g.hand_over() && S->g.to_act != 0) S->g.act(S->g.decide(rng));
    after_change();
}
void deal(uint32_t seed) { if (!S) return; S->g.new_hand(seed); S->recorded = 0; after_change(); }
void you(holdem::Act a) { if (!S || S->g.hand_over() || S->g.to_act != 0) return; S->g.act({a, S->g.min_raise_to()}); after_change(); }
holdem::Game* game() { return S ? &S->g : nullptr; }
void refresh() { after_change(); }
} // namespace holdem_preview
#endif
