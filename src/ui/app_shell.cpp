// The app shell: game picker (boot screen), switching between games, and
// the settings every game shares.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <lvgl.h>
#include "games/registry.h"
#include "shell.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

Shell      H{};
UiSettings S{};
int        current = -1;                 // open game (registry index), -1 = picker
int        page = 0;                     // page of the open category
int        cat_open = -1;                // category page shown, -1 = category list

// ---- Picker layout ------------------------------------------------------------
// First screen: title bar with the ☰ menu, a "Continue <last game>" card,
// then the categories as a text list (Tom's choice: Puzzle, Strategy, Word,
// Dice Games), each with its game count. A category opens its own
// page: back key, the category name, and its games as tiles, 2 per row (so
// titles like "Minesweeper" fit on a 240-px screen without breaking).
// Tiles that don't fit go on further pages, switched with big < > keys at
// the bottom: no scrolling, every target is a tap.
constexpr int kCols = 2;                 // tiles per row

struct PickerGeom {
    int pad, gap, top_h, cont_h, tile_w, tile_h, icon, pager_h;
};

PickerGeom geom()
{
    const Metrics& m = metrics();
    PickerGeom g{};
    g.pad     = m.large ? 12 : 8;
    g.gap     = m.large ? 10 : 6;
    g.top_h   = m.large ? 44 : 32;
    g.cont_h  = m.large ? 100 : 70;
    g.tile_w  = (m.w - 2 * g.pad - (kCols - 1) * g.gap) / kCols;
    g.icon    = m.large ? 60 : 44;
    // icon + up to two lines of title
    g.tile_h  = g.icon + 2 * lv_font_get_line_height(menu_font()) + (m.large ? 18 : 12);
    g.pager_h = menu_btn_h();
    return g;
}

constexpr int kMaxGames = 64;
int order[kMaxGames];                    // registry indexes in the open category
int order_n = 0;
int rows_per_page = 1;
int page_n = 1;

int games_in(int cat)
{
    int n = 0;
    for (int i = 0; i < games::count(); ++i)
        if (static_cast<int>(games::get(i).category) == cat) ++n;
    return n;
}

void collect(int cat)
{
    order_n = 0;
    for (int i = 0; i < games::count() && order_n < kMaxGames; ++i)
        if (static_cast<int>(games::get(i).category) == cat) order[order_n++] = i;
}

// Rows of tiles that fit below `top`; with more rows than that, the pager
// row takes room at the bottom.
void paginate(int top, const PickerGeom& g)
{
    const int rows = (order_n + kCols - 1) / kCols;
    auto fit = [&](int bottom) { const int r = (bottom - top + g.gap) / (g.tile_h + g.gap); return r < 1 ? 1 : r; };
    const int bottom = metrics().h - g.pad;
    rows_per_page = fit(bottom);
    if (rows > rows_per_page) rows_per_page = fit(bottom - g.pager_h - g.gap);
    page_n = rows ? (rows + rows_per_page - 1) / rows_per_page : 1;
}

void picker_build();

void tile_cb(lv_event_t* e)
{
    app_open_game(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

void rebuild_async(void*) { picker_build(); }

// Rebuilding deletes the key that was tapped, so it happens after the event
void rebuild_later() { lv_async_call(rebuild_async, nullptr); }

void pager_cb(lv_event_t* e)
{
    const int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const int next = page + step;
    if (next < 0 || next >= page_n) return;
    page = next;
    rebuild_later();
}

void category_cb(lv_event_t* e)
{
    cat_open = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    page = 0;
    rebuild_later();
}

void back_cb(lv_event_t*)
{
    cat_open = -1;
    rebuild_later();
}

void menu_cb(lv_event_t* e);

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* f, lv_color_t c)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}

