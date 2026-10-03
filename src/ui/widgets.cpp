#include "widgets.h"

#include <cstdio>
#include "theme.h"

namespace ui {

namespace {

Metrics M;

lv_style_t st_key, st_key_pressed, st_key_checked, st_key_dim;
bool styles_inited = false;

lv_obj_t* overlay_obj = nullptr;
void (*overlay_closer)() = nullptr;

int pad() { return M.large ? 16 : 10; }

} // namespace

// ---- Screen size ---------------------------------------------------------------
void metrics_update()
{
    lv_display_t* d = lv_display_get_default();
    M.w = lv_display_get_horizontal_resolution(d);
    M.h = lv_display_get_vertical_resolution(d);
    M.large = M.w >= 300;
}

const Metrics& metrics() { return M; }

// ---- Styles ------------------------------------------------------------------------
void styles_apply()
{
    const Palette& p = pal();
    if (!styles_inited) {
        styles_inited = true;
        lv_style_init(&st_key);
        lv_style_init(&st_key_pressed);
        lv_style_init(&st_key_checked);
        lv_style_init(&st_key_dim);
    }
    lv_style_set_bg_color(&st_key, p.key);
    lv_style_set_bg_opa(&st_key, LV_OPA_COVER);
    lv_style_set_border_color(&st_key, p.key_border);
    lv_style_set_border_width(&st_key, 1);
    lv_style_set_radius(&st_key, 6);
    lv_style_set_text_color(&st_key, p.ink);
    lv_style_set_pad_all(&st_key, 0);

    lv_style_set_bg_color(&st_key_pressed, p.key_pressed);
    lv_style_set_bg_opa(&st_key_pressed, LV_OPA_COVER);

    lv_style_set_bg_color(&st_key_checked, p.key_on);
    lv_style_set_bg_opa(&st_key_checked, LV_OPA_COVER);
    lv_style_set_border_color(&st_key_checked, p.key_on);
    lv_style_set_text_color(&st_key_checked, p.key_on_text);

    lv_style_set_text_color(&st_key_dim, p.key_dim_text);

    lv_obj_report_style_change(nullptr);
}

lv_obj_t* make_key(lv_obj_t* parent, int w, int h, lv_event_cb_t cb, intptr_t user)
{
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_key, 0);
    lv_obj_add_style(b, &st_key_pressed, LV_STATE_PRESSED);
    lv_obj_add_style(b, &st_key_checked, LV_STATE_CHECKED);
    lv_obj_set_size(b, w, h);
    lv_obj_set_clickable(b, true);
    lv_obj_set_scrollable(b, false);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(user));
    return b;
}

lv_obj_t* key_label(lv_obj_t* key, const char* text, const lv_font_t* font)
{
    lv_obj_t* l = lv_label_create(key);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_center(l);
    return l;
}

void set_checked(lv_obj_t* o, bool on)
{
    if (!o) return;
    if (on) lv_obj_add_state(o, LV_STATE_CHECKED);
    else    lv_obj_remove_state(o, LV_STATE_CHECKED);
}

void set_dim(lv_obj_t* o, bool dim)
{
    if (!o) return;
    if (dim) lv_obj_add_style(o, &st_key_dim, 0);
    else     lv_obj_remove_style(o, &st_key_dim, 0);
}

int text_width(const char* s, const lv_font_t* f)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

lv_obj_t* make_hamburger(lv_obj_t* parent, int w, int h, lv_event_cb_t cb, intptr_t user)
{
    lv_obj_t* b = lv_obj_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &st_key_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_size(b, w, h);
    lv_obj_set_clickable(b, true);
    lv_obj_set_scrollable(b, false);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(user));

    const int bar_w = h * 3 / 4 < 26 ? h * 3 / 4 : 26;
    const int bar_t = h >= 34 ? 3 : 2;
    const int step  = h >= 34 ? 7 : 5;
    for (int k = -1; k <= 1; ++k) {
        lv_obj_t* bar = lv_obj_create(b);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, bar_w, bar_t);
        lv_obj_set_style_bg_color(bar, pal().ink, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_clickable(bar, false);
        lv_obj_align(bar, LV_ALIGN_CENTER, 0, k * step);
    }
    return b;
}

int              menu_btn_h() { return M.large ? 50 : 32; }
const lv_font_t* menu_font()  { return M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }
const lv_font_t* bar_font()   { return M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }
const lv_font_t* title_font() { return M.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20; }

// ---- Overlays ------------------------------------------------------------------------
lv_obj_t* overlay_begin(const char* title, void (*on_close)())
{
    close_overlays();
    overlay_closer = on_close;
    overlay_obj = lv_obj_create(lv_layer_top());
    lv_obj_t* o = overlay_obj;
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, M.w, M.h);
    lv_obj_set_style_bg_color(o, pal().screen, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(o, pad(), 0);
    lv_obj_set_style_pad_row(o, M.large ? 10 : 6, 0);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_clickable(o, true);      // swallow taps behind it
    lv_obj_set_scrollable(o, false);

    lv_obj_t* t = lv_label_create(o);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, title_font(), 0);
    lv_obj_set_style_text_color(t, pal().ink, 0);
    return o;
}

