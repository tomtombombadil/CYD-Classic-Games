#include "cards.h"

#include <cstring>
#include "game_kit.h"
#include "ui/theme.h"
#include "ui/widgets.h"

extern "C" {
extern const lv_font_t card_font_12, card_font_16, card_font_20, card_font_26, card_font_34, card_font_46;
}

namespace cards {

namespace {

const lv_font_t* const kFonts[] = {&card_font_12, &card_font_16, &card_font_20,
                                   &card_font_26, &card_font_34, &card_font_46};
constexpr int kFontCount = sizeof kFonts / sizeof kFonts[0];

const char* const kRanks[14] = {"", "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"};
// U+2660 spade, U+2665 heart, U+2666 diamond, U+2663 club (solid)
const char* const kSuits[4] = {"\xE2\x99\xA0", "\xE2\x99\xA5", "\xE2\x99\xA6", "\xE2\x99\xA3"};
const uint32_t kSuitCode[4] = {0x2660, 0x2665, 0x2666, 0x2663};

// Ink box of one glyph
struct Box { int w, h, top; };     // top = offset of the ink from the line top
Box glyph_box(const lv_font_t* f, uint32_t code)
{
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(f, &g, code, 0)) return {0, 0, 0};
    const int lh = lv_font_get_line_height(f);
    return {int(g.box_w), int(g.box_h), lh - f->base_line - g.ofs_y - int(g.box_h)};
}

int text_w(const char* s, const lv_font_t* f) { return ui::text_width(s, f); }

// Draw `s` with its ink (judged by `ref` glyph) top-left at x, y
void draw_at(lv_layer_t* layer, const char* s, const lv_font_t* f, lv_color_t c, int x, int y, uint32_t ref)
{
    const Box b = glyph_box(f, ref);
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = f;
    d.color = c;
    const int top = y - b.top;
    lv_area_t a{x, top, x + text_w(s, f) + 2, top + lv_font_get_line_height(f)};
    lv_draw_label(layer, &d, &a);
}

// Largest font whose ink fits h tall and w wide for `s` (ref glyph for height)
const lv_font_t* fit(const char* s, uint32_t ref, int w, int h)
{
    const lv_font_t* best = kFonts[0];
    for (int i = 0; i < kFontCount; ++i) {
        const Box b = glyph_box(kFonts[i], ref);
        if (b.h <= h && text_w(s, kFonts[i]) <= w) best = kFonts[i];
    }
    return best;
}

// Clip a line segment to the box (Liang-Barsky); false if nothing is left
bool clip_line(int& x1, int& y1, int& x2, int& y2, int bx1, int by1, int bx2, int by2)
{
    float t0 = 0, t1 = 1;
    const float dx = float(x2 - x1), dy = float(y2 - y1);
    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {float(x1 - bx1), float(bx2 - x1), float(y1 - by1), float(by2 - y1)};
    for (int i = 0; i < 4; ++i) {
        if (p[i] == 0) { if (q[i] < 0) return false; continue; }
        const float r = q[i] / p[i];
        if (p[i] < 0) { if (r > t1) return false; if (r > t0) t0 = r; }
        else          { if (r < t0) return false; if (r < t1) t1 = r; }
    }
    const int ox = x1, oy = y1;
    x1 = int(ox + t0 * dx + 0.5f); y1 = int(oy + t0 * dy + 0.5f);
    x2 = int(ox + t1 * dx + 0.5f); y2 = int(oy + t1 * dy + 0.5f);
    return true;
}

void clipped_line(lv_layer_t* layer, int x1, int y1, int x2, int y2, int w, lv_color_t c,
                  int bx1, int by1, int bx2, int by2)
{
    if (clip_line(x1, y1, x2, y2, bx1, by1, bx2, by2)) kit::line(layer, x1, y1, x2, y2, w, c);
}

lv_color_t back_color(int c)
{
    const ui::Palette& P = ui::pal();
    switch (c) {
        case 1:  return lv_color_mix(P.piece_a, P.stone_dark, 190);    // wine red
        case 2:  return lv_color_mix(P.felt, P.stone_dark, 170);       // deep green
        default: return lv_color_mix(P.frame, P.stone_dark, 170);      // night blue
    }
}

} // namespace

