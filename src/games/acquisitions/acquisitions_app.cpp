// Acquisitions: registry entry, save file and screen. Rules and computer
// players in acquisitions_core.*.
//
// Screen (portrait): the 12 x 9 board on top (read-only: your playable
// tiles are edged in gold, the last tile laid has a dark ring), the seven
// hotels as chips (letter, size, share price), a line saying what just
// happened, and a key row at the bottom: your six tiles to lay, then Undo /
// End Game / Done while buying (tap a hotel chip to buy a share). Founding a
// hotel, picking a merger's survivor and deciding about a defunct hotel's
// shares are pages of their own. Tapping a chip any other time shows the
// Stocks page: every hotel's size, price, shares left and who holds what.
//
// You play against Ada, Max and Zoe; they act one step at a time so their
// moves can be followed. Sounds: your tile (Place), theirs (Turn), a bonus
// paid to you (Trill), a tile that can't go down (Error), the end (Win /
// Lose). Buying is silent.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include "acquisitions_core.h"
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace acq;
using namespace ui;

constexpr const char* kId = "acquisitions";
const char* const kNames[kPlayers] = {"You", "Ada", "Max", "Zoe"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};

struct State {
    Game     g;
    uint8_t  level = 1;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
    char     log[5][48] = {};          // newest first (not saved)
    uint8_t  bought[kMaxBuy] = {};     // your buys this turn, for Undo
    uint8_t  bought_n = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 7;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;
lv_obj_t*   keys = nullptr;
int         cs = 19, bx = 0, by = 0;           // board
int         chip_y = 0, chip_w = 0, chip_h = 0, chip_gap = 0, chip_x = 0;
int         msg_y = 0, msg_h = 0;
uint32_t    next_at = 0, last_save_ms = 0, now_ms = 0;
enum class Page : uint8_t { None, Found, Survivor, Dispose, Stocks, Menu };
Page        page = Page::None;
int         d_sell = 0, d_trade = 0;           // the Dispose page's choice

void open_menu();
void update();

// ---- Save ----------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = S->level;
    buf[n + 1] = S->recorded;
    for (int k = 0; k < 4; ++k) buf[n + 2 + k] = uint8_t(S->seconds >> (8 * k));
    buf[n + 6] = 0;
    shell().save_game(kId, buf, sizeof buf);
}

bool load(State& st)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    if (n != kSaveBytes || !st.g.deserialize(buf, n)) return false;
    const size_t b = Game::kSaveBytes;
    st.level = buf[b] < 3 ? buf[b] : 1;
    st.recorded = buf[b + 1] ? 1 : 0;
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[b + 2 + k]) << (8 * k);
    return true;
}

// ---- Helpers -------------------------------------------------------------------------------
void money(char* buf, size_t cap, int32_t v)
{
    const bool neg = v < 0;
    uint32_t u = uint32_t(neg ? -v : v);
    if (u >= 1000) snprintf(buf, cap, "%s$%lu,%03lu", neg ? "-" : "", (unsigned long)(u / 1000), (unsigned long)(u % 1000));
    else           snprintf(buf, cap, "%s$%lu", neg ? "-" : "", (unsigned long)u);
}

lv_color_t chain_color(int c)
{
    const Palette& P = pal();
    switch (c) {
        case 0: return P.piece_b;                               // Sunrise: gold
        case 1: return P.sq_dark;                               // Oakwood: brown
        case 2: return P.frame;                                 // Harbor: blue
        case 3: return P.felt;                                  // Meadow: green
        case 4: return lv_color_mix(P.felt, P.frame, 110);      // Lagoon: teal
        case 5: return lv_color_mix(P.piece_a, P.frame, 120);   // Royal: purple
        default: return P.piece_a;                              // Crimson: red
    }
}

lv_color_t ink_on(lv_color_t bg)
{
    return lv_color_luminance(bg) > 150 ? pal().stone_dark : pal().stone_light;
}

