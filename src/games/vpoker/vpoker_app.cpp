// Video Poker (Jacks or Better): registry entry, save file and screen.
// Rules in vpoker_core.*; cards in common/cards.*.
//
// Screen: top bar (credits, what's happening, ☰); the table: the pay table
// for the current bet (the hand you hold lights up), then the five cards -
// tap a card (or anywhere in its column) to hold it: it turns gold and says
// HELD; keys: [Bet One] [Bet Max] [Hint] / [Deal] or [Draw].
// Bet Max sets 5 credits and deals at once, like the machines.
// Sounds: quiet while playing (card games); a win trills, four of a kind
// and better plays the fanfare. A losing hand is silent (it is most hands).
#include <cstdio>
#include <new>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "vpoker_core.h"

namespace {

using namespace vpoker;
using namespace ui;

constexpr const char* kId = "vpoker";

Game*       G = nullptr;
kit::TopBar bar;
lv_obj_t*   table = nullptr;
lv_obj_t*   keys[4] = {};
lv_obj_t*   key_l[4] = {};
int         cw = 0, ch = 0, cards_y = 0, cards_x0 = 0, card_gap = 0, row_h = 0;

void build();
void open_menu();
void update_status();

// ---- Save ---------------------------------------------------------------------------------
void save()
{
    if (!G || !shell().save_game) return;
    uint8_t buf[Game::kSaveBytes];
    const size_t n = G->serialize(buf, sizeof buf);
    if (n) shell().save_game(kId, buf, n);
}

bool load(Game& g)
{
    uint8_t buf[Game::kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    return n == sizeof buf && g.deserialize(buf, n);
}

void record()
{
    Record r{G->bet, G->last_rank, G->last_win, G->credits};
    char body[64];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
}

// ---- Drawing ---------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!G) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Metrics& M = metrics();
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt());
    const lv_color_t ink = contrast_text(cards::felt());
    // Pay table in two columns, best hand first; the hand that counts now lights up
    static const char* const kShort[kRanks] = {"", "Jacks or Better", "Two Pair", "3 of a Kind", "Straight",
                                               "Flush", "Full House", "4 of a Kind", "Straight Flush", "Royal Flush"};
    const lv_font_t* f = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int lit = G->phase == Phase::Done ? G->last_rank : G->phase == Phase::Dealt ? G->current() : -1;
    const int pad = M.large ? 8 : 4, colgap = M.large ? 12 : 6;
    const int colw = (lv_area_get_width(&a) - 2 * pad - colgap) / 2;
    for (int k = 0; k < kRanks - 1; ++k) {
        const int r = RoyalFlush - k;
        const int col = k < 5 ? 0 : 1, row = k < 5 ? k : k - 5;
        const int x = a.x1 + pad + col * (colw + colgap);
        const int y = a.y1 + 4 + row * row_h;
        char amt[12];
        snprintf(amt, sizeof amt, "%d", pay(r, G->bet));
        const bool on = r == lit && r != Nothing;
        if (on) kit::fill_rect(layer, x - 2, y, x + colw + 1, y + row_h - 1, P.lit, 3);
        const lv_color_t c = on ? contrast_text(P.lit) : ink;
        const int lh = lv_font_get_line_height(f), ty = y + (row_h - lh) / 2;
        const int aw = text_width(amt, f);
        kit::text(layer, kShort[r], f, c, x + 2, ty, text_width(kShort[r], f) + 2, lh);   // left-aligned
        kit::text(layer, amt, f, c, x + colw - aw - 2, ty, aw + 2, lh);
    }
    // The cards
    const lv_font_t* hf = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    for (int i = 0; i < 5; ++i) {
        const int x = cards_x0 + i * (cw + card_gap);
        if (G->phase == Phase::Ready)                           // nothing dealt yet
            cards::draw_back(layer, x, cards_y, cw, ch);
        else
            cards::draw_face(layer, x, cards_y, cw, ch, G->hand[i], G->phase == Phase::Dealt && ((G->held >> i) & 1));
        if (G->phase == Phase::Dealt && ((G->held >> i) & 1)) {
            const int lh = lv_font_get_line_height(hf);
            kit::text(layer, "HELD", hf, P.lit, x - card_gap, cards_y + ch + 3, cw + 2 * card_gap, lh);
        }
    }
}

