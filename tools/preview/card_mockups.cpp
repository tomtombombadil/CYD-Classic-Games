// Preview only: mockups of card-game tables drawn with the shared card
// module (src/games/common/cards.*), for Tom to judge sizes and looks
// before the games are built. Nothing here ships.
#include <cstdio>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace cards;
int which = 0;
int W = 0, H = 0, top = 0;

uint8_t C(int rank, int suit) { return make(rank, suit); }

void face(lv_layer_t* l, int x, int y, int w, int h, uint8_t c, bool sel = false) { draw_face(l, x, y, w, h, c, sel); }
void back(lv_layer_t* l, int x, int y, int w, int h, int b = BackBombadil) { draw_back(l, x, y, w, h, b); }

void label(lv_layer_t* l, const char* s, int x, int y, int w, int h, lv_color_t c)
{
    kit::text(l, s, ui::bar_font(), c, x, y, w, h);
}

// ---- 0: the card sheet -------------------------------------------------------------------
void sheet(lv_layer_t* l)
{
    const bool large = W >= 320;
    // Klondike-size cards, every rank in two suits
    const int cw = large ? 41 : 31, ch = cw * 7 / 5, gap = 3;
    int x = 4, y = top + 4;
    const uint8_t row1[7] = {C(1, 0), C(2, 1), C(3, 2), C(4, 3), C(5, 0), C(6, 1), C(7, 2)};
    const uint8_t row2[7] = {C(8, 3), C(9, 0), C(10, 1), C(11, 2), C(12, 3), C(13, 0), C(10, 3)};
    for (int i = 0; i < 7; ++i) face(l, x + i * (cw + gap), y, cw, ch, row1[i]);
    y += ch + gap;
    for (int i = 0; i < 7; ++i) face(l, x + i * (cw + gap), y, cw, ch, row2[i], i == 2);
    y += ch + 8;
    // All 12 backs, 4 per row: pictures first, then the patterns
    const int bw = W >= 320 ? 56 : 42, bh = bw * 7 / 5, bg = (W - 8 - 4 * bw) / 3;
    for (int k = 0; k < kBacks; ++k) {
        const int bx = 4 + (k % 4) * (bw + bg), byy = y + (k / 4) * (bh + 4);
        if (byy + bh > H) break;
        back(l, bx, byy, bw, bh, k);
    }
}

// ---- 1: Klondike -------------------------------------------------------------------------------
void klondike(lv_layer_t* l)
{
    const int m = W >= 320 ? 4 : 2;
    const int pitch = (W - 2 * m) / 7, cw = pitch - (W >= 320 ? 4 : 3), ch = cw * 7 / 5;
    const int x0 = m + (W - 2 * m - 7 * pitch) / 2 + (pitch - cw) / 2;
    int y = top + 4;
    auto col_x = [&](int i) { return x0 + i * pitch; };
    // Stock, waste (3 fanned), gap, foundations
    back(l, col_x(0), y, cw, ch, BackBombadil);
    const int fan = (col_x(3) - col_x(1) - cw - 2) / 2;    // three waste cards, each index showing
    face(l, col_x(1), y, cw, ch, C(4, 3));
    face(l, col_x(1) + fan, y, cw, ch, C(9, 1));
    face(l, col_x(1) + 2 * fan, y, cw, ch, C(12, 0), true);
    draw_slot(l, col_x(3), y, cw, ch, Spades);
    face(l, col_x(4), y, cw, ch, C(3, 1));
    face(l, col_x(5), y, cw, ch, C(1, 2));
    draw_slot(l, col_x(6), y, cw, ch, Clubs);
    y += ch + (W >= 320 ? 10 : 6);
    // Tableau: face-down cards overlap tightly, face-up show their index strip
    const int down = ch / 7 > 5 ? ch / 7 : 5, up = index_h(cw, ch) + 2;
    static const uint8_t ups[7][6] = {
        {C(13, 3), C(12, 1), C(11, 0), C(10, 2), 0xFF},
        {C(6, 0), 0xFF}, {C(9, 3), C(8, 2), C(7, 3), 0xFF}, {C(11, 1), 0xFF},
        {C(5, 2), C(4, 0), 0xFF}, {C(2, 0), 0xFF}, {C(8, 0), C(7, 1), C(6, 3), C(5, 1), 0xFF}};
    static const int downs[7] = {0, 1, 2, 3, 3, 5, 4};
    for (int i = 0; i < 7; ++i) {
        int yy = y;
        for (int d = 0; d < downs[i]; ++d, yy += down) back(l, col_x(i), yy, cw, ch, BackBombadil);
        for (int k = 0; ups[i][k] != 0xFF; ++k, yy += up) face(l, col_x(i), yy, cw, ch, ups[i][k]);
    }
}