void log_add(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void log_add(const char* fmt, ...)
{
    if (!S) return;
    for (int i = 4; i > 0; --i) memcpy(S->log[i], S->log[i - 1], sizeof S->log[0]);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(S->log[0], sizeof S->log[0], fmt, ap);
    va_end(ap);
}

bool my_turn() { return S && S->g.phase != Phase::Over && S->g.actor() == 0; }

// ---- Stats ---------------------------------------------------------------------------------
void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

int my_place()
{
    uint8_t order[kPlayers];
    S->g.ranking(order);
    for (int i = 0; i < kPlayers; ++i) if (order[i] == 0) return i + 1;
    return kPlayers;
}

void record()
{
    Record r;
    r.place = uint8_t(my_place());
    r.money = S->g.final_money(0);
    r.level = S->level;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

// ---- Board drawing -------------------------------------------------------------------------
void area_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int x0 = a.x1 + bx, y0 = a.y1 + by;
    // Your tiles that can go down
    bool mine[kTiles] = {};
    for (int i = 0; i < kHand; ++i) {
        const uint8_t t = g.p[0].hand[i];
        if (t != kNone && g.tile_state(t) == TileState::Ok) mine[t] = true;
    }
    kit::fill_rect(layer, x0 - 2, y0 - 2, x0 + kCols * cs + 1, y0 + kRows * cs + 1, P.key_border, 3);
    const lv_font_t* small = large ? &lv_font_montserrat_10 : &lv_font_montserrat_8;
    const lv_font_t* letter = large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    const lv_color_t empty = lv_color_mix(P.cell, P.screen, 200), loose = lv_color_mix(P.stone_light, P.stone_dark, 150);
    for (int t = 0; t < kTiles; ++t) {
        const int x = x0 + (t % kCols) * cs, y = y0 + (t / kCols) * cs;
        const uint8_t v = g.board[t];
        char nm[6];
        if (v == 0) {
            kit::fill_rect(layer, x + 1, y + 1, x + cs - 2, y + cs - 2, empty, 2);
            tile_name(t, nm, sizeof nm);
            if (mine[t] && g.phase != Phase::Over) {
                kit::fill_rect(layer, x, y, x + cs - 1, y + cs - 1, P.lit, 3);
                kit::fill_rect(layer, x + 2, y + 2, x + cs - 3, y + cs - 3, empty, 2);
                kit::text(layer, nm, small, P.ink, x, y, cs, cs);
            } else {
                kit::text(layer, nm, small, lv_color_mix(P.muted, empty, 150), x, y, cs, cs);
            }
        } else if (v == 1) {
            kit::fill_rect(layer, x + 1, y + 1, x + cs - 2, y + cs - 2, loose, 3);
        } else {
            const lv_color_t c = chain_color(v - 2);
            kit::fill_rect(layer, x + 1, y + 1, x + cs - 2, y + cs - 2, c, 3);
            char l[2] = {chain_letter(v - 2), 0};
            kit::text(layer, l, letter, ink_on(c), x, y, cs, cs);
        }
        if (t == g.last_tile && g.phase != Phase::Over) {
            const lv_color_t r = P.ink;
            kit::fill_rect(layer, x, y, x + cs - 1, y + 1, r);
            kit::fill_rect(layer, x, y + cs - 2, x + cs - 1, y + cs - 1, r);
            kit::fill_rect(layer, x, y, x + 1, y + cs - 1, r);
            kit::fill_rect(layer, x + cs - 2, y, x + cs - 1, y + cs - 1, r);
        }
    }
    // The hotels
    const lv_font_t* f1 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const lv_font_t* f2 = large ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    const bool buying = my_turn() && g.phase == Phase::Buy;
    for (int c = 0; c < kChains; ++c) {
        const int x = a.x1 + chip_x + c * (chip_w + chip_gap), y = a.y1 + chip_y;
        const bool on = g.active(c);
        const lv_color_t bg = on ? chain_color(c) : P.key;
        const bool can = buying && on && g.bank(c) > 0 && g.p[0].cash >= g.price(c) && g.bought < kMaxBuy;
        if (can) kit::fill_rect(layer, x - 2, y - 2, x + chip_w + 1, y + chip_h + 1, P.ink, 6);
        kit::fill_rect(layer, x, y, x + chip_w - 1, y + chip_h - 1, bg, 5);
        const lv_color_t ink = on ? ink_on(bg) : P.muted;
        char l1[8], l2[10];
        if (on) snprintf(l1, sizeof l1, "%c %d", chain_letter(c), g.size(c));
        else    snprintf(l1, sizeof l1, "%c", chain_letter(c));
        const int h1 = lv_font_get_line_height(f1), h2 = lv_font_get_line_height(f2);
        const int ty = y + (chip_h - h1 - h2) / 2;
        kit::text(layer, l1, f1, ink, x, ty, chip_w, h1);
        if (on) {
            snprintf(l2, sizeof l2, "$%d", g.price(c));
            kit::text(layer, l2, f2, ink, x, ty + h1, chip_w, h2);
        } else {
            kit::text(layer, "-", f2, ink, x, ty + h1, chip_w, h2);
        }
    }
    // What just happened
    kit::text(layer, S->log[0], large ? &lv_font_montserrat_14 : &lv_font_montserrat_12, P.ink,
              a.x1 + 2, a.y1 + msg_y, lv_area_get_width(&a) - 4, msg_h);
    // The end: standings over the board
    if (g.phase == Phase::Over) {
        uint8_t order[kPlayers];
        g.ranking(order);
        const lv_font_t* tf = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const lv_font_t* lf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
        const int th = lv_font_get_line_height(tf), lh = lv_font_get_line_height(lf) + 2;
        const int bw = kCols * cs - 2 * cs, bh = th + kPlayers * lh + (large ? 18 : 12);
        const int bx1 = x0 + cs, by1 = y0 + (kRows * cs - bh) / 2;
        kit::fill_rect(layer, bx1 - 2, by1 - 2, bx1 + bw + 1, by1 + bh + 1, order[0] == 0 ? P.lit : P.stone_dark, 8);
        kit::fill_rect(layer, bx1, by1, bx1 + bw - 1, by1 + bh - 1, P.cell, 7);
        char t[32];
        if (order[0] == 0) snprintf(t, sizeof t, "You Win!");
        else snprintf(t, sizeof t, "%s Wins", kNames[order[0]]);
        kit::text(layer, t, tf, P.ink, bx1, by1 + 4, bw, th);
        for (int i = 0; i < kPlayers; ++i) {
            char m[16], line[40];
            money(m, sizeof m, g.p[order[i]].cash);
            snprintf(line, sizeof line, "%d. %s  %s", i + 1, kNames[order[i]], m);
            kit::text(layer, line, lf, order[i] == 0 ? P.ink : P.muted, bx1, by1 + 6 + th + i * lh, bw, lh);
        }
    }
}

// ---- Pages ---------------------------------------------------------------------------------
void close_page()
{
    page = Page::None;
    close_overlays();
}

void found_cb(lv_event_t* e)
{
    const int c = int(intptr_t(lv_event_get_user_data(e)));
    if (!S || !S->g.found(c)) return;
    log_add("You found %s", chain_name(c));
    sound(Sound::Place);
    close_page();
    save();
    update();
}

void open_found()
{
    page = Page::Found;
    overlay_begin("Found a Hotel");
    overlay_text("Your tile starts a new hotel. Pick which one; you get a free share.", false);
    for (int c = 0; c < kChains; ++c) {
        if (S->g.active(c)) continue;
        char t[40];
        static const char* const tiers[3] = {"cheap", "middle", "dear"};
        snprintf(t, sizeof t, "%s  (%s, $%d)", chain_name(c), tiers[chain_tier(c)], price_for(c, 2));
        overlay_button(overlay(), t, found_cb, c);
    }
}

void survivor_cb(lv_event_t* e)
{
    const int c = int(intptr_t(lv_event_get_user_data(e)));
    if (!S) return;
    int32_t before[kPlayers];
    for (int i = 0; i < kPlayers; ++i) before[i] = S->g.p[i].cash;
    if (!S->g.choose_survivor(c)) return;
    log_add("%s takes over", chain_name(c));
    if (S->g.p[0].cash > before[0]) sound(Sound::Trill);
    close_page();
    save();
    update();
}

void open_survivor()
{
    page = Page::Survivor;
    overlay_begin("Merger");
    overlay_text("Your tile joins hotels of the same size. Pick the one that survives.", false);
    const Game& g = S->g;
    int big = 0;
    for (int i = 0; i < g.defunct_n; ++i) if (g.defunct_size[i] > big) big = g.defunct_size[i];
    for (int i = 0; i < g.defunct_n; ++i) {
        if (g.defunct_size[i] != big) continue;
        char t[40];
        snprintf(t, sizeof t, "%s  (you hold %d)", chain_name(g.defunct[i]), g.p[0].shares[g.defunct[i]]);
        overlay_button(overlay(), t, survivor_cb, g.defunct[i]);
    }
}

lv_obj_t* d_info = nullptr;
void dispose_text()
{
    if (!d_info || !S) return;
    const Game& g = S->g;
    const int c = g.defunct[g.defunct_i], have = g.p[0].shares[c];
    char t[96];
    snprintf(t, sizeof t, "Sell %d   Trade %d for %d   Keep %d", d_sell, d_trade, d_trade / 2, have - d_sell - d_trade);
    lv_label_set_text(d_info, t);
}

void dispose_cb(lv_event_t* e)
{
    const int k = int(intptr_t(lv_event_get_user_data(e)));
    if (!S) return;
    Game& g = S->g;
    const int c = g.defunct[g.defunct_i], have = g.p[0].shares[c];
    const int room = g.bank(g.survivor) * 2;
    switch (k) {
        case 0: if (d_sell > 0) --d_sell; break;
        case 1: if (d_sell + d_trade < have) ++d_sell; break;
        case 2: if (d_trade >= 2) d_trade -= 2; break;
        case 3: if (d_sell + d_trade + 2 <= have && d_trade + 2 <= room) d_trade += 2; break;
        case 4: d_sell = have; d_trade = 0; break;
        case 5: {
            const int sell = d_sell, trade = d_trade;
            const int32_t expect = g.p[0].cash + sell * price_for(c, g.defunct_size[g.defunct_i]);
            if (!g.dispose(sell, trade)) return;
            log_add("You sell %d, trade %d, keep %d %s", sell, trade, have - sell - trade, chain_name(c));
            if (g.p[0].cash > expect) sound(Sound::Trill);        // the next defunct hotel paid you a bonus
            close_page();
            save();
            update();
            return;
        }
    }
    dispose_text();
}

lv_obj_t* page_row()
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

void page_key(lv_obj_t* r, const char* t, lv_event_cb_t cb, intptr_t id, bool primary = false)
{
    lv_obj_t* b = make_key(r, 10, menu_btn_h(), cb, id);
    lv_obj_set_flex_grow(b, 1);
    key_label(b, t, menu_font());
    if (primary) lv_obj_add_state(b, LV_STATE_CHECKED);
}

void open_dispose()
{
    page = Page::Dispose;
    const Game& g = S->g;
    const int c = g.defunct[g.defunct_i], have = g.p[0].shares[c];
    d_sell = 0; d_trade = 0;
    char title[24];
    snprintf(title, sizeof title, "%s Is Gone", chain_name(c));
    overlay_begin(title);
    char t[160];
    snprintf(t, sizeof t, "%s joined %s. You hold %d %s shares: sell them for $%d each, trade two for one %s share ($%d), or keep them.",
             chain_name(c), chain_name(g.survivor), have, chain_name(c), price_for(c, g.defunct_size[g.defunct_i]),
             chain_name(g.survivor), g.price(g.survivor));
    overlay_text(t, false);
    lv_obj_t* r1 = page_row();
    page_key(r1, "Sell -", dispose_cb, 0);
    page_key(r1, "Sell +", dispose_cb, 1);
    lv_obj_t* r2 = page_row();
    page_key(r2, "Trade -", dispose_cb, 2);
    page_key(r2, "Trade +", dispose_cb, 3);
    d_info = overlay_text("", false);
    dispose_text();
    lv_obj_t* r3 = page_row();
    page_key(r3, "Sell All", dispose_cb, 4);
    page_key(r3, "Done", dispose_cb, 5, true);
}

// The Stocks page: every hotel, and who holds what
void stocks_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int lh = lv_font_get_line_height(f) + (large ? 6 : 4);
    const int w = lv_area_get_width(&a);
    const int name_w = w * 31 / 100, col = (w - name_w) / 6, sw = lh * 6 / 10;
    const char* const head[6] = {"$", "Left", "You", "Ada", "Max", "Zoe"};
    int y = a.y1;
    for (int k = 0; k < 6; ++k) kit::text(layer, head[k], f, P.muted, a.x1 + name_w + k * col, y, col, lh);
    y += lh;
    for (int c = 0; c < kChains; ++c) {
        const bool on = g.active(c);
        kit::fill_rect(layer, a.x1, y + (lh - sw) / 2, a.x1 + sw - 1, y + (lh + sw) / 2 - 1, chain_color(c), 2);
        kit::text_left(layer, chain_name(c), f, on ? P.ink : P.muted, a.x1 + sw + 3, y, name_w - sw - 3, lh);
        char v[6][10];
        snprintf(v[0], 10, "%d", g.price(c));
        snprintf(v[1], 10, "%d", g.bank(c));
        for (int i = 0; i < kPlayers; ++i) snprintf(v[2 + i], 10, "%d", g.p[i].shares[c]);
        for (int k = 0; k < 6; ++k) {
            const bool zero = !strcmp(v[k], "0");
            kit::text(layer, zero ? "-" : v[k], f, zero ? P.muted : P.ink, a.x1 + name_w + k * col, y, col, lh);
        }
        y += lh;
    }
}
void stocks_back_cb(lv_event_t*) { close_page(); update(); }