// The tile: icon on top, title (up to two lines) under it.
void make_tile(lv_obj_t* scr, int idx, int x, int y, const PickerGeom& g)
{
    const games::GameInfo& gi = games::get(idx);
    lv_obj_t* t = make_key(scr, g.tile_w, g.tile_h, tile_cb, idx);
    lv_obj_set_pos(t, x, y);
    const int top = metrics().large ? 8 : 5;
    lv_obj_t* box = lv_obj_create(t);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, g.icon, g.icon);
    lv_obj_align(box, LV_ALIGN_TOP_MID, 0, top);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    if (gi.ops && gi.ops->icon) gi.ops->icon(box, g.icon);
    lv_obj_t* l = label(t, gi.title, menu_font(), pal().ink);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, g.tile_w - 6);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, top + g.icon + (metrics().large ? 6 : 3));
}

// "Continue <title>" with the game's icon and its one-line state.
int make_continue(lv_obj_t* scr, int y, const PickerGeom& g)
{
    const int idx = games::find(S.last_game);
    if (idx < 0) return y;
    const games::GameInfo& gi = games::get(idx);
    const Metrics& m = metrics();
    lv_obj_t* c = make_key(scr, m.w - 2 * g.pad, g.cont_h, tile_cb, idx);
    lv_obj_set_pos(c, g.pad, y);
    lv_obj_add_state(c, LV_STATE_CHECKED);       // primary color

    const int icon = g.cont_h - (m.large ? 20 : 14);
    const int ix = m.large ? 12 : 8;
    lv_obj_t* box = lv_obj_create(c);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, icon, icon);
    lv_obj_align(box, LV_ALIGN_LEFT_MID, ix, 0);
    lv_obj_set_clickable(box, false);
    lv_obj_set_scrollable(box, false);
    if (gi.ops && gi.ops->icon) gi.ops->icon(box, icon);

    // Three lines: "Continue", the title, and the game's state
    char state[48];
    if (!(gi.ops && gi.ops->summary && gi.ops->summary(state, sizeof state)))
        snprintf(state, sizeof state, "Start a game");
    const lv_font_t* fs = m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const lv_font_t* fb = m.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    const char* text[3] = {"Continue", gi.title, state};
    const lv_font_t* font[3] = {fs, fb, fs};
    const int tx = ix + icon + (m.large ? 14 : 10);
    const int tw = m.w - 2 * g.pad - tx - 6;
    int th = 0;
    for (const lv_font_t* f : font) th += lv_font_get_line_height(f);
    int ty = (g.cont_h - th) / 2;
    for (int k = 0; k < 3; ++k) {
        const int lh = lv_font_get_line_height(font[k]);
        lv_obj_t* l = label(c, text[k], font[k], pal().key_on_text);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_set_size(l, tw, lh);
        lv_obj_set_pos(l, tx, ty);
        ty += lh;
    }
    return y + g.cont_h + g.gap + (m.large ? 4 : 2);
}

void screen_begin()
{
    metrics_update();
    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, pal().screen, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
}

// Title bar: optional back key, title, ☰. Returns the y below it.
int title_bar(const char* title, bool back, const PickerGeom& g)
{
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int y = m.large ? 2 : 1;
    int tx = g.pad;
    if (back) {
        const int bw = g.top_h * 3 / 2;
        lv_obj_t* b = make_key(scr, bw, g.top_h, back_cb, 0);
        lv_obj_set_pos(b, m.large ? 2 : 1, y);
        key_label(b, LV_SYMBOL_LEFT, menu_font());
        tx = bw + (m.large ? 10 : 6);
    }
    lv_obj_t* t = label(scr, title, title_font(), pal().ink);
    const int ty = y + (g.top_h - lv_font_get_line_height(title_font())) / 2;
    lv_obj_set_pos(t, tx, ty);
    if (!back && H.firmware_version) {
        // "Classic Games v1.2.0": the version in small type after the title,
        // on the same baseline, in the largest size that clears the ☰ key
        const int vx = tx + text_width(title, title_font()) + (m.large ? 8 : 5);
        const int room = m.w - (m.large ? 2 : 1) - g.top_h * 3 / 2 - 4 - vx;
        const lv_font_t* const sizes[3] = {menu_font(), &lv_font_montserrat_14, &lv_font_montserrat_12};
        const lv_font_t* vf = sizes[2];
        for (const lv_font_t* f : sizes)
            if (text_width(H.firmware_version, f) <= room) { vf = f; break; }
        lv_obj_t* v = label(scr, H.firmware_version, vf, pal().muted);
        lv_obj_set_pos(v, vx,
                       ty + (lv_font_get_line_height(title_font()) - title_font()->base_line)
                          - (lv_font_get_line_height(vf) - vf->base_line));
    }
    const int hb_w = g.top_h * 3 / 2;
    lv_obj_t* hb = make_hamburger(scr, hb_w, g.top_h, menu_cb, 0);
    lv_obj_set_pos(hb, m.w - (m.large ? 2 : 1) - hb_w, y);
    return y + g.top_h + g.gap;
}

