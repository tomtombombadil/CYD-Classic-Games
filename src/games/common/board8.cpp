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

void tap_cb(lv_event_t* e)
{
    const int sq = square_from_event(e);
    if (sq >= 0 && C.on_tap) C.on_tap(sq);
}

void long_cb(lv_event_t* e)
{
    const int sq = square_from_event(e);
    if (sq >= 0 && C.on_long_press) C.on_long_press(sq);
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
    lv_obj_add_event_cb(obj, tap_cb, LV_EVENT_SHORT_CLICKED, nullptr);
    if (cfg.on_long_press) lv_obj_add_event_cb(obj, long_cb, LV_EVENT_LONG_PRESSED, nullptr);
    return obj;
}

void set_marks(const Marks& m) { M = m; redraw(); }
void set_flipped(bool f)       { C.flipped = f; redraw(); }
void redraw()                  { if (obj) lv_obj_invalidate(obj); }
void forget()                  { obj = nullptr; }
int  cell_size()               { return cell; }

} // namespace board8