void open_stocks()
{
    page = Page::Stocks;
    overlay_begin("Stocks");
    const bool large = metrics().large;
    const int lh = lv_font_get_line_height(large ? &lv_font_montserrat_14 : &lv_font_montserrat_12) + (large ? 6 : 4);
    lv_obj_t* o = lv_obj_create(overlay());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, lv_pct(100), lh * 8 + 4);
    lv_obj_add_event_cb(o, stocks_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    for (int r = 0; r < 2; ++r) {
        char line[96] = "", m[16];
        snprintf(line, sizeof line, "%s:", r == 0 ? "Cash" : "Worth");
        for (int i = 0; i < kPlayers; ++i) {
            money(m, sizeof m, r == 0 ? S->g.p[i].cash : S->g.worth(i));
            const size_t n = strlen(line);
            snprintf(line + n, sizeof line - n, "%s %s %s", i ? "," : "", kNames[i], m);
        }
        overlay_text(line, r == 1);
    }
    for (int i = 0; i < 4 && S->log[i][0]; ++i) overlay_text(S->log[i], i > 0);
    overlay_back(stocks_back_cb, 0);
}

// ---- Keys ----------------------------------------------------------------------------------
enum KeyId : intptr_t { kTile0 = 0, kNoTile = 10, kUndo = 11, kEnd = 12, kDone = 13, kAgain = 14, kStocks = 15 };

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    const int k = int(intptr_t(lv_event_get_user_data(e)));
    Game& g = S->g;
    if (k == kAgain) {
        S->g.start(shell().random_seed ? shell().random_seed() : lv_tick_get());
        S->recorded = 0; S->seconds = 0;
        for (auto& l : S->log) l[0] = 0;
        log_add("A new game: %s starts", kNames[S->g.turn]);
        next_at = now_ms + 800;
        save(); update();
        return;
    }
    if (k == kStocks) { open_stocks(); return; }
    if (!my_turn()) return;
    if (k >= kTile0 && k < kTile0 + kHand && g.phase == Phase::Play) {
        const uint8_t t = g.p[0].hand[k];
        if (t == kNone) return;
        if (g.tile_state(t) != TileState::Ok) { sound(Sound::Error); log_add("That tile can't go down now"); update(); return; }
        int32_t before[kPlayers];
        for (int i = 0; i < kPlayers; ++i) before[i] = g.p[i].cash;
        char nm[6];
        tile_name(t, nm, sizeof nm);
        g.play(k);
        S->bought_n = 0;
        if (g.phase == Phase::Dispose || g.phase == Phase::Survivor) log_add("You lay %s: a merger!", nm);
        else if (g.phase == Phase::Found) log_add("You lay %s", nm);
        else if (g.board[t] >= 2) log_add("You lay %s: %s grows to %d", nm, chain_name(g.board[t] - 2), g.size(g.board[t] - 2));
        else log_add("You lay %s", nm);
        if (g.p[0].cash > before[0]) sound(Sound::Trill); else sound(Sound::Place);
        save(); update();
        return;
    }
    if (k == kNoTile && g.phase == Phase::Play) { g.skip_play(); S->bought_n = 0; log_add("No tile of yours fits"); update(); return; }
    if (g.phase != Phase::Buy) return;
    if (k == kUndo && S->bought_n > 0) { g.unbuy(S->bought[--S->bought_n]); update(); return; }
    if (k == kEnd) { if (g.call_end()) log_add("You call the end: last buys"); update(); return; }
    if (k == kDone) {
        if (S->bought_n) {
            char t[48] = "You buy";
            for (int i = 0; i < S->bought_n; ++i) {
                const size_t n = strlen(t);
                snprintf(t + n, sizeof t - n, "%s %c", i ? "," : "", chain_letter(S->bought[i]));
            }
            log_add("%s", t);
        }
        S->bought_n = 0;
        g.buy_done();
        if (g.phase == Phase::Over) {
            if (!S->recorded) record();
            sound(my_place() == 1 ? Sound::Win : Sound::Lose);
        }
        next_at = now_ms + 700;
        save(); update();
    }
}

