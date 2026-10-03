#include "sudoku_board_view.h"

#include "ui/theme.h"

namespace sudoku_ui {

using namespace ui;

namespace {

struct BoardData {
    const sudoku::Game* g;
    int cell;
    BoardHighlight hl;
    void (*on_tap)(int);
};

void fill(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = c;
    d.bg_opa = LV_OPA_COVER;
    d.border_width = 0;
    d.radius = 0;
    lv_area_t a{x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

void text(lv_layer_t* layer, const char* s, const lv_font_t* font, lv_color_t c,
          int32_t x1, int32_t y1, int32_t w, int32_t h)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = font;
    d.color = c;
    d.align = LV_TEXT_ALIGN_CENTER;
    const int32_t lh = lv_font_get_line_height(font);
    const int32_t top = y1 + (h - lh) / 2;
    lv_area_t a{x1, top, x1 + w - 1, top + lh - 1};
    lv_draw_label(layer, &d, &a);
}

void draw_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto* bd = static_cast<BoardData*>(lv_obj_get_user_data(obj));
    if (!bd || !bd->g) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    const sudoku::Game& g = *bd->g;

    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int cell = bd->cell;
    const int ox = a.x1 + 1, oy = a.y1 + 1;   // 1px for the outer thick line
    const int sel = bd->hl.selected;
    const int hd  = bd->hl.digit;
    const Palette& P = pal();

    // Background = thin grid line color; cells are drawn 1px inset over it.
    fill(layer, a.x1, a.y1, a.x2, a.y2, P.line_thin);

    const lv_font_t* vfont = font_for_cell(cell);
    const lv_font_t* nfont = note_font_for_cell(cell);
    char buf[2] = {0, 0};

    for (int i = 0; i < sudoku::N; ++i) {
        const int r = i / 9, c = i % 9;
        const int x = ox + c * cell, y = oy + r * cell;
        const uint8_t v = g.value(i);
        const bool conflict = g.conflict(i);

        // Both sides of a clash get the red tint, givens included, so the
        // player can see what the wrong digit collides with.
        lv_color_t bg = P.cell;
        if (sel >= 0 && sudoku::same_unit(sel, i)) bg = P.peer;
        if (hd && v == hd) bg = P.same;
        if (i == sel) bg = P.selected;
        if (conflict) bg = P.conflict_bg;
        fill(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, bg);
        if (conflict && i == sel) {
            // Keep the red tint visible; show the selection as a frame.
            const lv_color_t f = P.selected;
            fill(layer, x + 1, y + 1, x + cell - 1, y + 2, f);
            fill(layer, x + 1, y + cell - 2, x + cell - 1, y + cell - 1, f);
            fill(layer, x + 1, y + 1, x + 2, y + cell - 1, f);
            fill(layer, x + cell - 2, y + 1, x + cell - 1, y + cell - 1, f);
        }

        if (v) {
            buf[0] = '0' + v;
            const lv_color_t col = conflict && !g.locked(i) ? P.conflict
                                 : g.given(i)  ? P.given
                                 : g.hinted(i) ? P.hinted : P.entry;
            text(layer, buf, vfont, col, x + 1, y + 1, cell - 1, cell - 1);
        } else if (g.notes(i)) {
            const int sub = (cell - 1) / 3;
            for (int d = 1; d <= 9; ++d) {
                if (!g.has_note(i, d)) continue;
                buf[0] = '0' + d;
                const int nr = (d - 1) / 3, nc = (d - 1) % 3;
                // A note matching the highlighted digit is drawn in ink so
                // candidates for that digit stand out.
                text(layer, buf, nfont, d == hd ? P.note_match : P.note,
                     x + 1 + nc * sub, y + 1 + nr * sub, sub, sub);
            }
        }
    }

    // Thick lines on box borders (2px, covering the thin line + 1px)
    for (int k = 0; k <= 9; k += 3) {
        const int bx = ox + k * cell, by = oy + k * cell;
        fill(layer, bx - 1, a.y1, bx, a.y2, P.line_thick);
        fill(layer, a.x1, by - 1, a.x2, by, P.line_thick);
    }
}

void press_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    auto* bd = static_cast<BoardData*>(lv_obj_get_user_data(obj));
    lv_indev_t* indev = lv_indev_active();
    if (!bd || !indev || !bd->on_tap) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    int c = (p.x - a.x1 - 1) / bd->cell;
    int r = (p.y - a.y1 - 1) / bd->cell;
    c = c < 0 ? 0 : c > 8 ? 8 : c;
    r = r < 0 ? 0 : r > 8 ? 8 : r;
    bd->on_tap(r * 9 + c);
}

void delete_cb(lv_event_t* e)
{
    lv_obj_t* obj = lv_event_get_target_obj(e);
    delete static_cast<BoardData*>(lv_obj_get_user_data(obj));
    lv_obj_set_user_data(obj, nullptr);
}

} // namespace

lv_obj_t* board_create(lv_obj_t* parent, const sudoku::Game* g, int cell, void (*on_tap)(int))
{
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 9 * cell + 2, 9 * cell + 2);
    lv_obj_set_clickable(obj, true);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_user_data(obj, new BoardData{g, cell, {}, on_tap});
    lv_obj_add_event_cb(obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(obj, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(obj, delete_cb, LV_EVENT_DELETE, nullptr);
    return obj;
}

void board_set_highlight(lv_obj_t* board, const BoardHighlight& h)
{
    auto* bd = static_cast<BoardData*>(lv_obj_get_user_data(board));
    if (!bd) return;
    bd->hl = h;
    lv_obj_invalidate(board);
}

void board_refresh(lv_obj_t* board) { lv_obj_invalidate(board); }

} // namespace sudoku_ui