void press_cb(lv_event_t* e)
{
    if (!G || overlay_open() || G->phase != Phase::Dealt) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (p.y < cards_y - 4) return;                          // the pay table
    const int i = (p.x - cards_x0 + card_gap / 2) / (cw + card_gap);
    if (i < 0 || i > 4) return;
    G->toggle_hold(i);
    save();
    lv_obj_invalidate(table);
    update_status();
}

// ---- Keys and status ---------------------------------------------------------------------
enum KeyId : intptr_t { kBetOne = 0, kBetMax, kHint, kMain };

void update_status()
{
    if (!bar.center) return;
    char s[48], left[24], sh[24] = "";
    snprintf(left, sizeof left, "%ld", long(G->credits));
    lv_label_set_text(bar.left, left);
    if (G->phase == Phase::Dealt) {
        const Rank r = G->current();
        if (r != Nothing) snprintf(s, sizeof s, "%s", rank_name(r));
        else { snprintf(s, sizeof s, "Hold, then Draw"); snprintf(sh, sizeof sh, "Hold, Draw"); }
    } else if (G->phase == Phase::Done) {
        if (G->last_win) {
            snprintf(s, sizeof s, "%s! Win %ld", rank_name(G->last_rank), long(G->last_win));
            snprintf(sh, sizeof sh, "Win %ld", long(G->last_win));
        }
        else snprintf(s, sizeof s, "No win");
    } else {
        snprintf(s, sizeof s, "Bet %d", int(G->bet));
    }
    kit::top_bar_status(bar, s, sh[0] ? sh : nullptr);
    const bool dealt = G->phase == Phase::Dealt;
    char bt[16];
    snprintf(bt, sizeof bt, "Bet %d", int(G->bet));
    lv_label_set_text(key_l[kBetOne], dealt ? "Bet One" : bt);
    set_dim(keys[kBetOne], dealt);
    set_dim(keys[kBetMax], dealt || G->credits < 1);
    set_dim(keys[kHint], !dealt);
    if (dealt) lv_label_set_text(key_l[kMain], "Draw");
    else if (G->broke()) lv_label_set_text(key_l[kMain], "New Credits (+500)");
    else lv_label_set_text(key_l[kMain], "Deal");
    set_dim(keys[kMain], !dealt && !G->broke() && !G->can_deal());
    set_checked(keys[kMain], true);
}

void finish_hand()
{
    record();
    if (G->last_rank >= FourKind) { sound(Sound::Fanfare); kit::flash(); }
    else if (G->last_win) sound(Sound::Trill);
}

void deal_now()
{
    if (G->bet > G->credits) G->bet = uint8_t(G->credits < kMaxBet ? G->credits : kMaxBet);
    if (!G->deal(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u)) {
        sound(Sound::Error);
        return;
    }
    save();
    lv_obj_invalidate(table);
    update_status();
}