const char* rank_text(int r) { return r >= 1 && r <= 13 ? kRanks[r] : ""; }
const char* suit_text(int s) { return kSuits[s & 3]; }

const char* back_name(int p)
{
    static const char* const n[kBackPatterns] = {"Lattice", "Stripes", "Dots", "Starry Night"};
    return n[p % kBackPatterns];
}

const char* back_color_name(int c)
{
    static const char* const n[kBackColors] = {"Blue", "Red", "Green"};
    return n[c % kBackColors];
}

lv_color_t felt() { return ui::pal().felt; }

int index_h(int w, int h)
{
    int s = h * 30 / 100;
    if (s < 12) s = 12;
    if (s > w / 2) s = w / 2;
    return s;
}

void draw_face(lv_layer_t* layer, int x, int y, int w, int h, uint8_t card, bool selected)
{
    const ui::Palette& P = ui::pal();
    const int rad = w / 8;
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, selected ? P.selected : P.key_border, rad);
    const int bw = selected ? (w >= 40 ? 3 : 2) : 1;
    kit::fill_rect(layer, x + bw, y + bw, x + w - 1 - bw, y + h - 1 - bw, P.stone_light, rad > bw ? rad - bw : 0);

    const int rank = rank_of(card), suit = suit_of(card);
    const lv_color_t ink = is_red(card) ? P.piece_a : P.stone_dark;
    const char* rs = kRanks[rank];
    const uint32_t sc = kSuitCode[suit];

    // Index strip: rank, then a small suit, ink filling ~70 % of the strip
    const int pad = w >= 40 ? 4 : 2, sh = index_h(w, h);
    const int ink_h = sh * 72 / 100;
    const int avail_w = w - 2 * pad - 1;
    const lv_font_t* rf = kFonts[0];
    for (int i = 0; i < kFontCount; ++i) {
        const Box rb = glyph_box(kFonts[i], 'K');
        const Box sb = glyph_box(kFonts[i], sc);
        // sized for the widest rank, "10", so every card's index matches
        if (rb.h <= ink_h && text_w("10", kFonts[i]) + 1 + sb.w <= avail_w) rf = kFonts[i];
    }
    const int ty = y + pad + (sh - pad - glyph_box(rf, 'K').h) / 2;
    draw_at(layer, rs, rf, ink, x + pad, ty, 'K');
    const Box sb = glyph_box(rf, sc), kb = glyph_box(rf, 'K');
    // suit right after the rank, so a sideways fan that shows the rank shows the suit too
    draw_at(layer, kSuits[suit], rf, ink, x + pad + text_w(rs, rf) + 1, ty + (kb.h - sb.h) / 2, sc);

    // Body: one big suit in the space below the strip
    const int by = y + sh + 1, bh = y + h - pad - by;
    const lv_font_t* bf = fit(kSuits[suit], sc, w - 2 * pad - 2, bh - 2);
    const Box bb = glyph_box(bf, sc);
    draw_at(layer, kSuits[suit], bf, ink, x + (w - bb.w) / 2, by + (bh - bb.h) / 2, sc);
}