// ---- 2: FreeCell ------------------------------------------------------------------------------
void freecell(lv_layer_t* l)
{
    const int m = W >= 320 ? 3 : 1;
    const int pitch = (W - 2 * m) / 8, cw = pitch - 2, ch = cw * 7 / 5;
    const int x0 = m + (W - 2 * m - 8 * pitch) / 2 + 1;
    int y = top + 4;
    // 4 free cells, 4 foundations
    face(l, x0, y, cw, ch, C(13, 1));
    draw_slot(l, x0 + pitch, y, cw, ch);
    face(l, x0 + 2 * pitch, y, cw, ch, C(7, 0));
    draw_slot(l, x0 + 3 * pitch, y, cw, ch);
    face(l, x0 + 4 * pitch, y, cw, ch, C(2, 0));
    face(l, x0 + 5 * pitch, y, cw, ch, C(4, 1));
    draw_slot(l, x0 + 6 * pitch, y, cw, ch, Diamonds);
    face(l, x0 + 7 * pitch, y, cw, ch, C(1, 3));
    y += ch + 6;
    const int up = index_h(cw, ch) + 2;
    static const uint8_t cols[8][8] = {
        {C(9, 3), C(5, 2), C(12, 0), C(8, 1), C(3, 3), C(11, 2), 0xFF},
        {C(6, 1), C(2, 2), C(10, 0), C(13, 3), C(4, 0), C(9, 2), C(8, 3), 0xFF},
        {C(1, 1), C(7, 3), C(11, 1), C(3, 2), 0xFF},
        {C(12, 2), C(6, 0), C(5, 3), C(10, 1), C(9, 0), C(8, 2), C(7, 1), 0xFF},
        {C(4, 3), C(13, 0), C(2, 1), C(5, 0), C(11, 3), 0xFF},
        {C(3, 0), C(12, 3), C(6, 2), C(10, 3), C(9, 1), 0xFF},
        {C(13, 2), C(1, 0), C(7, 2), C(4, 2), C(12, 1), C(11, 0), C(10, 2), 0xFF},
        {C(8, 0), C(3, 1), C(5, 1), C(6, 3), C(2, 3), 0xFF}};
    for (int i = 0; i < 8; ++i) {
        int yy = y;
        for (int k = 0; cols[i][k] != 0xFF; ++k, yy += up) {
            const bool last = cols[i][k + 1] == 0xFF;
            face(l, x0 + i * pitch, yy, cw, ch, cols[i][k], i == 3 && last);
        }
    }
}

// ---- 3: Blackjack -----------------------------------------------------------------------------
void blackjack(lv_layer_t* l)
{
    const bool large = W >= 320;
    const int cw = large ? 66 : 50, ch = cw * 7 / 5, fan = cw + (large ? 8 : 6);   // side by side while they fit
    const lv_color_t ink = ui::pal().stone_light;
    int y = top + 6;
    label(l, "Dealer", 0, y, W, 20, ink);
    y += large ? 26 : 20;
    int x = (W - (cw + fan)) / 2;
    face(l, x, y, cw, ch, C(10, 3));
    back(l, x + fan, y, cw, ch, BackMoon);
    y += ch + (large ? 30 : 16);
    label(l, "You: 17", 0, y - (large ? 26 : 16), W, 16, ink);
    x = (W - (cw + 2 * fan)) / 2;
    face(l, x, y, cw, ch, C(1, 1));
    face(l, x + fan, y, cw, ch, C(2, 0));
    face(l, x + 2 * fan, y, cw, ch, C(4, 2));
    y += ch + (large ? 16 : 8);
    label(l, "Bet 10    Chips 240", 0, y, W, 18, ink);
}