void add_key(int w, const char* t, intptr_t id, bool primary, bool dim)
{
    lv_obj_t* b = make_key(keys, w, menu_btn_h(), key_cb, id);
    key_label(b, t, menu_font());
    if (primary) lv_obj_add_state(b, LV_STATE_CHECKED);
    if (dim) set_dim(b, true);
}

void build_keys()
{
    if (!keys || !S) return;
    lv_obj_clean(keys);
    const Game& g = S->g;
    const Metrics& m = metrics();
    const int gap = m.large ? 6 : 4, w = lv_obj_get_width(keys);
    if (g.phase == Phase::Over) { add_key(w, "Play Again", kAgain, true, false); return; }
    if (!my_turn()) { add_key(w, "Stocks", kStocks, false, false); return; }
    if (g.phase == Phase::Play) {
        if (!g.has_playable(0)) { add_key(w, "No Tile Fits", kNoTile, true, false); return; }
        const int kw = (w - (kHand - 1) * gap) / kHand;
        for (int i = 0; i < kHand; ++i) {
            const uint8_t t = g.p[0].hand[i];
            char nm[6] = "";
            if (t != kNone) tile_name(t, nm, sizeof nm);
            add_key(kw, nm, kTile0 + i, false, t == kNone || g.tile_state(t) != TileState::Ok);
        }
        return;
    }
    if (g.phase == Phase::Buy) {
        const bool end = g.can_end() && !g.end_called;
        const int n = end ? 3 : 2, kw = (w - (n - 1) * gap) / n;
        add_key(kw, "Undo", kUndo, false, S->bought_n == 0);
        if (end) add_key(kw, "End Game", kEnd, false, false);
        add_key(kw, g.end_called ? "Finish" : "Done", kDone, true, false);
    }
}

