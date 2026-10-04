#include "sysbar.h"

#include <cstdio>
#include <cstring>
#include "games/common/wplay.h"
#include "shell.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

lv_obj_t* bar = nullptr;
lv_obj_t* back_btn = nullptr;
lv_obj_t* title_l = nullptr;
lv_obj_t* note_l = nullptr;
lv_obj_t* mid = nullptr;                 // a game's clock + status
lv_obj_t* left_l = nullptr;
lv_obj_t* center_l = nullptr;
lv_obj_t* menu_btn = nullptr;            // a game's menu callback (the gear presses it)
lv_obj_t* gear_o = nullptr;
lv_obj_t* wifi_o = nullptr;
lv_obj_t* two_o = nullptr;
int       bar_h = 0;
int       icon = 0;                      // icon key size (square)

// What the bar shows: the screen's title/back, overridden while a page is up
char      screen_title[40] = "";
char      screen_note[24] = "";
void    (*screen_back)() = nullptr;
bool      page_up = false;
char      page_title[48] = "";
lv_obj_t* page_back = nullptr;
bool      game_mode = false;
int       left_wanted = 0;
lv_event_cb_t game_menu_cb = nullptr;
intptr_t  game_menu_user = 0;

int  wifi_shown = -2;                    // what the icons show now
int  two_shown = -1;
uint32_t next_ms = 0;

int margin() { return metrics().large ? 2 : 1; }
int key_h()  { return metrics().large ? 36 : 26; }   // the games' bar keys always were this tall

int text_w(const char* t, const lv_font_t* f)
{
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return sz.x;
}

// x where the middle starts / ends (between back and 2P)
int mid_x0() { return lv_obj_get_width(back_btn) + margin() + 2; }
int mid_x1() { return lv_obj_get_x(two_o) - 2; }

void show_text()
{
    if (!title_l) return;
    const bool game = game_mode && !page_up;
    lv_obj_set_hidden(mid, !game);
    lv_obj_set_hidden(title_l, game);
    lv_obj_set_hidden(note_l, game);
    const bool can_back = page_up || screen_back;
    lv_obj_set_style_text_color(lv_obj_get_child(back_btn, 0), can_back ? pal().ink : pal().key_border, 0);
    if (game) return;
    const char* t = page_up ? page_title : screen_title;
    const char* n = page_up ? "" : screen_note;
    // One line: the title as wide as it is (dots if it can't fit); the note
    // after it only if both fit
    const int room = mid_x1() - mid_x0();
    const int tw = text_w(t, bar_font());
    const int nw = *n ? text_w(n, &lv_font_montserrat_12) + 4 : 0;
    lv_label_set_text(title_l, t);
    lv_obj_set_width(title_l, tw < room ? tw + 1 : room);
    lv_obj_set_pos(title_l, mid_x0(), (bar_h - lv_font_get_line_height(bar_font())) / 2);
    lv_label_set_text(note_l, *n && tw + nw <= room ? n : "");
    lv_obj_update_layout(title_l);
    lv_obj_align_to(note_l, title_l, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -2);
}

// Wifi: a dot and three arcs fanning up. Off = all faint; on = the dot and
// as many arcs as the signal is strong, the rest faint.
void wifi_draw(lv_event_t* e)
{
    lv_obj_t* o = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int s = lv_area_get_height(&a) - 4;
    const int cx = (a.x1 + a.x2) / 2, cy = a.y2 - 3;
    const int level = wifi_shown;
    const lv_color_t on = pal().ink, off = pal().key_border;
    const int w = s >= 24 ? 3 : 2;
    for (int k = 1; k <= 3; ++k) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = uint16_t(s * (k + 0.6) / 3.8);
        d.start_angle = 225;
        d.end_angle = 315;
        d.width = w;
        d.rounded = 1;
        d.color = level >= k ? on : off;
        lv_draw_arc(layer, &d);
    }
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.radius = LV_RADIUS_CIRCLE;
    r.bg_color = level >= 0 ? on : off;
    const int dr = w;
    lv_area_t dot{cx - dr, cy - dr, cx + dr, cy + dr};
    lv_draw_rect(layer, &r, &dot);
}

// 2P: two head-and-shoulders, the back one a little up and to the right.
// Filled while a wireless game is going; outlines otherwise.
void person(lv_layer_t* layer, int cx, int top, int s, bool filled, lv_color_t ink, lv_color_t bg)
{
    const int hr = s * 18 / 100 + 1;                 // head
    const int bw = s * 34 / 100, bh = s * 30 / 100;  // shoulders
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = filled ? ink : bg;
    r.bg_opa = LV_OPA_COVER;
    r.border_color = filled ? bg : ink;
    r.border_width = filled ? 1 : (s >= 24 ? 2 : 1);
    r.radius = LV_RADIUS_CIRCLE;
    lv_area_t head{cx - hr, top, cx + hr, top + 2 * hr};
    lv_area_t body{cx - bw, top + 2 * hr + 1, cx + bw, top + 2 * hr + 1 + 2 * bh};
    lv_draw_rect(layer, &r, &body);                  // a rounded top; the bottom half is cut off
    lv_draw_rect(layer, &r, &head);
}