void build_category_list()
{
    const Metrics& m = metrics();
    const PickerGeom g = geom();
    lv_obj_t* scr = lv_screen_active();
    int y = title_bar("Classic Games", false, g);
    y = make_continue(scr, y, g);

    const int n = games::kCategories;
    const int gap = m.large ? 8 : 5;
    int key_h = (m.h - g.pad - y - (n - 1) * gap) / n;
    const int cap = m.large ? 58 : 40;
    if (key_h > cap) key_h = cap;
    const lv_font_t* f = m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_20;
    const lv_font_t* fc = m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    for (int c = 0; c < n; ++c) {
        const int count = games_in(c);
        lv_obj_t* k = make_key(scr, m.w - 2 * g.pad, key_h, count ? category_cb : nullptr, c);
        lv_obj_set_pos(k, g.pad, y);
        lv_obj_t* l = label(k, games::category_title(static_cast<games::Category>(c)), f, pal().ink);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, m.large ? 14 : 10, 0);
        char cnt[16];
        if (count) snprintf(cnt, sizeof cnt, "%d " LV_SYMBOL_RIGHT, count);
        else       snprintf(cnt, sizeof cnt, "soon");
        lv_obj_t* r = label(k, cnt, fc, pal().muted);
        lv_obj_align(r, LV_ALIGN_RIGHT_MID, m.large ? -14 : -10, 0);
        if (!count) set_dim(k, true);
        y += key_h + gap;
    }
}

void build_category_page()
{
    const Metrics& m = metrics();
    const PickerGeom g = geom();
    lv_obj_t* scr = lv_screen_active();
    const int top = title_bar(games::category_short(static_cast<games::Category>(cat_open)), true, g);
    collect(cat_open);
    paginate(top, g);
    if (page >= page_n) page = page_n - 1;
    if (page < 0) page = 0;

    int y = top;
    const int first = page * rows_per_page * kCols;
    const int last = first + rows_per_page * kCols < order_n ? first + rows_per_page * kCols : order_n;
    for (int k = first; k < last; k += kCols) {
        for (int i = 0; i < kCols && k + i < last; ++i)
            make_tile(scr, order[k + i], g.pad + i * (g.tile_w + g.gap), y, g);
        y += g.tile_h + g.gap;
    }

    if (page_n > 1) {
        const int py = m.h - g.pad - g.pager_h;
        const int bw = (m.w - 2 * g.pad) / 3;
        lv_obj_t* prev = make_key(scr, bw, g.pager_h, pager_cb, -1);
        lv_obj_set_pos(prev, g.pad, py);
        key_label(prev, LV_SYMBOL_LEFT, menu_font());
        set_dim(prev, page == 0);
        lv_obj_t* next = make_key(scr, bw, g.pager_h, pager_cb, 1);
        lv_obj_set_pos(next, m.w - g.pad - bw, py);
        key_label(next, LV_SYMBOL_RIGHT, menu_font());
        set_dim(next, page == page_n - 1);
        char pg[16];
        snprintf(pg, sizeof pg, "%d / %d", page + 1, page_n);
        lv_obj_t* pl = label(scr, pg, menu_font(), pal().muted);
        lv_obj_align(pl, LV_ALIGN_BOTTOM_MID, 0,
                     -(g.pad + (g.pager_h - lv_font_get_line_height(menu_font())) / 2));
    }
}

