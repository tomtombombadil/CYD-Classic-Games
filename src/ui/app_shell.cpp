// The app shell: game picker (boot screen), switching between games, and
// the settings every game shares.
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
int        page = 0;                     // picker page

// ---- Picker layout ------------------------------------------------------------
// Top to bottom: title bar with the ☰ menu, a "Continue <last game>" card,
// then the games as tiles grouped by category (2 per row, so titles like
// "Minesweeper" fit on a 240-px screen without breaking). Whatever doesn't
// fit goes on further pages, switched with big < > buttons at the bottom:
// no scrolling, every target is a tap.
constexpr int kCols = 2;                 // tiles per row

struct PickerGeom {
    int pad, gap, top_h, cont_h, head_h, tile_w, tile_h, icon, pager_h;
};

PickerGeom geom()
{
    const Metrics& m = metrics();
    PickerGeom g{};
    g.pad     = m.large ? 12 : 8;
    g.gap     = m.large ? 10 : 6;
    g.top_h   = m.large ? 44 : 32;
    g.cont_h  = m.large ? 100 : 70;
    g.head_h  = lv_font_get_line_height(bar_font()) + (m.large ? 6 : 4);
    g.tile_w  = (m.w - 2 * g.pad - (kCols - 1) * g.gap) / kCols;
    g.icon    = m.large ? 60 : 44;
    // icon + up to two lines of title
    g.tile_h  = g.icon + 2 * lv_font_get_line_height(menu_font()) + (m.large ? 18 : 12);
    g.pager_h = menu_btn_h();
    return g;
}

// One picker line: a category heading or a row of up to kCols tiles.
struct Line {
    bool heading;
    int  cat;                            // heading: category
    int  first, n;                       // tiles: indexes into `order`
};
constexpr int kMaxGames = 64;
constexpr int kMaxLines = kMaxGames + 2 * games::kCategories;
int  order[kMaxGames];                   // registry indexes, grouped by category
Line lines[kMaxLines];
int  line_n = 0;
int  page_start[kMaxLines + 1];          // first line of each page
int  page_n = 0;

int line_h(const Line& l, const PickerGeom& g) { return l.heading ? g.head_h : g.tile_h; }

void build_lines()
{
    int n = 0;
    line_n = 0;
    for (int c = 0; c < games::kCategories; ++c) {
        const int first = n;
        for (int i = 0; i < games::count() && n < kMaxGames; ++i)
            if (static_cast<int>(games::get(i).category) == c) order[n++] = i;
        if (n == first) continue;
        lines[line_n++] = Line{true, c, 0, 0};
        for (int k = first; k < n; k += kCols)
            lines[line_n++] = Line{false, c, k, (n - k) < kCols ? (n - k) : kCols};
    }
}

// Split the lines into pages that fit between `top` and the bottom of the
// screen (minus the pager row when there is more than one page). A heading
// never ends a page; a category that runs onto the next page gets its
// heading again there.
void paginate(int top, const PickerGeom& g)
{
    const int bottom_all = metrics().h - g.pad;
    for (int with_pager = 0; with_pager < 2; ++with_pager) {
        const int bottom = with_pager ? bottom_all - g.pager_h - g.gap : bottom_all;
        page_n = 0;
        int y = top, k = 0;
        page_start[page_n++] = 0;
        while (k < line_n) {
            int need = line_h(lines[k], g);
            if (lines[k].heading && k + 1 < line_n) need += g.gap / 2 + line_h(lines[k + 1], g);
            if (y + need > bottom && y > top) {
                page_start[page_n++] = k;
                // A page that starts mid-category repeats its heading
                y = lines[k].heading ? top : top + g.head_h + g.gap / 2;
                continue;
            }
            y += line_h(lines[k], g) + (lines[k].heading ? g.gap / 2 : g.gap);
            ++k;
        }
        page_start[page_n] = line_n;
        if (page_n == 1) return;             // fits without a pager
    }
}

