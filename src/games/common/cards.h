// Playing cards for every card game (Klondike, FreeCell, Blackjack,
// Poker, ...): one drawing module and one set of fonts, shared.
//
// A face is cream with the rank and a suit symbol, as large as fit:
//   - the index strip along the top (rank, then a small suit right after it,
//     top left) is what shows when cards overlap in a cascade or a fan, so
//     it is the part that must read well;
//   - below it, one big suit symbol (no pip patterns or court art: the
//     screens are too small for them).
// Red suits use the theme's red, black suits its dark ink. Faces stay
// cream in every theme (cards are paper); the table is the theme's felt.
//
// Backs: 4 patterns x 3 colors (Tom, 2026-10-03), chosen per player.
// Rank and suit glyphs: DejaVu Sans Condensed Bold / DejaVu Sans via
// lv_font_conv, card_font_{12,16,20,26,34,46}.c (THIRD_PARTY_NOTICES.md).
#pragma once

#include <cstdint>
#include <lvgl.h>

namespace cards {

enum Suit : uint8_t { Spades = 0, Hearts = 1, Diamonds = 2, Clubs = 3 };
// A card is 0..51: suit * 13 + (rank - 1), rank 1 = Ace .. 13 = King
inline uint8_t make(int rank, int suit) { return uint8_t(suit * 13 + rank - 1); }
inline int  rank_of(uint8_t c) { return c % 13 + 1; }
inline int  suit_of(uint8_t c) { return c / 13; }
inline bool is_red(uint8_t c)  { return suit_of(c) == Hearts || suit_of(c) == Diamonds; }
const char* rank_text(int rank);               // "A", "2" .. "10", "J", "Q", "K"
const char* suit_text(int suit);               // UTF-8 symbol

enum class Back : uint8_t { Lattice = 0, Stripes, Dots, Night };
constexpr int kBackPatterns = 4, kBackColors = 3;  // colors: blue, red, green
struct Look {
    uint8_t back = 0;                          // Back
    uint8_t color = 0;                         // 0 blue, 1 red, 2 green
};
const char* back_name(int pattern);            // "Lattice", ...
const char* back_color_name(int color);        // "Blue", ...

// Height of the index strip for a w x h card: in a cascade, show at least
// this much of each face-up card.
int index_h(int w, int h);

void draw_face(lv_layer_t* layer, int x, int y, int w, int h, uint8_t card, bool selected = false);
void draw_back(lv_layer_t* layer, int x, int y, int w, int h, Look look);
// An empty pile: a rounded outline, with a faint suit (foundations) or none
void draw_slot(lv_layer_t* layer, int x, int y, int w, int h, int suit = -1);
// The table under the cards
lv_color_t felt();

} // namespace cards
