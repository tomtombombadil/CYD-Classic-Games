// Building blocks shared by every screen: screen metrics, key-style buttons,
// the hamburger button, full-screen overlays (menus, settings, stats) and
// the column-label tables used by stats screens.
//
// Plain LVGL, no hardware, so it builds in the PC preview too.
#pragma once

#include <cstddef>
#include <cstdint>
#include <lvgl.h>

namespace ui {

// ---- Screen size -------------------------------------------------------------
// Read from the LVGL display; call metrics_update() before building a screen.
struct Metrics {
    int  w = 0, h = 0;
    bool large = false;                // 320-px-wide screens (3.5"/4.0")
};
void           metrics_update();
const Metrics& metrics();

// ---- Styles ------------------------------------------------------------------
// Set from the current palette; call again after a theme change.
void styles_apply();

// A key: rounded button with the key colors (pressed/checked states).
lv_obj_t* make_key(lv_obj_t* parent, int w, int h, lv_event_cb_t cb, intptr_t user);
lv_obj_t* key_label(lv_obj_t* key, const char* text, const lv_font_t* font);
void      set_checked(lv_obj_t* o, bool on);
void      set_dim(lv_obj_t* o, bool dim);      // grey text (finished / unavailable)
int       text_width(const char* s, const lv_font_t* f);

// The 3-line menu button for top bars.
lv_obj_t* make_hamburger(lv_obj_t* parent, int w, int h, lv_event_cb_t cb, intptr_t user);

// Fonts and heights for menus and top bars
int              menu_btn_h();
const lv_font_t* menu_font();
const lv_font_t* bar_font();
const lv_font_t* title_font();

// ---- Overlays ------------------------------------------------------------------
// A full-screen panel on the top layer, flex column, with a title. Only one
// is open at a time; opening one closes the previous. `on_close` (optional)
// runs when it closes, e.g. to stop a timer the overlay uses.
lv_obj_t* overlay_begin(const char* title, void (*on_close)() = nullptr);
lv_obj_t* overlay();                           // nullptr when none is open
bool      overlay_open();
lv_obj_t* overlay_text(const char* s, bool muted);
lv_obj_t* overlay_button(lv_obj_t* parent, const char* text, lv_event_cb_t cb, intptr_t user,
                         bool primary = false);
// Two buttons sharing a row (b may be nullptr: one full-width button).
void      overlay_pair(const char* a, lv_event_cb_t cb_a, intptr_t ida,
                       const char* b, lv_event_cb_t cb_b, intptr_t idb);
// A primary button pinned to the bottom of the overlay (outside the column flow)
lv_obj_t* overlay_bottom_button(const char* text, lv_event_cb_t cb, intptr_t user);
void      close_overlays();

// ---- Tables (stats screens) ------------------------------------------------------
// Each table is 4 column labels holding all rows ("\n"-separated) plus a
// header row: 8 objects per table however many rows it has, which keeps
// LVGL memory use flat.
struct Table {
    char   col[4][256];
    size_t len[4];
    int    rows;
};
void table_clear(Table& t);
void table_add(Table& t, const char* a, const char* b, const char* c, const char* d);
// pct = column widths in percent of the overlay width
void table_show(const Table& t, const char* const head[4], const int8_t pct[4],
                const lv_font_t* head_font, const lv_font_t* body_font);

} // namespace ui
