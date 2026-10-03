// Blackjack: registry entry, save file and screen. Rules in
// blackjack_core.*; cards in common/cards.*.
//
// Screen: top bar (chips, the round's news, ☰); the table: the dealer's
// cards (the hole card turns over when your hands are done, then the
// dealer's draws come one at a time), your hand (or two after a split; the
// one being played has a gold label); keys in two rows:
//   betting:  +5  +10  +25  Clear  /  Deal (the bet)  (New Chips when broke)
//   playing:  Hit  Stand  /  Double  Split
// You against the house, no other players at the table (Tom asked; that's
// the classic game).
//
// Sounds: none while playing (card games are quiet); Fanfare for a
// blackjack.
#include <cstdio>
#include <new>
#include "blackjack_core.h"
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace blackjack;
using namespace ui;

constexpr const char* kId = "blackjack";

struct State {
    Game    g;
    uint8_t recorded = 1;          // this round's hands are in the stats
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 1;

kit::TopBar bar;
lv_obj_t*   table = nullptr;
lv_obj_t*   keys[6] = {};          // two rows: 4 + 2 (betting) or 2 + 2 (playing)
lv_obj_t*   key_l[6] = {};
intptr_t    key_id[6] = {};       // what each key does right now
lv_timer_t* reveal_timer = nullptr;
int         dealer_shown = 0;      // dealer cards face up so far
int         cw = 0, ch = 0, kh = 0, rows_y = 0;

void build();
void open_menu();
void update_keys();

// ---- Save ---------------------------------------------------------------------------------
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

// ---- Stats ---------------------------------------------------------------------------------
int32_t hand_net(const Hand& h, Result r)
{
    switch (r) {
        case Result::Win:       return h.bet;
        case Result::Blackjack: return h.bet * 3 / 2;
        case Result::Lose:      return -h.bet;
        default:                return 0;
    }
}

void record_round()
{
    if (S->recorded) return;
    S->recorded = 1;
    const Game& g = S->g;
    for (int i = 0; i < g.hands; ++i) {
        Record r{g.hand[i].bet, g.result[i], hand_net(g.hand[i], g.result[i]), g.chips};
        char body[64];
        if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    }
}

// ---- Drawing --------------------------------------------------------------------------------
bool revealing() { return S->g.phase == Phase::Done && dealer_shown < S->g.dealer.n; }

// A row of cards centred at y; fanned closer when they don't fit
void draw_row(lv_layer_t* layer, const lv_area_t& a, const Hand& h, int y, int shown, int w, int hgt)
{
    const int W = lv_area_get_width(&a);
    int step = w + 6;
    if (h.n > 1 && (h.n - 1) * step + w > W - 8) step = (W - 8 - w) / (h.n - 1);
    const int total = (h.n - 1) * step + w;
    const int x0 = a.x1 + (W - total) / 2;
    for (int i = 0; i < h.n; ++i) {
        if (i < shown) cards::draw_face(layer, x0 + i * step, y, w, hgt, h.c[i]);
        else           cards::draw_back(layer, x0 + i * step, y, w, hgt);
    }
}

void label(lv_layer_t* layer, const char* s, const lv_area_t& a, int y, lv_color_t c)
{
    kit::text(layer, s, bar_font(), c, a.x1, y, lv_area_get_width(&a), lv_font_get_line_height(bar_font()));
}

const char* outcome(Result r)
{
    switch (r) {
        case Result::Win:       return "Win";
        case Result::Blackjack: return "Blackjack!";
        case Result::Push:      return "Push";
        case Result::Lose:      return "Lose";
        default:                return "";
    }
}

void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 0);
    const int lh = lv_font_get_line_height(bar_font());
    const bool dealt = g.dealer.n > 0;
    char t[48];
    int y = a.y1 + 4;
    // After a split the dealer and both hands share the height equally
    const int rows = g.hands == 2 ? 3 : 2;
    int row_ch = (lv_area_get_height(&a) - rows * (lh + 2) - 4 - 8 * rows) / rows;
    if (row_ch > ch) row_ch = ch;
    const int row_cw = row_ch * 5 / 7;
    // Dealer
    if (dealt) {
        const int shown = g.phase == Phase::Playing ? 1 : dealer_shown;
        Hand vis = g.dealer;
        vis.n = uint8_t(shown);
        if (shown >= g.dealer.n) snprintf(t, sizeof t, g.dealer.bust() ? "Dealer %d Bust" : "Dealer %d", g.dealer.value());
        else                     snprintf(t, sizeof t, "Dealer %d", vis.value());
        label(layer, t, a, y, P.stone_light);
        draw_row(layer, a, g.dealer, y + lh + 2, shown, row_cw, row_ch);
    } else {
        label(layer, "Dealer", a, y, P.stone_light);
        cards::draw_slot(layer, a.x1 + (lv_area_get_width(&a) - cw) / 2, y + lh + 2, cw, ch);
    }
    y += lh + row_ch + 10;
    const int hh = row_ch, hw = row_cw;
    for (int i = 0; i < g.hands; ++i) {
        const Hand& h = g.hand[i];
        if (!dealt) {
            snprintf(t, sizeof t, "Bet %ld", long(g.bet));
            label(layer, t, a, y, P.stone_light);
            cards::draw_slot(layer, a.x1 + (lv_area_get_width(&a) - cw) / 2, y + lh + 2, cw, ch);
            break;
        }
        const bool settled = g.phase == Phase::Done && !revealing();
        if (settled) snprintf(t, sizeof t, "%s%d  %s  (bet %ld)", g.hands == 2 ? "" : "You ", h.value(), outcome(g.result[i]), long(h.bet));
        else         snprintf(t, sizeof t, "%s%d%s  (bet %ld)", g.hands == 2 ? (i ? "Right " : "Left ") : "You ", h.value(),
                              h.bust() ? " Bust" : "", long(h.bet));
        const bool active = g.phase == Phase::Playing && g.active == i && g.hands == 2;
        label(layer, t, a, y, active ? P.lit : P.stone_light);
        draw_row(layer, a, h, y + lh + 2, h.n, hw, hh);
        y += lh + hh + 6;
    }
}

