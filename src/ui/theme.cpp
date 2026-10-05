#include "theme.h"

namespace ui {

namespace {

Theme        current = Theme::Light;
Palette      p_light, p_dark, p_custom;
bool         built = false;
CustomThemes customs;

// Both themes take their colors from the splash art (Tom Tom Bombadil in a
// twilight glade): Light is the warm parchment and cream of the title
// banners with the night-sky navy as ink, Dark is that night sky with cream
// text. Accents are the coat's gold, the hat's blue, the glade's teal-green
// and the wood's warm brown, kept muted so the games stay calm. Highlights
// still use clearly different hues (teal-blue rows, gold same-digit, amber
// selection) so they survive a TN panel seen at an angle.
void build()
{
    if (built) return;
    built = true;

    Palette& l = p_light;
    l.screen       = lv_color_hex(0xF2EDDC);   // parchment
    l.cell         = lv_color_hex(0xFFFCF2);   // cream
    l.line_thin    = lv_color_hex(0xB5AD98);
    l.line_thick   = lv_color_hex(0x1D2F4F);   // night-sky navy
    l.given        = lv_color_hex(0x1D2F4F);
    l.entry        = lv_color_hex(0x1F62A6);   // hat blue
    l.hinted       = lv_color_hex(0x2C7A5E);   // glade green
    l.note         = lv_color_hex(0x5B6271);
    l.note_match   = lv_color_hex(0x0B1730);
    l.conflict     = lv_color_hex(0xB8322A);
    l.conflict_bg  = lv_color_hex(0xF0B9AE);
    l.peer         = lv_color_hex(0xC4DADF);   // pale teal-blue (strong enough off-angle)
    l.same         = lv_color_hex(0xF0C766);   // coat gold
    l.selected     = lv_color_hex(0xDE8E3A);   // lantern amber
    l.key          = lv_color_hex(0xFFFCF2);
    l.key_border   = lv_color_hex(0xB5AD98);
    l.key_pressed  = lv_color_hex(0xE4DCC6);
    l.key_on       = lv_color_hex(0xE0AA4C);
    l.key_on_text  = lv_color_hex(0x1E1B12);
    l.key_dim_text = lv_color_hex(0xBDB5A2);
    l.ink          = lv_color_hex(0x1D2F4F);
    l.muted        = lv_color_hex(0x5B6271);
    l.piece_a      = lv_color_hex(0xC2412F);   // red disc, O
    l.piece_b      = lv_color_hex(0xE9B53C);   // yellow disc
    l.frame        = lv_color_hex(0x1E5D8C);   // FourConnect board: splash blue
    l.lit          = lv_color_hex(0xE8AE3E);   // a light that is on (deeper than cream)
    l.win          = lv_color_hex(0x2C7A5E);   // winning line / solved
    l.sq_light     = lv_color_hex(0xEADCBA);   // board squares: pale wood
    l.sq_dark      = lv_color_hex(0xA36E50);   //                warm brown
    l.felt         = lv_color_hex(0x3C8569);   // Reversi table
    l.stone_dark   = lv_color_hex(0x1B2233);
    l.stone_light  = lv_color_hex(0xF7F2E4);
    l.target       = lv_color_hex(0x2C7A5E);   // where a picked piece may go
    l.absent       = lv_color_hex(0x8F897B);   // warm grey

    Palette& d = p_dark;
    d.screen       = lv_color_hex(0x000000);   // black (Tom, 2026-10-04); the rest stays night-sky navy
    d.cell         = lv_color_hex(0x172642);
    d.line_thin    = lv_color_hex(0x2F4268);
    d.line_thick   = lv_color_hex(0xA9BCD6);
    d.given        = lv_color_hex(0xF3EAD3);   // cream
    d.entry        = lv_color_hex(0x8CC6EA);
    d.hinted       = lv_color_hex(0x79CFAE);
    d.note         = lv_color_hex(0xA6B3C7);
    d.note_match   = lv_color_hex(0xFFFFFF);
    d.conflict     = lv_color_hex(0xFF8A7A);
    d.conflict_bg  = lv_color_hex(0x6A2A2A);
    d.peer         = lv_color_hex(0x24405E);
    d.same         = lv_color_hex(0x6E5A1F);
    d.selected     = lv_color_hex(0x9E621E);
    d.key          = lv_color_hex(0x1B2B4A);
    d.key_border   = lv_color_hex(0x3B5078);
    d.key_pressed  = lv_color_hex(0x2A3E64);
    d.key_on       = lv_color_hex(0xE0AA4C);
    d.key_on_text  = lv_color_hex(0x16130A);
    d.key_dim_text = lv_color_hex(0x4B5A78);
    d.ink          = lv_color_hex(0xF3EAD3);
    d.muted        = lv_color_hex(0xA6B3C7);
    d.piece_a      = lv_color_hex(0xE5604E);
    d.piece_b      = lv_color_hex(0xF0C24E);
    d.frame        = lv_color_hex(0x285F92);
    d.lit          = lv_color_hex(0xE8B94A);
    d.win          = lv_color_hex(0x79CFAE);
    d.sq_light     = lv_color_hex(0xB9A47E);
    d.sq_dark      = lv_color_hex(0x6B4733);
    d.felt         = lv_color_hex(0x2B6652);
    d.stone_dark   = lv_color_hex(0x10151F);
    d.stone_light  = lv_color_hex(0xF3EEDF);
    d.target       = lv_color_hex(0x79CFAE);
    d.absent       = lv_color_hex(0x58607A);
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
        "Background", "Board", "Grid Lines", "Text", "Your Marks",
        "Selected", "Matching", "Row/Col", "Buttons", "Accent",
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