void picker_build()
{
    screen_begin();
    if (cat_open >= 0 && games_in(cat_open) > 0) build_category_page();
    else { cat_open = -1; build_category_list(); }
}

// ---- Picker menu ------------------------------------------------------------------
enum MenuAction : intptr_t { kOpenMenu, kSettings, kClose };

void back_to_picker_menu() { picker_open_menu(); }

void menu_cb(lv_event_t* e)
{
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case kOpenMenu: picker_open_menu(); break;
        case kSettings: settings_open(back_to_picker_menu); break;
        case kClose:    close_overlays(); break;
    }
}

// ---- Switching ------------------------------------------------------------------------
void close_current()
{
    close_overlays();
    if (current >= 0) {
        const games::GameOps* ops = games::get(current).ops;
        log_step("close %s", games::get(current).id);
        if (ops && ops->close) ops->close();
        current = -1;
    }
    lv_obj_clean(lv_screen_active());
}

void open_async(void* p) { app_open_game_now(static_cast<int>(reinterpret_cast<intptr_t>(p))); }
void home_async(void*)   { app_go_home_now(); }

} // namespace

// ---- Public -------------------------------------------------------------------------
const Shell& shell()    { return H; }

namespace {
void log_v(bool to_file, const char* fmt, va_list ap)
{
    if (!H.log) return;
    char text[96];
    vsnprintf(text, sizeof text, fmt, ap);
    H.log(text, to_file);
}
} // namespace

void log_event(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(true, fmt, ap);
    va_end(ap);
}

void log_step(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_v(false, fmt, ap);
    va_end(ap);
}
UiSettings&  settings() { return S; }

void save_settings()
{
    if (H.save_settings) H.save_settings(S);
}

void app_begin(const Shell& shell, const UiSettings& settings, const CustomThemes& themes)
{
    H = shell;
    S = settings;
    set_custom_themes(themes);
    set_theme(S.theme);
    metrics_update();
    styles_apply();
    if (H.set_brightness) H.set_brightness(S.brightness);
    current = -1;
    cat_open = -1;
    page = 0;
    picker_build();
}

void app_tick(uint32_t now_ms)
{
    if (current < 0) return;
    const games::GameOps* ops = games::get(current).ops;
    if (ops && ops->tick) ops->tick(now_ms);
}

void app_open_game(int index)
{
    lv_async_call(open_async, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
}

void app_go_home() { lv_async_call(home_async, nullptr); }

int app_current_game() { return current; }

void app_open_game_now(int index)
{
    if (index < 0 || index >= games::count()) return;
    close_current();
    const games::GameInfo& gi = games::get(index);
    if (strcmp(S.last_game, gi.id) != 0) {
        snprintf(S.last_game, sizeof S.last_game, "%s", gi.id);
        save_settings();
    }
    current = index;
    metrics_update();
    log_event("Open %s", gi.id);
    if (gi.ops && gi.ops->open) gi.ops->open();
}

void app_go_home_now()
{
    close_current();
    cat_open = -1;                       // "Exit Game" = back to the category list
    picker_build();
}

void app_save_current()
{
    if (current < 0) return;
    const games::GameOps* ops = games::get(current).ops;
    if (ops && ops->save) ops->save();
}

void app_theme_changed()
{
    if (current >= 0) {
        const games::GameOps* ops = games::get(current).ops;
        if (ops && ops->restyle) ops->restyle();
    } else {
        picker_build();
    }
}

void picker_open_menu()
{
    overlay_begin("CYD Classic Games");
    overlay_button(overlay(), "Settings", menu_cb, kSettings);
    overlay_button(overlay(), "Back", menu_cb, kClose, true);
    char info[128];
    snprintf(info, sizeof info, "%s, firmware %s", H.board_name ? H.board_name : "",
             H.firmware_version ? H.firmware_version : "");
    overlay_text(info, true);
}

void picker_open_category(int category)
{
    close_overlays();
    cat_open = category;
    page = 0;
    picker_build();
}

void picker_next_page()
{
    if (page + 1 < page_n) { ++page; picker_build(); }
}

} // namespace ui