// ---- Status --------------------------------------------------------------------------------
void update()
{
    if (!S || !bar.center) return;
    Game& g = S->g;
    char l[16], s[48], sh[24];
    money(l, sizeof l, g.p[0].cash);
    lv_label_set_text(bar.left, l);
    sh[0] = 0;
    if (g.phase == Phase::Over) { snprintf(s, sizeof s, "Game over"); snprintf(sh, sizeof sh, "Over"); }
    else if (g.actor() == 0) {
        switch (g.phase) {
            case Phase::Play: snprintf(s, sizeof s, "Your turn: lay a tile"); snprintf(sh, sizeof sh, "Lay a tile"); break;
            case Phase::Buy:
                snprintf(s, sizeof s, "Tap hotels to buy (%d left)", kMaxBuy - g.bought);
                snprintf(sh, sizeof sh, "Buy: %d left", kMaxBuy - g.bought);
                break;
            default: snprintf(s, sizeof s, "Your choice"); break;
        }
    } else snprintf(s, sizeof s, "%s's turn", kNames[g.actor()]);
    kit::top_bar_status(bar, s, sh[0] ? sh : nullptr);
    build_keys();
    if (area) lv_obj_invalidate(area);
    // Your choices open their pages
    if (my_turn() && !overlay_open()) {
        if (g.phase == Phase::Found) open_found();
        else if (g.phase == Phase::Survivor) open_survivor();
        else if (g.phase == Phase::Dispose) open_dispose();
    }
}

