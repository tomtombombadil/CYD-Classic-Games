#include "board8.h"

#include "game_kit.h"
#include "ui/theme.h"

namespace board8 {

namespace {

lv_obj_t* obj = nullptr;
Config    C;
Marks     M;
int       cell = 0;

// Screen row/col <-> square
int sq_at(int row, int col) { return C.flipped ? row * 8 + (7 - col) : (7 - row) * 8 + col; }

void draw_cb(lv_event_t* e)
{
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const bool felt = C.style == Style::Felt;
    if (felt) {
        kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.felt, cell / 6);
        // Grid: thin dark lines between the squares
        for (int k = 1; k < 8; ++k) {
            kit::fill_rect(layer, a.x1 + k * cell, a.y1 + 2, a.x1 + k * cell, a.y2 - 2, P.stone_dark);
            kit::fill_rect(layer, a.x1 + 2, a.y1 + k * cell, a.x2 - 2, a.y1 + k * cell, P.stone_dark);
        }
    }
    for (int row = 0; row < 8; ++row)
        for (int col = 0; col < 8; ++col) {
            const int sq = sq_at(row, col);
            const int x = a.x1 + col * cell, y = a.y1 + row * cell;
            const uint64_t b = bit(sq);
            lv_color_t bg = felt ? P.felt : ((row + col) & 1) ? P.sq_dark : P.sq_light;
            bool tint = true;
            if (sq == M.selected)   bg = P.selected;
            else if (M.warn & b)    bg = P.conflict_bg;
            else if (M.last & b)    bg = felt ? lv_color_mix(P.same, P.felt, 110) : lv_color_mix(P.same, bg, 150);
            else tint = false;
            if (!felt || tint) {
                const int in = felt ? 1 : 0;
                kit::fill_rect(layer, x + in, y + in, x + cell - 1 - in + (felt ? 0 : 0), y + cell - 1, bg);
            }
            if (C.draw_piece) C.draw_piece(layer, sq, x + cell / 2, y + cell / 2, cell);
            if (M.targets & b) {
                const int r = cell / 7 > 2 ? cell / 7 : 2;
                kit::fill_circle(layer, x + cell / 2, y + cell / 2, r + 1, P.stone_light);
                kit::fill_circle(layer, x + cell / 2, y + cell / 2, r, felt ? P.lit : P.target);
            }
        }
}

int square_from_event(lv_event_t* e)
{
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return -1;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    int col = (p.x - a.x1) / cell, row = (p.y - a.y1) / cell;
    col = col < 0 ? 0 : col > 7 ? 7 : col;
    row = row < 0 ? 0 : row > 7 ? 7 : row;
    return sq_at(row, col);
}

// A tap counts where the stylus first came down: a resistive panel's last
// readings as the stylus lifts drift, so the release point can be a
// neighbouring square. The long-press here waits kLongMs - longer than
// LVGL's 400 ms, which caught Tom's firm stylus taps and turned them into
// peeks. A peek shows while held; its release is not a tap.
constexpr uint32_t kLongMs = 750;
int      pressed_sq = -1;
uint32_t pressed_at = 0;
bool     long_shown = false;

void press_cb(lv_event_t* e)
{
    pressed_sq = square_from_event(e);
    pressed_at = lv_tick_get();
    long_shown = false;
}

void end_long()
{
    if (!long_shown) return;
    long_shown = false;
    if (C.on_long_end) C.on_long_end();
}

void tap_cb(lv_event_t*)
{
    const int sq = pressed_sq;
    pressed_sq = -1;
    if (long_shown) { end_long(); return; }
    if (sq >= 0 && C.on_tap) C.on_tap(sq);
}

void lost_cb(lv_event_t*)
{
    end_long();
    pressed_sq = -1;
}

void pressing_cb(lv_event_t*)
{
    if (long_shown || pressed_sq < 0 || !C.on_long_press) return;
    if (lv_tick_elaps(pressed_at) < kLongMs) return;
    long_shown = true;
    C.on_long_press(pressed_sq);
}

} // namespace

lv_obj_t* create(lv_obj_t* parent, int x, int y, int w, int h, const Config& cfg)
{
    C = cfg;
    M = Marks{};
    cell = (w < h ? w : h) / 8;
    obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 8 * cell, 8 * cell);
    lv_obj_set_pos(obj, x + (w - 8 * cell) / 2, y + (h - 8 * cell) / 2);
    lv_obj_set_clickable(obj, true);
    lv_obj_set_scrollable(obj, false);
    lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(obj, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, tap_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(obj, lost_cb, LV_EVENT_PRESS_LOST, nullptr);
    if (cfg.on_long_press) lv_obj_add_event_cb(obj, pressing_cb, LV_EVENT_PRESSING, nullptr);
    pressed_sq = -1;
    long_shown = false;
    return obj;
}

void set_marks(const Marks& m) { M = m; redraw(); }
void set_flipped(bool f)       { C.flipped = f; redraw(); }
void redraw()                  { if (obj) lv_obj_invalidate(obj); }
void forget()                  { obj = nullptr; }
int  cell_size()               { return cell; }

bool square_center(int sq, int* x, int* y)
{
    if (!obj) return false;
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int rank = sq / 8, file = sq % 8;
    const int row = C.flipped ? rank : 7 - rank, col = C.flipped ? 7 - file : file;
    *x = a.x1 + col * cell + cell / 2;
    *y = a.y1 + row * cell + cell / 2;
    return true;
}

} // namespace board8