void two_draw(lv_event_t* e)
{
    lv_obj_t* o = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int s = lv_area_get_height(&a) - 2;
    const bool filled = two_shown > 0;
    const lv_color_t ink = filled ? pal().ink : pal().muted, bg = pal().screen;
    // (drawing is clipped to the icon: the shoulders end at its bottom - busts)
    const int cx = (a.x1 + a.x2) / 2;
    person(layer, cx + s * 22 / 100, a.y1 + 1, s, filled, ink, bg);
    person(layer, cx - s * 12 / 100, a.y1 + s * 12 / 100 + 1, s, filled, ink, bg);
}

lv_obj_t* icon_btn(int w, lv_event_cb_t cb)
{
    lv_obj_t* b = lv_obj_create(bar);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, key_h());
    lv_obj_set_style_bg_color(b, pal().key_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_clickable(b, true);
    lv_obj_set_scrollable(b, false);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}

lv_obj_t* symbol(lv_obj_t* parent, const char* sym)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, metrics().large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, pal().ink, 0);
    lv_obj_center(l);
    return l;
}

void make_menu_btn()
{
    if (menu_btn) { lv_obj_delete(menu_btn); menu_btn = nullptr; }
    if (!game_mode || !bar) return;
    // Nothing to see: the gear presses it (it carries the game's callback
    // and user data)
    menu_btn = lv_obj_create(bar);
    lv_obj_remove_style_all(menu_btn);
    lv_obj_set_size(menu_btn, 0, 0);
    lv_obj_add_event_cb(menu_btn, game_menu_cb, LV_EVENT_CLICKED, reinterpret_cast<void*>(game_menu_user));
    lv_obj_set_pos(mid, mid_x0(), 0);
    lv_obj_set_size(mid, mid_x1() - mid_x0(), bar_h);
}

} // namespace

int sysbar_height()
{
    return (metrics().large ? 36 : 26) + 2 * (metrics().large ? 2 : 1);
}