// ---- The computers -------------------------------------------------------------------------
void computer_step()
{
    Game& g = S->g;
    if (g.phase == Phase::Over || overlay_open() || g.actor() == 0 || int32_t(now_ms - next_at) < 0) return;
    const int who = g.actor();
    const Phase ph = g.phase;
    int32_t before[kPlayers];
    for (int i = 0; i < kPlayers; ++i) before[i] = g.p[i].cash;
    uint8_t shares[kChains];
    memcpy(shares, g.p[who].shares, sizeof shares);
    const int lvl = S->level;
    uint32_t delay = 650;
    switch (ph) {
        case Phase::Play: {
            const int slot = g.ai_tile(lvl);
            if (slot < 0) { g.skip_play(); log_add("%s has no tile that fits", kNames[who]); break; }
            char nm[6];
            tile_name(g.p[who].hand[slot], nm, sizeof nm);
            const uint8_t t = g.p[who].hand[slot];
            g.play(slot);
            if (g.phase == Phase::Dispose || g.phase == Phase::Survivor) log_add("%s lays %s: a merger!", kNames[who], nm);
            else if (g.board[t] >= 2) log_add("%s lays %s: %s grows to %d", kNames[who], nm, chain_name(g.board[t] - 2), g.size(g.board[t] - 2));
            else log_add("%s lays %s", kNames[who], nm);
            sound(Sound::Turn);
            delay = 900;
            break;
        }
        case Phase::Found: {
            const int c = g.ai_found(lvl);
            g.found(c);
            log_add("%s founds %s", kNames[who], chain_name(c));
            break;
        }
        case Phase::Survivor: {
            const int c = g.ai_survivor(lvl);
            g.choose_survivor(c);
            log_add("%s keeps %s", kNames[who], chain_name(c));
            break;
        }
        case Phase::Dispose: {
            int sell = 0, trade = 0;
            const int c = g.defunct[g.defunct_i];
            const int have = g.p[who].shares[c];
            g.ai_dispose(lvl, sell, trade);
            if (!g.dispose(sell, trade)) { sell = have; trade = 0; g.dispose(sell, 0); }
            log_add("%s sells %d, trades %d, keeps %d %s", kNames[who], sell, trade, have - sell - trade, chain_name(c));
            break;
        }
        case Phase::Buy: {
            if (!g.end_called && g.ai_end(lvl) && g.call_end()) { log_add("%s calls the end of the game", kNames[who]); delay = 1200; break; }
            const int c = g.ai_buy(lvl);
            if (c >= 0 && g.buy(c)) { log_add("%s buys a %s share", kNames[who], chain_name(c)); delay = 500; break; }
            g.buy_done();
            if (g.phase == Phase::Over) {
                if (!S->recorded) record();
                sound(my_place() == 1 ? Sound::Win : Sound::Lose);
            }
            delay = 300;
            break;
        }
        case Phase::Over: break;
    }
    // Bonuses from a merger (or the game's end) paid to you
    if (ph != Phase::Buy && g.p[0].cash > before[0] && who != 0) {
        char m[16];
        money(m, sizeof m, g.p[0].cash - before[0]);
        log_add("You get a %s bonus!", m);
        sound(Sound::Trill);
    }
    next_at = now_ms + delay;
    save();
    update();
}

