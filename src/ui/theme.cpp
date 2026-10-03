#include "theme.h"

namespace ui {

namespace {

Theme   current = Theme::Light;
Palette p_light, p_dark;
bool    built = false;

void build()
{
    if (built) return;
    built = true;

    Palette& l = p_light;
    l.screen       = lv_color_hex(0xEEF0F2);
    l.cell         = lv_color_hex(0xFFFFFF);
    l.line_thin    = lv_color_hex(0xA9B0B9);
    l.line_thick   = lv_color_hex(0x1E232B);
    l.given        = lv_color_hex(0x1E232B);
    l.entry        = lv_color_hex(0x1A52AA);
    l.hinted       = lv_color_hex(0x13804A);
    l.note         = lv_color_hex(0x4E5663);
    l.note_match   = lv_color_hex(0x000000);
    l.conflict     = lv_color_hex(0xC01818);
    l.conflict_bg  = lv_color_hex(0xF2B8B8);
    l.peer         = lv_color_hex(0xC9D5E3);   // was #FBF2D2: too pale off-angle
    l.same         = lv_color_hex(0xF4CC52);
    l.selected     = lv_color_hex(0xE39A1E);
    l.key          = lv_color_hex(0xFFFFFF);
    l.key_border   = lv_color_hex(0xA9B0B9);
    l.key_pressed  = lv_color_hex(0xD5DAE0);
    l.key_on       = lv_color_hex(0xE3B53A);
    l.key_on_text  = lv_color_hex(0x1E1B12);
    l.key_dim_text = lv_color_hex(0xB9BFC7);
    l.ink          = lv_color_hex(0x1E232B);
    l.muted        = lv_color_hex(0x4E5663);

    Palette& d = p_dark;
    d.screen       = lv_color_hex(0x101318);
    d.cell         = lv_color_hex(0x1C2128);
    d.line_thin    = lv_color_hex(0x3C4450);
    d.line_thick   = lv_color_hex(0xA7B0BC);
    d.given        = lv_color_hex(0xEEF0F3);
    d.entry        = lv_color_hex(0x8CC0FF);
    d.hinted       = lv_color_hex(0x6FD69E);
    d.note         = lv_color_hex(0xA9B1BC);
    d.note_match   = lv_color_hex(0xFFFFFF);
    d.conflict     = lv_color_hex(0xFF8A8A);
    d.conflict_bg  = lv_color_hex(0x6A2424);
    d.peer         = lv_color_hex(0x33414F);
    d.same         = lv_color_hex(0x6B5414);
    d.selected     = lv_color_hex(0x9A6608);
    d.key          = lv_color_hex(0x232932);
    d.key_border   = lv_color_hex(0x4A5360);
    d.key_pressed  = lv_color_hex(0x343C48);
    d.key_on       = lv_color_hex(0xE3B53A);
    d.key_on_text  = lv_color_hex(0x16130A);
    d.key_dim_text = lv_color_hex(0x4E5663);
    d.ink          = lv_color_hex(0xEEF0F3);
    d.muted        = lv_color_hex(0xA9B1BC);
}

} // namespace

void set_theme(Theme t) { build(); current = t; }
Theme theme() { return current; }
const Palette& pal() { build(); return current == Theme::Dark ? p_dark : p_light; }
const char* theme_name(Theme t) { return t == Theme::Dark ? "Dark" : "Light"; }

} // namespace ui
