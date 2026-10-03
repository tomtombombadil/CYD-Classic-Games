#include "fourconnect_screen.h"

#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace fourconnect_ui {

using namespace fourconnect;

namespace {

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
int       cell = 0;

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.frame, cell / 4);
    const uint64_t wins = B->result() == 0 || B->result() == 1 ? B->winning_cells() : 0;
    const int last = B->last_col();
    const int r_hole = cell * 41 / 100;
    for (int c = 0; c < kCols; ++c)
        for (int r = 0; r < kRows; ++r) {
            const int cx = a.x1 + c * cell + cell / 2;
            const int cy = a.y1 + (kRows - 1 - r) * cell + cell / 2;
            const int who = B->cell(c, r);
            const lv_color_t col = who == 0 ? P.piece_a : who == 1 ? P.piece_b : P.screen;
            kit::fill_circle(layer, cx, cy, r_hole, col);
            if (who >= 0 && (wins & bit_of(c, r)))
                kit::ring(layer, cx, cy, r_hole, cell >= 40 ? 5 : 4, P.win);
            else if (who >= 0 && c == last && r == B->height[c] - 1)
                kit::fill_circle(layer, cx, cy, cell / 9, P.screen);     // last move: a dot
        }
}

void press_cb(lv_event_t* e)
{
    if (!B || !match::human_may_move()) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int c = (p.x - a.x1) / cell;
    c = c < 0 ? 0 : c >= kCols ? kCols - 1 : c;
    if (B->can_play(c)) match::human_move(c);
}

void icon_draw_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const ui::Palette& P = ui::pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int size = lv_area_get_width(&a);
    const int n = 4, c = size / n;
    const int ox = a.x1 + (size - n * c) / 2, oy = a.y1 + (size - n * c) / 2;
    kit::fill_rect(layer, ox, oy, ox + n * c - 1, oy + n * c - 1, P.frame, c / 3);
    // A little game in progress: who = 0 red, 1 yellow, -1 empty (top row first)
    static const int8_t art[4][4] = {{-1, -1, -1, -1}, {-1, 0, -1, -1}, {-1, 1, 0, -1}, {1, 0, 1, 0}};
    for (int r = 0; r < n; ++r)
        for (int k = 0; k < n; ++k) {
            const int w = art[r][k];
            kit::fill_circle(layer, ox + k * c + c / 2, oy + r * c + c / 2, c * 40 / 100,
                             w == 0 ? P.piece_a : w == 1 ? P.piece_b : P.screen);
        }
}

} // namespace

void screen_build(Board& b)
{
    B = &b;
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const ui::Metrics& m = ui::metrics();
    const int margin = m.large ? 4 : 2;
    const int by_w = (m.w - 2 * margin) / kCols;
    const int by_h = (bottom - top) / kRows;
    cell = by_w < by_h ? by_w : by_h;
    board_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, cell * kCols, cell * kRows);
    lv_obj_set_pos(board_obj, (m.w - cell * kCols) / 2, top + (bottom - top - cell * kRows) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_set_scrollable(board_obj, false);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, press_cb, LV_EVENT_PRESSED, nullptr);
}

void screen_destroy()
{
    board_obj = nullptr;
    B = nullptr;
}

void board_redraw()
{
    if (board_obj) lv_obj_invalidate(board_obj);
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

} // namespace fourconnect_ui