void key_cb(lv_event_t* e)
{
    if (!G || overlay_open()) return;
    const int k = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const bool dealt = G->phase == Phase::Dealt;
    switch (k) {
        case kBetOne:
            if (dealt) return;
            G->bet_one();
            if (G->bet > G->credits && G->credits >= 1) G->bet = 1;
            break;
        case kBetMax:
            if (dealt || G->credits < 1) return;
            G->bet = uint8_t(G->credits < kMaxBet ? G->credits : kMaxBet);
            deal_now();
            return;
        case kHint:
            if (!dealt) return;
            G->held = hint(G->hand);                      // quiet, like every card game
            break;
        case kMain:
            if (dealt) { G->draw(); finish_hand(); }
            else if (G->broke()) G->refill();
            else { deal_now(); return; }
            break;
    }
    save();
    lv_obj_invalidate(table);
    update_status();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); }, 2);
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    const int kh = menu_btn_h();
    const int rows_y = m.h - pad - 2 * kh - gap;
    table = lv_obj_create(scr);
    lv_obj_remove_style_all(table);
    const int table_h = rows_y - gap / 2 - bar.h;
    lv_obj_set_size(table, m.w, table_h);
    lv_obj_set_pos(table, 0, bar.h);
    lv_obj_set_clickable(table, true);
    lv_obj_add_event_cb(table, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(table, press_cb, LV_EVENT_PRESSED, nullptr);
    // Pay table rows, then the cards with room for HELD under them
    const lv_font_t* f = m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    row_h = lv_font_get_line_height(f) + (m.large ? 6 : 3);
    const int held_h = lv_font_get_line_height(m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14) + 4;
    card_gap = m.large ? 8 : 5;
    cw = (m.w - 2 * pad - 4 * card_gap) / 5;
    ch = cw * 7 / 5;
    const int room = table_h - 4 - 5 * row_h - 8 - held_h;
    if (ch > room) { ch = room; cw = ch * 5 / 7; }
    cards_x0 = (m.w - (5 * cw + 4 * card_gap)) / 2;
    cards_y = bar.h + 4 + 5 * row_h + 8 + (room - ch) / 2;     // relative to the screen
    // Keys: [Bet One] [Bet Max] [Hint] / [Deal or Draw]
    const int W = m.w - 2 * pad, third = (W - 2 * gap) / 3;
    const char* names[4] = {"Bet One", "Bet Max", "Hint", "Deal"};
    for (int k = 0; k < 4; ++k) {
        keys[k] = make_key(scr, k == kMain ? W : third, kh, key_cb, k);
        key_l[k] = key_label(keys[k], names[k], menu_font());
        if (k < 3) lv_obj_set_pos(keys[k], pad + k * (third + gap), rows_y);
        else lv_obj_set_pos(keys[k], pad, rows_y + kh + gap);
    }
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
    Summary* sum = new (std::nothrow) Summary();
    const bool ok = sum && shell().stats_read && shell().stats_read(kId, stats_line, sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum->hands == 0) {
        overlay_text("No hands recorded yet. Every hand you play is listed here.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[16], b[16], c[16], d[24];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum->hands);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum->wins);
        snprintf(c, sizeof c, "%ld", long(sum->net));
        snprintf(d, sizeof d, "%ld", long(sum->best_credits));
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Hands", "Won", "Net", "Most"};
        static const int8_t pct[4] = {25, 25, 25, 25};
        table_show(t, head, pct, hf, bf);
        // How often each winning hand came up
        Table& t2 = scratch_table(1);
        table_clear(t2);
        for (int r = RoyalFlush; r >= JacksOrBetter; --r) {
            if (!sum->count[r] && r < FourKind) continue;
            char n[12];
            snprintf(n, sizeof n, "%lu", (unsigned long)sum->count[r]);
            table_add(t2, rank_name(r), n, "", "");
        }
        const char* const head2[4] = {"Hand", "Times", "", ""};
        static const int8_t pct2[4] = {70, 30, 0, 0};
        table_show(t2, head2, pct2, hf, hf);
    }
    delete sum;
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.", shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    const int bh = menu_btn_h(), gap = large ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && shell().stats_delete_last && shell().stats_clear) {
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
    if (id == kit::kLevel0) {                   // New Game: a fresh 500
        const uint16_t refills = G->refills;
        *G = Game{};
        G->refills = refills;
        save();
        build();
    } else if (id == kit::kOptions) cards::back_screen(open_menu);
}
void menu_back() { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Video Poker", nullptr, h, false, "Card Back");
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    G = new (std::nothrow) Game();
    if (!G) { app_go_home(); return; }
    if (!load(*G)) *G = Game{};
    build();
}

void close()
{
    if (!G) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    table = nullptr;
    for (auto& k : keys) k = nullptr;
    for (auto& k : key_l) k = nullptr;
    delete G;
    G = nullptr;
}

void tick(uint32_t) {}

void restyle() { if (G) build(); }

bool summary(char* buf, size_t cap)
{
    Game tmp;
    const Game* g = G;
    if (!g) { if (!load(tmp)) return false; g = &tmp; }
    snprintf(buf, cap, "%ld credits", long(g->credits));
    return true;
}

void save_now() { save(); }

// Icon: a royal flush fanned (A K Q of hearts)
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 50 / 100, h = size * 72 / 100;
    const int step = (size - w) / 2;
    for (int k = 0; k < 3; ++k)
        cards::draw_face(layer, a.x1 + k * step, a.y1 + (size - h) / 2 + (k == 1 ? -4 : 0), w, h,
                         cards::make(k == 0 ? 1 : 14 - k, cards::Hearts));
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
extern const GameOps vpoker_ops;
const GameOps vpoker_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
