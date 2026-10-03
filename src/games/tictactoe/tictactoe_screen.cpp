#include "tictactoe_screen.h"

#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace tictactoe_ui {

using namespace tictactoe;

namespace {

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
int       cell = 0;

void draw_x(lv_layer_t* layer, int cx, int cy, int r, int w, lv_color_t c)
{
    kit::line(layer, cx - r, cy - r, cx + r, cy + r, w, c);
    kit::line(layer, cx - r, cy + r, cx + r, cy - r, w, c);
}

// Board + marks in a square at (x0, y0) with cells of `c` px
void draw_board(lv_layer_t* layer, const int8_t* cells, int x0, int y0, int c, int thick, const int* win)
{
    const ui::Palette& P = ui::pal();
    kit::fill_rect(layer, x0, y0, x0 + 3 * c - 1, y0 + 3 * c - 1, P.cell, c / 8);
    for (int k = 1; k < 3; ++k) {
        kit::fill_rect(layer, x0 + k * c - thick / 2, y0 + c / 10, x0 + k * c + (thick - 1) / 2,
                       y0 + 3 * c - c / 10, P.line_thick);
        kit::fill_rect(layer, x0 + c / 10, y0 + k * c - thick / 2, x0 + 3 * c - c / 10,
                       y0 + k * c + (thick - 1) / 2, P.line_thick);
    }
    const int r = c * 30 / 100, w = c >= 60 ? 9 : c >= 30 ? 6 : 3;
    for (int i = 0; i < 9; ++i) {
        const int cx = x0 + (i % 3) * c + c / 2, cy = y0 + (i / 3) * c + c / 2;
        if (cells[i] == 0) draw_x(layer, cx, cy, r, w, P.entry);
        else if (cells[i] == 1) kit::ring(layer, cx, cy, r + w / 2, w, P.piece_a);
    }
    if (win) {
        const int ax = x0 + (win[0] % 3) * c + c / 2, ay = y0 + (win[0] / 3) * c + c / 2;
        const int bx = x0 + (win[2] % 3) * c + c / 2, by = y0 + (win[2] / 3) * c + c / 2;
        kit::line(layer, ax, ay, bx, by, w + 2, P.win);
    }
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int line[3];
    const bool won = B->winning_line(line);
    draw_board(lv_event_get_layer(e), B->cell, a.x1, a.y1, cell, cell >= 80 ? 6 : 4, won ? line : nullptr);
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
    int c = (p.x - a.x1) / cell, r = (p.y - a.y1) / cell;
    c = c < 0 ? 0 : c > 2 ? 2 : c;
    r = r < 0 ? 0 : r > 2 ? 2 : r;
    if (B->can_play(r * 3 + c)) match::human_move(r * 3 + c);
}

void icon_draw_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int size = lv_area_get_width(&a), c = size / 3;
    static const int8_t art[9] = {0, -1, 1, -1, 0, -1, 1, -1, 0};
    static const int win[3] = {0, 4, 8};
    draw_board(lv_event_get_layer(e), art, a.x1 + (size - 3 * c) / 2, a.y1 + (size - 3 * c) / 2, c,
               c >= 20 ? 3 : 2, win);
}

} // namespace

void screen_build(Board& b)
{
    B = &b;
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const ui::Metrics& m = ui::metrics();
    const int margin = m.large ? 12 : 8;
    const int w = (m.w - 2 * margin) / 3, h = (bottom - top - 2 * margin) / 3;
    cell = w < h ? w : h;
    board_obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, 3 * cell, 3 * cell);
    lv_obj_set_pos(board_obj, (m.w - 3 * cell) / 2, top + (bottom - top - 3 * cell) / 2);
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

} // namespace tictactoe_ui
