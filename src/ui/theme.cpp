#include "theme.h"

namespace ui {

namespace {

Theme        current = Theme::Light;
Palette      p_light, p_dark, p_custom;
bool         built = false;
CustomThemes customs;

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
    l.piece_a      = lv_color_hex(0xD32F2F);   // red disc, O
    l.piece_b      = lv_color_hex(0xF2B807);   // yellow disc
    l.frame        = lv_color_hex(0x1F57B8);   // FourConnect board
    l.lit          = lv_color_hex(0xF4CC52);   // a light that is on
    l.win          = lv_color_hex(0x13804A);   // winning line / solved

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
    d.piece_a      = lv_color_hex(0xF0564F);
    d.piece_b      = lv_color_hex(0xF5C842);
    d.frame        = lv_color_hex(0x2D5BA8);
    d.lit          = lv_color_hex(0xE8B730);
    d.win          = lv_color_hex(0x6FD69E);
}

uint32_t hex_of(lv_color_t c) { return (uint32_t(c.red) << 16) | (uint32_t(c.green) << 8) | c.blue; }

const Palette& base_of(const CustomTheme& ct) { return ct.base == 1 ? p_dark : p_light; }

// The palette field(s) that show each role (the first one is the role's color)
lv_color_t Palette::* const kRoleField[kRoles] = {
    &Palette::screen, &Palette::cell, &Palette::line_thin, &Palette::ink, &Palette::entry,
    &Palette::selected, &Palette::same, &Palette::peer, &Palette::key, &Palette::key_on,
};

void apply_custom(const CustomTheme& ct, Palette& out)
{
    out = base_of(ct);
    auto picked = [&](Role r) { return (ct.set >> static_cast<int>(r)) & 1; };
    auto col = [&](Role r) { return lv_color_hex(ct.color[static_cast<int>(r)]); };
    for (int r = 0; r < kRoles; ++r)
        if (picked(static_cast<Role>(r))) out.*kRoleField[r] = col(static_cast<Role>(r));
    // Roles that drive more than one field
    if (picked(Role::GridLines)) out.key_border = out.line_thin;
    if (picked(Role::Text)) {
        out.given = out.line_thick = out.note_match = out.ink;
    }
    if (picked(Role::Text) || picked(Role::Background))
        out.muted = out.note = lv_color_mix(out.ink, out.screen, 165);
    if (picked(Role::Text) || picked(Role::Buttons)) {
        out.key_pressed = lv_color_mix(out.ink, out.key, 60);
        out.key_dim_text = lv_color_mix(out.ink, out.key, 90);
    }
    if (picked(Role::Accent)) out.key_on_text = contrast_text(out.key_on);
}

} // namespace

const char* role_name(Role r)
{
    static const char* const names[kRoles] = {
        "Background", "Board", "Grid lines", "Text", "Your marks",
        "Selected", "Matching", "Row/col", "Buttons", "Accent",
    };
    return names[static_cast<int>(r)];
}

// 8 greys, then 8 hues (red, orange, yellow, green, teal, blue, purple,
// pink) in 5 shades from pale to deep.
const uint32_t kPalette[kPaletteColors] = {
    0xFFFFFF, 0xE4E6E9, 0xC4C8CE, 0x9AA0A8, 0x6E747C, 0x474C53, 0x24282D, 0x000000,
    0xF9DCDC, 0xF9E8DC, 0xF9F3DC, 0xE1F9DC, 0xDCF9F4, 0xDCE8F9, 0xEBDCF9, 0xF9DCED,
    0xF2A6A6, 0xF2C6A6, 0xF2E3A6, 0xB3F2A6, 0xA6F2E6, 0xA6C6F2, 0xCCA6F2, 0xF2A6D2,
    0xE45858, 0xE49258, 0xE4C858, 0x70E458, 0x58E4CD, 0x5892E4, 0x9E58E4, 0xE458AA,
    0xC91D1D, 0xC9641D, 0xC9A61D, 0x39C91D, 0x1DC9AC, 0x1D64C9, 0x731DC9, 0xC91D81,
    0x821717, 0x824417, 0x826D17, 0x298217, 0x178270, 0x174482, 0x4C1782, 0x821755,
};

lv_color_t contrast_text(lv_color_t bg)
{
    const int lum = 299 * bg.red + 587 * bg.green + 114 * bg.blue;   // 0..255000
    return lum > 140000 ? lv_color_hex(0x16130A) : lv_color_hex(0xFFFFFF);
}

void set_custom_themes(const CustomThemes& t)
{
    build();
    customs = t;
    for (auto& c : customs.slot) if (c.base > 1) c.base = 0;
    if (is_custom(current)) apply_custom(customs.slot[custom_slot(current)], p_custom);
}

const CustomThemes& custom_themes() { return customs; }

void set_custom_theme(int slot, const CustomTheme& t)
{
    if (slot < 0 || slot >= kCustomThemes) return;
    customs.slot[slot] = t;
    if (customs.slot[slot].base > 1) customs.slot[slot].base = 0;
    if (current == custom_theme(slot)) apply_custom(customs.slot[slot], p_custom);
}

uint32_t role_color(Theme t, Role r)
{
    build();
    const int k = static_cast<int>(r);
    if (is_custom(t)) {
        const CustomTheme& ct = customs.slot[custom_slot(t)];
        if ((ct.set >> k) & 1) return ct.color[k];
        return hex_of(base_of(ct).*kRoleField[k]);
    }
    return hex_of((t == Theme::Dark ? p_dark : p_light).*kRoleField[k]);
}

void set_theme(Theme t)
{
    build();
    if (static_cast<uint8_t>(t) >= kThemeCount) t = Theme::Light;
    current = t;
    if (is_custom(t)) apply_custom(customs.slot[custom_slot(t)], p_custom);
}

Theme theme() { return current; }

const Palette& pal()
{
    build();
    if (is_custom(current)) return p_custom;
    return current == Theme::Dark ? p_dark : p_light;
}

const char* theme_name(Theme t)
{
    switch (t) {
        case Theme::Light:   return "Light";
        case Theme::Dark:    return "Dark";
        case Theme::Custom1: return "Custom 1";
        case Theme::Custom2: return "Custom 2";
        case Theme::Custom3: return "Custom 3";
    }
    return "Light";
}

} // namespace ui