void draw_back(lv_layer_t* layer, int x, int y, int w, int h, Look look)
{
    const ui::Palette& P = ui::pal();
    const int rad = w / 8;
    const lv_color_t base = back_color(look.color);
    const lv_color_t cream = P.stone_light;
    const lv_color_t fine = lv_color_mix(cream, base, 90);    // pattern lines: cream, a third strength
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, P.key_border, rad);
    kit::fill_rect(layer, x + 1, y + 1, x + w - 2, y + h - 2, cream, rad > 1 ? rad - 1 : 0);
    // Inner panel inside a cream margin
    const int m = w >= 40 ? 4 : 3;
    const int x1 = x + m, y1 = y + m, x2 = x + w - 1 - m, y2 = y + h - 1 - m;
    kit::fill_rect(layer, x1, y1, x2, y2, base, rad / 2);

    // pattern stays inside the panel, off its rounded corners
    const int cx1 = x1 + 2, cy1 = y1 + 2, cx2 = x2 - 2, cy2 = y2 - 2;
    const int pw = x2 - x1, ph = y2 - y1;
    const int step = w >= 40 ? 8 : 6;
    switch (static_cast<Back>(look.back % kBackPatterns)) {
        case Back::Lattice:          // diamond lattice: both diagonals
            for (int k = -ph; k < pw + ph; k += step) {
                clipped_line(layer, x1 + k, y1, x1 + k + ph, y2, 1, fine, cx1, cy1, cx2, cy2);
                clipped_line(layer, x1 + k, y2, x1 + k + ph, y1, 1, fine, cx1, cy1, cx2, cy2);
            }
            break;
        case Back::Stripes:          // one diagonal, wider bands
            for (int k = -ph; k < pw + ph; k += step)
                clipped_line(layer, x1 + k, y1, x1 + k + ph, y2, step / 3 > 1 ? step / 3 : 2, fine,
                             cx1 + 1, cy1 + 1, cx2 - 1, cy2 - 1);
            break;
        case Back::Dots: {           // offset dot grid
            const int r = w >= 40 ? 2 : 1;
            for (int j = 0, yy = y1 + step / 2; yy + r < y2 - 1; yy += step, ++j)
                for (int xx = x1 + step / 2 + (j % 2) * step / 2; xx + r < x2 - 1; xx += step)
                    kit::fill_rect(layer, xx - r, yy - r, xx + r, yy + r, fine, r);
            break;
        }
        case Back::Night: {          // the splash art's night sky: gold stars and a moon
            const lv_color_t gold = P.lit;
            static const uint8_t stars[][2] = {{15, 12}, {38, 18}, {40, 35}, {85, 50}, {20, 60},
                                               {60, 72}, {35, 88}, {80, 92}, {12, 40}, {52, 55}};
            for (auto& s : stars) {
                const int sx = x1 + pw * s[0] / 100, sy = y1 + ph * s[1] / 100;
                const int r = (s[0] + s[1]) % 3 == 0 && w >= 40 ? 2 : 1;
                kit::fill_rect(layer, sx - r, sy - r, sx + r, sy + r, gold, r);
            }
            const int mr = (pw < ph ? pw : ph) / 5;                     // crescent, inside the panel
            const int mx = x1 + pw * 62 / 100, my = y1 + mr + mr / 4 + 3;
            kit::fill_rect(layer, mx - mr, my - mr, mx + mr, my + mr, gold, mr);
            kit::fill_rect(layer, mx - mr + mr / 2, my - mr - mr / 4, mx + mr + mr / 2, my + mr - mr / 4, base, mr);
            break;
        }
    }
}

void draw_slot(lv_layer_t* layer, int x, int y, int w, int h, int suit)
{
    const ui::Palette& P = ui::pal();
    const int rad = w / 8;
    const lv_color_t edge = lv_color_mix(P.stone_light, felt(), 110);
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, edge, rad);
    const lv_color_t inner = lv_color_mix(P.stone_dark, felt(), 40);
    kit::fill_rect(layer, x + 2, y + 2, x + w - 3, y + h - 3, inner, rad > 2 ? rad - 2 : 0);
    if (suit >= 0) {
        const uint32_t sc = kSuitCode[suit & 3];
        const lv_font_t* f = fit(kSuits[suit & 3], sc, w * 6 / 10, h * 5 / 10);
        const Box b = glyph_box(f, sc);
        draw_at(layer, kSuits[suit & 3], f, edge, x + (w - b.w) / 2, y + (h - b.h) / 2, sc);
    }
}

} // namespace cards
