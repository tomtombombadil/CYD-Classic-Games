// Theme screens: pick Light, Dark or a Custom theme; edit a custom theme's
// 10 color roles; pick a role's color from the 48-color palette. Every
// change shows at once (the screens are drawn in the theme being edited).
#include <cstdio>
#include <lvgl.h>
#include "shell.h"
#include "sound.h"
#include "theme.h"
#include "widgets.h"

namespace ui {

namespace {

int edit_slot = 0;                       // custom slot in the editor / palette
Role edit_role = Role::Background;

int gap() { return metrics().large ? 6 : 4; }

lv_obj_t* row(int h)
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, gap(), 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

lv_obj_t* grow_key(lv_obj_t* parent, const char* text, lv_event_cb_t cb, intptr_t id, bool on)
{
    lv_obj_t* b = make_key(parent, 10, 10, cb, id);
    lv_obj_set_height(b, lv_pct(100));
    lv_obj_set_flex_grow(b, 1);
    key_label(b, text, menu_font());
    set_checked(b, on);
    return b;
}

// A key filled with a color, its text in black or white to stay readable
lv_obj_t* color_key(lv_obj_t* parent, int w, int h, uint32_t rgb, lv_event_cb_t cb, intptr_t id)
{
    lv_obj_t* b = make_key(parent, w, h, cb, id);
    lv_obj_set_style_bg_color(b, lv_color_hex(rgb), 0);
    lv_obj_set_style_text_color(b, contrast_text(lv_color_hex(rgb)), 0);
    lv_obj_set_style_border_color(b, pal().ink, 0);
    return b;
}

void save_themes()
{
    if (shell().save_themes) shell().save_themes(custom_themes());
}

// The custom theme changed: rebuild everything if it is the one on screen
void custom_changed()
{
    save_themes();
    if (theme() == custom_theme(edit_slot)) app_set_theme(theme());
}

// ---- Theme list ----------------------------------------------------------------
enum ListAction : intptr_t { kEdit = 100, kBack = 101 };

void list_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (id == kBack) { settings_reopen(); return; }
    if (id == kEdit) { theme_open_editor(custom_slot(theme())); return; }
    app_set_theme(static_cast<Theme>(id));
    theme_open();
}

// ---- Editor --------------------------------------------------------------------------
enum EditAction : intptr_t { kResetLight = 100, kResetDark = 101, kDone = 102 };

void editor_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (id == kDone) { theme_open(); return; }
    if (id == kResetLight || id == kResetDark) {
        CustomTheme t;
        t.base = id == kResetDark ? 1 : 0;
        set_custom_theme(edit_slot, t);
        custom_changed();
        theme_open_editor(edit_slot);
        return;
    }
    theme_open_palette(edit_slot, static_cast<Role>(id));
}

// ---- Palette ------------------------------------------------------------------------
enum PaletteAction : intptr_t { kDefault = 100, kPaletteBack = 101 };

void palette_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    if (id == kPaletteBack) { theme_open_editor(edit_slot); return; }
    CustomTheme t = custom_themes().slot[edit_slot];
    const int r = static_cast<int>(edit_role);
    if (id == kDefault) {
        t.set &= ~(1u << r);
    } else {
        t.set |= 1u << r;
        t.color[r] = kPalette[id];
    }
    set_custom_theme(edit_slot, t);
    custom_changed();
    theme_open_editor(edit_slot);
}

} // namespace

void app_set_theme(Theme t)
{
    settings().theme = t;
    set_theme(t);
    styles_apply();
    app_theme_changed();
    save_settings();
}

void theme_open()
{
    const Theme cur = theme();
    overlay_begin("Theme");
    const int h = menu_btn_h();
    lv_obj_t* r1 = row(h);
    grow_key(r1, "Light", list_cb, static_cast<intptr_t>(Theme::Light), cur == Theme::Light);
    grow_key(r1, "Dark", list_cb, static_cast<intptr_t>(Theme::Dark), cur == Theme::Dark);
    lv_obj_t* r2 = row(h);
    for (int k = 0; k < kCustomThemes; ++k) {
        const Theme t = custom_theme(k);
        grow_key(r2, theme_name(t), list_cb, static_cast<intptr_t>(t), cur == t);
    }
    if (is_custom(cur)) {
        char txt[32];
        snprintf(txt, sizeof txt, "Edit %s", theme_name(cur));
        overlay_button(overlay(), txt, list_cb, kEdit);
    } else {
        overlay_text("Pick Custom 1, 2 or 3, then Edit to choose your own colors.", true);
    }
    overlay_bottom_button("Back", list_cb, kBack);
}

void theme_open_editor(int slot)
{
    edit_slot = slot;
    const Theme t = custom_theme(slot);
    overlay_begin(theme_name(t));
    const int h = metrics().large ? 44 : 30;
    for (int k = 0; k < kRoles; k += 2) {
        lv_obj_t* r = row(h);
        for (int j = k; j < k + 2; ++j) {
            const Role role = static_cast<Role>(j);
            lv_obj_t* b = color_key(r, 10, h, role_color(t, role), editor_cb, j);
            lv_obj_set_flex_grow(b, 1);
            key_label(b, role_name(role), menu_font());
        }
    }
    lv_obj_t* r = row(menu_btn_h());
    grow_key(r, "Reset: Light", editor_cb, kResetLight, false);
    grow_key(r, "Reset: Dark", editor_cb, kResetDark, false);
    overlay_bottom_button("Done", editor_cb, kDone);
}

void theme_open_palette(int slot, Role role)
{
    edit_slot = slot;
    edit_role = role;
    const Theme t = custom_theme(slot);
    const uint32_t now = role_color(t, role);
    overlay_begin(role_name(role));
    const bool large = metrics().large;
    const int g = gap();
    const int inner_w = metrics().w - 2 * (large ? 16 : 10);
    const int sw = (inner_w - 7 * g) / 8;
    const int sh = large ? 44 : 30;
    lv_obj_t* grid = lv_obj_create(overlay());
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(grid, g, 0);
    lv_obj_set_style_pad_row(grid, g, 0);
    lv_obj_set_scrollable(grid, false);
    for (int k = 0; k < kPaletteColors; ++k) {
        lv_obj_t* b = color_key(grid, sw, sh, kPalette[k], palette_cb, k);
        if (kPalette[k] == now) {               // the current pick: thick frame
            lv_obj_set_style_border_width(b, large ? 4 : 3, 0);
            lv_obj_set_style_border_color(b, contrast_text(lv_color_hex(now)), 0);
            lv_obj_set_style_outline_width(b, 2, 0);
            lv_obj_set_style_outline_color(b, pal().ink, 0);
        }
    }
    lv_obj_t* r = row(menu_btn_h());
    lv_obj_set_ignore_layout(r, true);
    lv_obj_align(r, LV_ALIGN_BOTTOM_MID, 0, 0);
    grow_key(r, "Default", palette_cb, kDefault, false);
    grow_key(r, "Back", palette_cb, kPaletteBack, true);
}

} // namespace ui