int page_of_game(int idx)
{
    for (int p = 0; p < page_n; ++p)
        for (int k = page_start[p]; k < page_start[p + 1]; ++k)
            if (!lines[k].heading)
                for (int t = 0; t < lines[k].n; ++t)
                    if (order[lines[k].first + t] == idx) return p;
    return 0;
}

void picker_build();

void tile_cb(lv_event_t* e)
{
    app_open_game(static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));
}

void rebuild_async(void*) { picker_build(); }

void pager_cb(lv_event_t* e)
{
    const int step = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const int next = page + step;
    if (next < 0 || next >= page_n) return;
    page = next;
    // Rebuilding deletes this button, so do it after the event is done
    lv_async_call(rebuild_async, nullptr);
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

void picker_build()
{
    metrics_update();
    const Metrics& m = metrics();
    const Palette& P = pal();
    const PickerGeom g = geom();
    lv_obj_t* scr = lv_screen_active();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, P.screen, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    // Title bar
    int y = m.large ? 2 : 1;
    lv_obj_t* t = label(scr, "Classic Games", title_font(), P.ink);
    lv_obj_set_pos(t, g.pad, y + (g.top_h - lv_font_get_line_height(title_font())) / 2);
    const int hb_w = g.top_h * 3 / 2;
    lv_obj_t* hb = make_hamburger(scr, hb_w, g.top_h, menu_cb, 0);
    lv_obj_set_pos(hb, m.w - (m.large ? 2 : 1) - hb_w, y);
    y += g.top_h + g.gap;

    y = make_continue(scr, y, g);

    build_lines();
    paginate(y, g);
    if (page >= page_n) page = page_n - 1;
    if (page < 0) page = 0;

    auto heading = [&](int cat) {
        lv_obj_t* h = label(scr, games::category_name(static_cast<games::Category>(cat)),
                            bar_font(), P.muted);
        lv_obj_set_pos(h, g.pad + 2, y + g.head_h - lv_font_get_line_height(bar_font()));
        y += g.head_h + g.gap / 2;
    };
    for (int k = page_start[page]; k < page_start[page + 1]; ++k) {
        const Line& l = lines[k];
        if (l.heading) {
            heading(l.cat);
        } else {
            if (k == page_start[page]) heading(l.cat);     // continued category
            for (int i = 0; i < l.n; ++i)
                make_tile(scr, order[l.first + i], g.pad + i * (g.tile_w + g.gap), y, g);
            y += g.tile_h + g.gap;
        }
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
        lv_obj_t* pl = label(scr, pg, menu_font(), P.muted);
        lv_obj_align(pl, LV_ALIGN_BOTTOM_MID, 0,
                     -(g.pad + (g.pager_h - lv_font_get_line_height(menu_font())) / 2));
    }
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
UiSettings&  settings() { return S; }

void save_settings()
{
    if (H.save_settings) H.save_settings(S);
}

void app_begin(const Shell& shell, const UiSettings& settings)
{
    H = shell;
    S = settings;
    set_theme(S.theme);
    metrics_update();
    styles_apply();
    if (H.set_brightness) H.set_brightness(S.brightness);
    current = -1;
    build_lines();
    paginate(0, geom());
    page = page_of_game(games::find(S.last_game));
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
    if (gi.ops && gi.ops->open) gi.ops->open();
}

void app_go_home_now()
{
    const int was = current;
    close_current();
    if (was >= 0) {
        build_lines();
        paginate(0, geom());
        page = page_of_game(was);
    }
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
    overlay_button(overlay(), "Display & touch", menu_cb, kSettings);
    overlay_button(overlay(), "Back", menu_cb, kClose, true);
    char info[128];
    snprintf(info, sizeof info, "%s, firmware %s", H.board_name ? H.board_name : "",
             H.firmware_version ? H.firmware_version : "");
    overlay_text(info, true);
}

} // namespace ui
