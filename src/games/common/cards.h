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
// Rank and suit glyphs: DejaVu Sans Condensed Bold / DejaVu Sans via
// lv_font_conv, card_font_{10,12,16,20,26,34,46}.c; the back's "B" is DejaVu
// Serif Bold, card_b_font_*.c (THIRD_PARTY_NOTICES.md).
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

// Backs (Tom, 2026-10-03): three pictures and three patterns in three
// colors. The player's choice is shared by every card game
// (ui::UiSettings::card_back); `back` below is an index into this list.
enum Back : uint8_t {
    BackBombadil = 0,          // a gold serif "B" on blue
    BackMoon,                  // a crescent moon and one star (top right) on black
    BackTree,                  // a green tree with a brown trunk
    BackLattice,               // + color: Lattice Blue (the default - Tom), Red, Green
    BackStripes = BackLattice + 3,
    BackDots = BackStripes + 3,
    kBacks = BackDots + 3,     // 12
};
const char* back_name(int back);               // "Bombadil", "Moon", "Lattice Red", ...
uint8_t     current_back();                    // the player's choice, from the settings

// Height of the index strip for a w x h card: in a cascade, show at least
// this much of each face-up card.
int index_h(int w, int h);

void draw_face(lv_layer_t* layer, int x, int y, int w, int h, uint8_t card, bool selected = false);
void draw_back(lv_layer_t* layer, int x, int y, int w, int h, int back);
// The player's back
inline void draw_back(lv_layer_t* layer, int x, int y, int w, int h) { draw_back(layer, x, y, w, h, current_back()); }
// All the backs in a 6 x 2 grid for an Options screen, the current one
// ringed; a tap picks a back and saves it for every card game.
lv_obj_t* back_picker(lv_obj_t* parent, int w);
// A whole screen for it (title, picker, the back's name, Back key)
void back_screen(void (*back)());

// The win show (Windows tradition): cards leave their piles one at a time
// and bounce down and off the screen, leaving trails. Only the flying
// card's new spot is redrawn each frame, so the trails cost no memory - the
// panel simply keeps the old pixels. A tap ends it and calls `done`.
struct Launch {
    int16_t x, y;              // where the card starts (its pile)
    uint8_t card;              // the card that flies
    uint8_t under;             // what that pile shows once it has left (0xFF = an empty
                               // pile outline, 0xFE = nothing: bare table)
};
void celebrate(const Launch* list, int n, int cw, int ch, void (*done)());
void celebrate_stop();         // end it without calling `done` (game closing)
bool celebrating();

// An empty pile: a rounded outline, with a faint suit (foundations) or none
void draw_slot(lv_layer_t* layer, int x, int y, int w, int h, int suit = -1);
// The table under the cards
lv_color_t felt();

} // namespace cards