// ---- 4: Video poker -------------------------------------------------------------------------
void poker(lv_layer_t* l)
{
    const bool large = W >= 320;
    const int gap = large ? 6 : 4, cw = (W - 8 - 4 * gap) / 5, ch = cw * 7 / 5;
    const lv_color_t ink = ui::pal().stone_light, gold = ui::pal().lit;
    int y = top + 6;
    static const char* const pays[] = {"Royal Flush 250", "Straight Flush 50", "Four of a Kind 25",
                                       "Full House 9", "Flush 6", "Straight 4", "Three of a Kind 3",
                                       "Two Pair 2", "Jacks or Better 1"};
    const int ph = large ? 20 : 15;
    for (int i = 0; i < 9; ++i)
        kit::text(l, pays[i], large ? &lv_font_montserrat_14 : &lv_font_montserrat_12, i == 6 ? gold : ink,
                  0, y + i * ph, W, ph);
    y += 9 * ph + (large ? 14 : 8);
    static const uint8_t hand[5] = {C(7, 1), C(7, 3), C(12, 2), C(7, 0), C(2, 1)};
    static const bool held[5] = {true, true, false, true, false};
    for (int i = 0; i < 5; ++i) {
        const int x = 4 + i * (cw + gap);
        face(l, x, y, cw, ch, hand[i], held[i]);
        if (held[i]) kit::text(l, "Held", large ? &lv_font_montserrat_14 : &lv_font_montserrat_12, gold,
                               x, y + ch + 2, cw, 16);
    }
}

// ---- 5: Solitaire laid out for landscape (Undo/Hint up in the bar) ----------------
void klondike_landscape(lv_layer_t* l)
{
    const int pitch = W / 7, cw = pitch - 4, ch = cw * 7 / 5;
    auto col_x = [&](int i) { return 2 + i * pitch + 2; };
    int y = top + 3;
    back(l, col_x(0), y, cw, ch, BackLattice);
    const int fan = (col_x(3) - col_x(1) - cw - 2) / 2;
    face(l, col_x(1), y, cw, ch, C(4, 3));
    face(l, col_x(1) + fan, y, cw, ch, C(9, 1));
    face(l, col_x(1) + 2 * fan, y, cw, ch, C(12, 0), true);
    face(l, col_x(3), y, cw, ch, C(3, 0));
    face(l, col_x(4), y, cw, ch, C(1, 1));
    draw_slot(l, col_x(5), y, cw, ch, Clubs);
    draw_slot(l, col_x(6), y, cw, ch, Diamonds);
    y += ch + 4;
    const int room = H - y - 2;
    static const uint8_t ups[7][7] = {
        {C(13, 3), C(12, 1), C(11, 0), C(10, 2), C(9, 3), C(8, 1), 0xFF},
        {C(6, 0), 0xFF}, {C(9, 3), C(8, 2), C(7, 3), 0xFF}, {C(11, 1), 0xFF},
        {C(5, 2), C(4, 0), 0xFF}, {C(2, 0), 0xFF}, {C(8, 0), C(7, 1), C(6, 3), C(5, 1), 0xFF}};
    static const int downs[7] = {0, 1, 2, 3, 4, 5, 6};
    for (int i = 0; i < 7; ++i) {
        int nu = 0; while (ups[i][nu] != 0xFF) ++nu;
        int down = ch / 7, up = index_h(cw, ch) + 2;
        if (downs[i] * down + (nu - 1) * up + ch > room && nu > 1) up = (room - ch - downs[i] * down) / (nu - 1);
        int yy = y;
        for (int d = 0; d < downs[i]; ++d, yy += down) back(l, col_x(i), yy, cw, ch, BackLattice);
        for (int k = 0; k < nu; ++k, yy += up) face(l, col_x(i), yy, cw, ch, ups[i][k]);
    }
}

void draw_cb(lv_event_t* e)
{
    lv_layer_t* l = lv_event_get_layer(e);
    kit::fill_rect(l, 0, top, W - 1, H - 1, felt(), 0);
    switch (which) {
        case 0: sheet(l); break;
        case 1: klondike(l); break;
        case 2: freecell(l); break;
        case 3: blackjack(l); break;
        case 4: poker(l); break;
        case 5: klondike_landscape(l); break;
    }
}

} // namespace

// Build mockup screen `w` (0 sheet, 1 Klondike, 2 FreeCell, 3 Blackjack, 4 Poker)
void card_mockup(int w, const char* title, const char* status)
{
    which = w;
    W = lv_display_get_horizontal_resolution(nullptr);
    H = lv_display_get_vertical_resolution(nullptr);
    kit::screen_begin();
    kit::TopBar bar = kit::top_bar([](lv_event_t*) {});
    lv_label_set_text(bar.left, title);
    kit::top_bar_status(bar, status);
    top = bar.h;
    lv_obj_t* o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, W, H);
    lv_obj_add_event_cb(o, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_move_to_index(o, 0);
}
