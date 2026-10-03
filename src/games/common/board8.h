// An 8x8 board drawn as one LVGL object, for Reversi, Checkers and Chess.
// Squares are numbered sq = rank * 8 + file, rank 0 at the bottom (White's
// side in chess); `flipped` turns the board so rank 7 is at the bottom
// (the player sits behind their own pieces).
//
// Taps: reported on release, with the square where the stylus came down
// (the lift-off readings of a resistive panel drift). A hold of 0.75 s is a
// long-press instead: "this piece's moves" show while held (Tom approved
// that for Chess and Checkers only) - on_long_press when it starts,
// on_long_end on release, which is then not a tap. The game keeps its
// selection across a peek.
#pragma once

#include <cstdint>
#include <lvgl.h>

namespace board8 {

enum class Style : uint8_t { Checkered, Felt };

struct Marks {
    int      selected = -1;      // picked piece (amber)
    uint64_t targets  = 0;       // where it may go (dots)
    uint64_t last     = 0;       // last move's squares (tint)
    uint64_t warn     = 0;       // e.g. a king in check (red tint)
};

struct Config {
    Style style = Style::Checkered;
    bool  flipped = false;
    // Draw the piece on `sq` (nothing if empty) centred in the square
    void (*draw_piece)(lv_layer_t* layer, int sq, int cx, int cy, int size) = nullptr;
    void (*on_tap)(int sq) = nullptr;
    void (*on_long_press)(int sq) = nullptr;   // nullptr = long-press does nothing
    void (*on_long_end)() = nullptr;           // the long-press was released
};

// Creates the board, as large as fits in w x h, centred in that area.
lv_obj_t* create(lv_obj_t* parent, int x, int y, int w, int h, const Config& cfg);
void      set_marks(const Marks& m);
void      set_flipped(bool flipped);
void      redraw();
void      forget();                     // the screen was cleaned
int       cell_size();
bool      square_center(int sq, int* x, int* y);   // screen point (preview taps)

inline uint64_t bit(int sq) { return 1ull << sq; }

} // namespace board8