// ---- Flow ---------------------------------------------------------------------------------------
void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char c[24], s[48];
    snprintf(c, sizeof c, "$%ld", long(g.chips));
    lv_label_set_text(bar.left, c);
    if (g.phase == Phase::Playing)   snprintf(s, sizeof s, "Hit or stand?");
    else if (revealing())            snprintf(s, sizeof s, "Dealer plays...");
    else if (g.phase == Phase::Done) {
        const long n = long(g.net);
        if (n > 0)       snprintf(s, sizeof s, "You win $%ld", n);
        else if (n < 0)  snprintf(s, sizeof s, "You lose $%ld", -n);
        else             snprintf(s, sizeof s, "Even");
    } else                           snprintf(s, sizeof s, "Place your bet");
    kit::top_bar_status(bar, s);
    update_keys();
    lv_obj_invalidate(table);
}

void reveal_cb(lv_timer_t*)
{
    if (!S) return;
    if (dealer_shown < S->g.dealer.n) ++dealer_shown;
    if (dealer_shown >= S->g.dealer.n) {
        lv_timer_delete(reveal_timer);
        reveal_timer = nullptr;
        record_round();
        save();
    }
    update_status();
}

void round_over()
{
    // Turn the hole card over, then the dealer's draws one by one
    const Game& g = S->g;
    S->recorded = 0;
    dealer_shown = 2;
    for (int i = 0; i < g.hands; ++i)
        if (g.result[i] == Result::Blackjack) sound(Sound::Fanfare);
    if (!reveal_timer) reveal_timer = lv_timer_create(reveal_cb, 450, nullptr);
    save();
    update_status();
}

void after_action()
{
    if (S->g.phase == Phase::Done) round_over();
    else { save(); update_status(); }
}

enum KeyId : intptr_t { kAdd5 = 0, kAdd10, kAdd25, kClear, kDeal, kHit, kStand, kDouble, kSplit, kRefill };

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open() || revealing()) return;
    Game& g = S->g;
    const int k = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    switch (static_cast<KeyId>(key_id[k])) {
        case kAdd5:   g.add_bet(5); break;
        case kAdd10:  g.add_bet(10); break;
        case kAdd25:  g.add_bet(25); break;
        case kClear:  g.clear_bet(); break;
        case kDeal:
            if (g.deal()) { dealer_shown = 1; after_action(); return; }
            break;
        case kRefill: g.refill(); if (g.bet < kMinBet) g.bet = 10; break;
        case kHit:    g.hit(); after_action(); return;
        case kStand:  g.stand(); after_action(); return;
        case kDouble: g.double_down(); after_action(); return;
        case kSplit:  g.split(); after_action(); return;
    }
    save();
    update_status();
}

void set_key(int k, const char* text, intptr_t id, bool on, bool primary = false)
{
    lv_obj_set_hidden(keys[k], text == nullptr);
    if (!text) return;
    lv_label_set_text(key_l[k], text);
    key_id[k] = id;
    set_dim(keys[k], !on);
    set_checked(keys[k], primary && on);
}