// ---- Screen --------------------------------------------------------------------------------
void chip_tap_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int y = p.y - a.y1, x = p.x - a.x1 - chip_x;
    if (y < chip_y - 4 || y > chip_y + chip_h + 4 || x < 0) return;
    const int c = x / (chip_w + chip_gap);
    if (c < 0 || c >= kChains) return;
    Game& g = S->g;
    if (my_turn() && g.phase == Phase::Buy && g.active(c)) {
        if (g.buy(c)) { S->bought[S->bought_n++] = uint8_t(c); update(); }
        else sound(Sound::Error);
        return;
    }
    open_stocks();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); }, 2);
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int top = bar.h, h = m.h - top;
    cs = (m.w - 2 * pad - 4) / kCols;
    bx = (m.w - kCols * cs) / 2;
    by = pad + 2;
    chip_gap = m.large ? 5 : 3;
    chip_w = (m.w - 2 * pad - (kChains - 1) * chip_gap) / kChains;
    chip_x = (m.w - (kChains * chip_w + (kChains - 1) * chip_gap)) / 2;
    chip_h = m.large ? 44 : 32;
    chip_y = by + kRows * cs + (m.large ? 12 : 7);
    msg_y = chip_y + chip_h + (m.large ? 8 : 4);
    msg_h = lv_font_get_line_height(m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12) + 2;
    // Room left: centre the lot between the top bar and the keys
    const int used = msg_y + msg_h, room = h - kh - 2 * pad;
    if (room > used) { const int d = (room - used) / 2; by += d; chip_y += d; msg_y += d; }

    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, h - kh - 2 * pad);
    lv_obj_set_pos(area, 0, top);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, area_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, chip_tap_cb, LV_EVENT_CLICKED, nullptr);

    keys = lv_obj_create(scr);
    lv_obj_remove_style_all(keys);
    lv_obj_set_size(keys, m.w - 2 * pad, kh);
    lv_obj_set_pos(keys, pad, m.h - pad - kh);
    lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(keys, m.large ? 6 : 4, 0);
    lv_obj_set_scrollable(keys, false);
    lv_obj_update_layout(keys);
    clock_ = kit::Clock{};
    page = Page::None;
    update();
}