lv_obj_t* overlay()      { return overlay_obj; }
bool      overlay_open() { return overlay_obj != nullptr; }

lv_obj_t* overlay_text(const char* s, bool muted)
{
    lv_obj_t* l = lv_label_create(overlay_obj);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, muted ? pal().muted : pal().ink, 0);
    return l;
}

lv_obj_t* overlay_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, intptr_t user,
                         bool primary)
{
    lv_obj_t* b = make_key(parent, lv_pct(100), menu_btn_h(), cb, user);
    if (primary) lv_obj_add_state(b, LV_STATE_CHECKED);
    key_label(b, text, menu_font());
    return b;
}

void overlay_pair(const char* a, lv_event_cb_t cb_a, intptr_t ida,
                  const char* b, lv_event_cb_t cb_b, intptr_t idb)
{
    lv_obj_t* row = lv_obj_create(overlay_obj);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_scrollable(row, false);
    const char* t[2] = {a, b};
    lv_event_cb_t cb[2] = {cb_a, cb_b};
    const intptr_t id[2] = {ida, idb};
    for (int k = 0; k < 2; ++k) {
        if (!t[k]) continue;
        lv_obj_t* btn = make_key(row, 10, menu_btn_h(), cb[k], id[k]);
        lv_obj_set_flex_grow(btn, 1);
        key_label(btn, t[k], menu_font());
    }
}

lv_obj_t* overlay_bottom_button(const char* text, lv_event_cb_t cb, intptr_t user)
{
    lv_obj_t* b = make_key(overlay_obj, lv_pct(100), menu_btn_h(), cb, user);
    lv_obj_add_state(b, LV_STATE_CHECKED);
    key_label(b, text, menu_font());
    lv_obj_set_ignore_layout(b, true);
    lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
    return b;
}

void overlay_exit_row(lv_event_cb_t cb, intptr_t exit_menu_id, intptr_t exit_game_id)
{
    lv_obj_t* row = lv_obj_create(overlay_obj);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_scrollable(row, false);
    lv_obj_set_ignore_layout(row, true);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* a = make_key(row, 10, menu_btn_h(), cb, exit_menu_id);
    lv_obj_set_flex_grow(a, 1);
    lv_obj_add_state(a, LV_STATE_CHECKED);
    key_label(a, "Exit Menu", menu_font());
    lv_obj_t* b = make_key(row, 10, menu_btn_h(), cb, exit_game_id);
    lv_obj_set_flex_grow(b, 1);
    key_label(b, "Exit Game", menu_font());
}

void close_overlays()
{
    if (overlay_closer) {
        void (*f)() = overlay_closer;
        overlay_closer = nullptr;
        f();
    }
    if (overlay_obj) {
        // Async: this often runs inside a click handler of a button that
        // lives on the overlay being removed.
        lv_obj_delete_async(overlay_obj);
        overlay_obj = nullptr;
    }
}

// ---- Tables ------------------------------------------------------------------------
void table_clear(Table& t) { t = Table{}; }

Table& scratch_table(int which)
{
    static Table* tables[2] = {nullptr, nullptr};
    which = which ? 1 : 0;
    if (!tables[which]) tables[which] = new Table();
    table_clear(*tables[which]);
    return *tables[which];
}

void table_add(Table& t, const char* a, const char* b, const char* c, const char* d)
{
    // Every column gets a line per row, even when the cell is empty, so the
    // columns stay lined up.
    const char* cells[4] = {a, b, c, d};
    for (int k = 0; k < 4; ++k) {
        const size_t room = sizeof t.col[k] - t.len[k];
        const int n = snprintf(t.col[k] + t.len[k], room, "%s%s", t.rows ? "\n" : "", cells[k]);
        if (n > 0 && size_t(n) < room) t.len[k] += n;
    }
    ++t.rows;
}

void table_show(const Table& t, const char* const head[4], const int8_t pct[4],
                const lv_font_t* head_font, const lv_font_t* body_font)
{
    for (int part = 0; part < 2; ++part) {
        lv_obj_t* row = lv_obj_create(overlay_obj);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_clickable(row, false);
        lv_obj_set_scrollable(row, false);
        if (part == 1) lv_obj_set_style_margin_top(row, M.large ? -6 : -4, 0);  // header hugs its rows
        for (int k = 0; k < 4; ++k) {
            lv_obj_t* l = lv_label_create(row);
            lv_label_set_text(l, part == 0 ? head[k] : t.col[k]);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_width(l, lv_pct(pct[k]));
            lv_obj_set_style_text_font(l, part == 0 ? head_font : body_font, 0);
            lv_obj_set_style_text_color(l, part == 0 ? pal().muted : pal().ink, 0);
            lv_obj_set_style_text_line_space(l, M.large ? 3 : 1, 0);
        }
    }
}

} // namespace ui