// Built once (games keep pointers to the clock and status labels); later
// calls (theme, rotation) only recolour and re-place it.
void sysbar_build()
{
    const Metrics& m = metrics();
    bar_h = sysbar_height();
    icon = key_h();
    const int mg = margin();
    if (!bar) {
        bar = lv_obj_create(lv_layer_sys());
        lv_obj_remove_style_all(bar);
        lv_obj_set_scrollable(bar, false);
        lv_obj_set_clickable(bar, false);
        back_btn = icon_btn(icon, [](lv_event_t*) { sysbar_back(); });
        symbol(back_btn, LV_SYMBOL_LEFT);
        // The gear: in a game its menu (New Game ... Settings), elsewhere Settings
        gear_o = icon_btn(icon, [](lv_event_t*) { sysbar_gear(); });
        symbol(gear_o, LV_SYMBOL_SETTINGS);
        wifi_o = icon_btn(icon, [](lv_event_t*) { wplay::open_menu(nullptr); });
        lv_obj_add_event_cb(wifi_o, wifi_draw, LV_EVENT_DRAW_MAIN, nullptr);
        two_o = icon_btn(icon, [](lv_event_t*) { wplay::resume_session(); });
        lv_obj_add_event_cb(two_o, two_draw, LV_EVENT_DRAW_MAIN, nullptr);
        title_l = lv_label_create(bar);
        lv_label_set_long_mode(title_l, LV_LABEL_LONG_MODE_DOTS);
        note_l = lv_label_create(bar);
        // A game's middle: clock on the left, status centred in what's left
        mid = lv_obj_create(bar);
        lv_obj_remove_style_all(mid);
        lv_obj_set_scrollable(mid, false);
        lv_obj_set_clickable(mid, false);
        left_l = lv_label_create(mid);
        lv_label_set_text(left_l, "");
        center_l = lv_label_create(mid);
        lv_label_set_text(center_l, "");
        lv_label_set_long_mode(center_l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_style_text_align(center_l, LV_TEXT_ALIGN_CENTER, 0);
    }
    lv_obj_set_size(bar, m.w, bar_h);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, pal().screen, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, pal().key_border, 0);
    lv_obj_t* const icons[] = {back_btn, gear_o, wifi_o, two_o};
    for (lv_obj_t* o : icons) {
        lv_obj_set_size(o, o == back_btn ? icon + 4 : icon, icon);
        lv_obj_set_style_bg_color(o, pal().key_pressed, LV_STATE_PRESSED);
        lv_obj_t* l = lv_obj_get_child(o, 0);
        if (l) {
            lv_obj_set_style_text_font(l, m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(l, pal().ink, 0);
        }
    }
    // Left: back. Right, from the edge: gear, wifi, 2P
    lv_obj_set_pos(back_btn, mg, mg);
    lv_obj_align(gear_o, LV_ALIGN_TOP_RIGHT, -mg, mg);
    lv_obj_align_to(wifi_o, gear_o, LV_ALIGN_OUT_LEFT_TOP, -2, 0);
    lv_obj_align_to(two_o, wifi_o, LV_ALIGN_OUT_LEFT_TOP, -2, 0);
    lv_obj_update_layout(bar);
    lv_obj_set_style_text_font(title_l, bar_font(), 0);
    lv_obj_set_style_text_color(title_l, pal().ink, 0);
    lv_obj_set_style_text_font(note_l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(note_l, pal().muted, 0);
    lv_obj_set_style_text_font(left_l, bar_font(), 0);
    lv_obj_set_style_text_color(left_l, pal().muted, 0);
    lv_obj_set_style_text_font(center_l, bar_font(), 0);
    lv_obj_set_height(center_l, lv_font_get_line_height(bar_font()));   // one line, dots if long
    lv_obj_set_style_text_color(center_l, pal().ink, 0);
    lv_obj_set_pos(mid, mid_x0(), 0);
    lv_obj_set_size(mid, mid_x1() - mid_x0(), bar_h);
    make_menu_btn();
    wifi_shown = -2;
    two_shown = -1;
    show_text();
    sysbar_tick(0);
}

void sysbar_screen(const char* title, void (*back)(), const char* note)
{
    snprintf(screen_title, sizeof screen_title, "%s", title ? title : "");
    snprintf(screen_note, sizeof screen_note, "%s", note ? note : "");
    screen_back = back;
    if (game_mode) {
        game_mode = false;
        make_menu_btn();
        if (left_l) { lv_label_set_text(left_l, ""); lv_label_set_text(center_l, ""); }
    }
    show_text();
}

void sysbar_game(lv_event_cb_t menu_cb, intptr_t user, int show_left)
{
    game_mode = true;
    left_wanted = show_left;
    game_menu_cb = menu_cb;
    game_menu_user = user;
    lv_label_set_text(left_l, "");
    lv_label_set_text(center_l, "");
    lv_obj_set_hidden(left_l, !show_left);
    make_menu_btn();
    show_text();
}

lv_obj_t* sysbar_left()   { return left_l; }
lv_obj_t* sysbar_center() { return center_l; }

void sysbar_status(const char* text, const char* short_text)
{
    if (!center_l) return;
    const lv_font_t* f = bar_font();
    const int room = mid_x1() - mid_x0() - 4;
    const char* clock = lv_label_get_text(left_l);
    const int cw = left_wanted && clock && *clock ? text_w(clock, f) + 4 : 0;
    const int w = text_w(text, f);
    const int sw = short_text ? text_w(short_text, f) : w;
    bool show_clock = true;
    const char* use = text;
    if (left_wanted == 2) {
        if (short_text && w + cw > room) use = short_text;
    } else if (w + cw > room) {
        if (short_text && sw + cw <= room) use = short_text;
        else if (w <= room) show_clock = false;
        else { use = short_text ? short_text : text; show_clock = false; }
    }
    const bool left_shown = left_wanted == 2 || (left_wanted && show_clock);
    lv_obj_set_hidden(left_l, !left_shown);
    lv_label_set_text(center_l, use);
    // Placed by hand: the left label at the start, the status centred in
    // the rest (one line, dots if it still doesn't fit)
    const int y = (bar_h - lv_font_get_line_height(f)) / 2;
    lv_obj_set_pos(left_l, 4, y);
    const int x = left_shown ? 4 + cw : 0;
    lv_obj_set_pos(center_l, x, y);
    lv_obj_set_width(center_l, room + 4 - x);
}

void sysbar_overlay(const char* title, lv_obj_t* back_key)
{
    page_up = true;
    snprintf(page_title, sizeof page_title, "%s", title ? title : "");
    page_back = back_key;
    show_text();
}

void sysbar_page_back(lv_obj_t* back_key) { page_back = back_key; }

void sysbar_overlay_closed()
{
    page_up = false;
    page_back = nullptr;
    show_text();
}

void sysbar_back()
{
    if (page_up && overlay_open()) {
        if (page_back) lv_obj_send_event(page_back, LV_EVENT_CLICKED, nullptr);
        else close_overlays();
        return;
    }
    if (screen_back) screen_back();
}

void sysbar_gear()
{
    if (game_mode && !page_up && menu_btn) lv_obj_send_event(menu_btn, LV_EVENT_CLICKED, nullptr);
    else settings_open(nullptr);
}

void sysbar_tick(uint32_t now)
{
    if (!bar || int32_t(now - next_ms) < 0) return;
    next_ms = now + 300;
    const int w = wplay::wifi_level();
    if (w != wifi_shown) { wifi_shown = w; lv_obj_invalidate(wifi_o); }
    const int t = wplay::two_player_state();
    if (t != two_shown) { two_shown = t; lv_obj_invalidate(two_o); }
}

} // namespace ui