// ---- Stats screen --------------------------------------------------------------------------
void stats_back_cb(lv_event_t*) { open_menu(); }
void stats_action_cb(lv_event_t* e);

void open_stats()
{
    page = Page::Menu;
    overlay_begin("Stats");
    static Summary sum;
    sum = Summary{};
    const bool ok = shell().stats_read && shell().stats_read(kId, stats_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum.games == 0) {
        overlay_text("No games recorded yet. Every finished game is listed here, and a game left part-way.", false);
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
            table_add(rt, s1, s2, kLevels[r.level < 3 ? r.level : 0], tm);
        }
        const char* const head2[4] = {"Place", "Money", "Level", "Time"};
        static const int8_t pct2[4] = {18, 32, 24, 26};
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

// ---- Menu ---------------------------------------------------------------------------------
void new_game(int level)
{
    // A game left part-way counts, at the place you stood
    if (!S->recorded && S->g.phase != Phase::Over && S->g.turns >= kPlayers) record();
    S->g.start(shell().random_seed ? shell().random_seed() : lv_tick_get());
    S->level = uint8_t(level);
    S->recorded = 0;
    S->seconds = 0;
    S->bought_n = 0;
    for (auto& l : S->log) l[0] = 0;
    log_add("A new game: %s starts", kNames[S->g.turn]);
    next_at = now_ms + 800;
    save();
}

void menu_pick(int id)
{
    if (id >= kit::kLevel0 && id <= kit::kLevel2) new_game(id - kit::kLevel0);
    if (id == kit::kOptions) { open_stocks(); return; }
    page = Page::None;
    update();
}
void menu_back() { page = Page::None; update(); }

void open_menu()
{
    page = Page::Menu;
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Acquisitions", kLevels, h, false, "Stocks");
}

// ---- Registry entry -----------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        *S = State{};
        S->g.start(shell().random_seed ? shell().random_seed() : lv_tick_get());
        log_add("A new game: %s starts", kNames[S->g.turn]);
    } else {
        log_add("Welcome back");
    }
    next_at = 0;
    build();
}

void close()
{
    if (!S) return;
    save();
    bar = kit::TopBar{};
    area = keys = nullptr;
    d_info = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    now_ms = now;
    if (next_at == 0) next_at = now + 800;
    computer_step();
    if (clock_.tick(now, S->g.phase != Phase::Over, S->seconds) && bar.center) {
        char l[16];
        money(l, sizeof l, S->g.p[0].cash);
        lv_label_set_text(bar.left, l);
    }
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
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
    char m[16];
    money(m, sizeof m, st->g.phase == Phase::Over ? st->g.p[0].cash : st->g.worth(0));
    if (st->g.phase == Phase::Over) snprintf(buf, cap, "Game over: %s", m);
    else snprintf(buf, cap, "%s, worth %s", kLevels[st->level], m);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a few hotel tiles in three chains, like a corner of the board
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), s = size / 4;
    kit::fill_rect(layer, a.x1, a.y1, a.x1 + size - 1, a.y1 + size - 1, P.key_border, size / 12);
    static const int8_t cells[16] = {-1, 2, 2, -1,  6, 6, 2, 0,  6, -2, 0, 0,  -1, 3, 3, -1};
    for (int i = 0; i < 16; ++i) {
        const int x = a.x1 + (i % 4) * s, y = a.y1 + (i / 4) * s;
        const int v = cells[i];
        const lv_color_t c = v >= 0 ? chain_color(v) : v == -2 ? lv_color_mix(P.stone_light, P.stone_dark, 150)
                                                                : lv_color_mix(P.cell, P.screen, 200);
        kit::fill_rect(layer, x + 1, y + 1, x + s - 2, y + s - 2, c, 2);
    }
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

// Preview staging (tools/preview)
namespace acq_preview {
acq::Game* game() { return S ? &S->g : nullptr; }
void redraw() { if (S) { next_at = now_ms + 600000; update(); } }       // computers wait
void hold(bool on) { if (S) next_at = on ? now_ms + 600000 : now_ms; }
void log(const char* t) { log_add("%s", t); if (area) lv_obj_invalidate(area); }
void stocks() { open_stocks(); }
}

namespace games {
extern const GameOps acquisitions_ops;
const GameOps acquisitions_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