void update_keys()
{
    const Game& g = S->g;
    const Metrics& m = metrics();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    const int W = m.w - 2 * pad;
    if (g.phase == Phase::Playing) {
        const int half = (W - gap) / 2;
        set_key(0, "Hit", kHit, g.can_hit(), true);
        set_key(1, "Stand", kStand, true, true);
        set_key(2, nullptr, 0, false);
        set_key(3, nullptr, 0, false);
        set_key(4, "Double", kDouble, g.can_double());
        set_key(5, "Split", kSplit, g.can_split());
        lv_obj_set_size(keys[0], half, kh); lv_obj_set_pos(keys[0], pad, rows_y);
        lv_obj_set_size(keys[1], half, kh); lv_obj_set_pos(keys[1], pad + half + gap, rows_y);
        lv_obj_set_size(keys[4], half, kh); lv_obj_set_pos(keys[4], pad, rows_y + kh + gap);
        lv_obj_set_size(keys[5], half, kh); lv_obj_set_pos(keys[5], pad + half + gap, rows_y + kh + gap);
        return;
    }
    const int q = (W - 3 * gap) / 4;
    const bool idle = !revealing();
    set_key(0, "+5", kAdd5, idle && g.bet + 5 <= g.chips);
    set_key(1, "+10", kAdd10, idle && g.bet + 10 <= g.chips);
    set_key(2, "+25", kAdd25, idle && g.bet + 25 <= g.chips);
    set_key(3, "Clear", kClear, idle && g.bet > 0);
    for (int k = 0; k < 4; ++k) { lv_obj_set_size(keys[k], q, kh); lv_obj_set_pos(keys[k], pad + k * (q + gap), rows_y); }
    static char deal_t[24];
    if (g.broke()) {
        set_key(4, "New Chips (+500)", kRefill, idle, true);
    } else {
        snprintf(deal_t, sizeof deal_t, "Deal (Bet %ld)", long(g.bet));
        set_key(4, deal_t, kDeal, idle && g.can_deal(), true);
    }
    set_key(5, nullptr, 0, false);
    lv_obj_set_size(keys[4], W, kh);
    lv_obj_set_pos(keys[4], pad, rows_y + kh + gap);
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
    for (int k = 0; k < 6; ++k) {
        keys[k] = make_key(scr, 10, kh, key_cb, k);
        key_l[k] = key_label(keys[k], "", menu_font());
    }
    // Cards: big enough for two rows of hands plus the dealer's
    const int table_h = rows_y - gap / 2 - bar.h, lh = lv_font_get_line_height(bar_font());
    ch = (table_h - 2 * lh - 30) / 2;
    cw = ch * 5 / 7;
    if (cw > (m.w - 40) / 3) { cw = (m.w - 40) / 3; ch = cw * 7 / 5; }
    update_status();
}

// ---- Stats screen ----------------------------------------------------------------------------
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
    if (!ok || sum.hands == 0) {
        overlay_text("No hands recorded yet. Every hand you play is listed here.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[12], b[12], c[12], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.hands);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.wins);
        snprintf(c, sizeof c, "%lu", (unsigned long)sum.losses);
        snprintf(d, sizeof d, "%lu", (unsigned long)sum.pushes);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Hands", "Won", "Lost", "Pushes"};
        static const int8_t pct[4] = {25, 25, 25, 25};
        table_show(t, head, pct, hf, bf);
        Table& t2 = scratch_table(1);
        table_clear(t2);
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.blackjacks);
        snprintf(b, sizeof b, "$%ld", long(sum.best_chips));
        snprintf(c, sizeof c, "%s$%ld", sum.net < 0 ? "-" : "", long(sum.net < 0 ? -sum.net : sum.net));
        snprintf(d, sizeof d, "%u", unsigned(S ? S->g.refills : 0));
        table_add(t2, a, b, c, d);
        const char* const head2[4] = {"Blackjacks", "Most", "Net", "Refills"};
        static const int8_t pct2[4] = {31, 23, 23, 23};
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
void menu_pick(int id)
{
    if (id == kit::kLevel0) {                  // New Game: a fresh 500 and a new shoe
        if (reveal_timer) { lv_timer_delete(reveal_timer); reveal_timer = nullptr; record_round(); }
        const uint16_t refills = S->g.refills;
        S->g = Game{};
        S->g.refills = refills;
        S->g.new_shoe(shell().random_seed ? shell().random_seed() : lv_tick_get());
        S->recorded = 1;
        dealer_shown = 0;
        save();
        build();
    } else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_back() { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Blackjack", nullptr, h, false, "Card Back");
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        S->g = Game{};
        S->g.new_shoe(shell().random_seed ? shell().random_seed() : lv_tick_get());
        S->recorded = 1;
    }
    dealer_shown = S->g.phase == Phase::Playing ? 1 : S->g.dealer.n;
    if (!S->recorded) record_round();
    build();
}

void close()
{
    if (!S) return;
    if (reveal_timer) { lv_timer_delete(reveal_timer); reveal_timer = nullptr; }
    record_round();
    save();
    bar = kit::TopBar{};
    table = nullptr;
    for (auto& k : keys) k = nullptr;
    for (auto& k : key_l) k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t) {}

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
    snprintf(buf, cap, "$%ld in chips", long(st->g.chips));
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: an Ace and a King, fanned
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 58 / 100, h = size * 80 / 100;
    cards::draw_face(layer, a.x1, a.y1, w, h, cards::make(1, cards::Spades));
    cards::draw_face(layer, a.x1 + size - w, a.y1 + size - h, w, h, cards::make(13, cards::Hearts));
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
extern const GameOps blackjack_ops;
const GameOps blackjack_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
