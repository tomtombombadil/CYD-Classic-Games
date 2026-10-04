#include "cards.h"

#include <cstdio>
#include <cstring>
#include <new>
#include "game_kit.h"
#include "ui/theme.h"
#include "ui/shell.h"
#include "ui/widgets.h"

extern "C" {
extern const lv_font_t card_font_10, card_font_12, card_font_14, card_font_16, card_font_18, card_font_20, card_font_24,
    card_font_28, card_font_34, card_font_46;
extern const lv_font_t card_b_font_16, card_b_font_22, card_b_font_30, card_b_font_40, card_b_font_54, card_b_font_70;
}

namespace cards {

namespace {

const lv_font_t* const kFonts[] = {&card_font_10, &card_font_12, &card_font_14, &card_font_16, &card_font_18,
                                   &card_font_20, &card_font_24, &card_font_28, &card_font_34, &card_font_46};
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

// Pattern backs: blue, red, green, each deep enough for cream lines
lv_color_t back_color(int c)
{
    const ui::Palette& P = ui::pal();
    switch (c) {
        case 1:  return lv_color_mix(P.piece_a, P.stone_dark, 190);    // wine red
        case 2:  return lv_color_mix(P.felt, P.stone_dark, 170);       // deep green
        default: return lv_color_mix(P.frame, P.stone_dark, 170);      // night blue
    }
}

const lv_font_t* const kBFonts[] = {&card_b_font_16, &card_b_font_22, &card_b_font_30,
                                    &card_b_font_40, &card_b_font_54, &card_b_font_70};

void circle(lv_layer_t* layer, int cx, int cy, int r, lv_color_t c)
{
    kit::fill_rect(layer, cx - r, cy - r, cx + r, cy + r, c, r);
}

// A small four-point star: two thin crossed diamonds as rectangles + a dot
void star(lv_layer_t* layer, int cx, int cy, int r, lv_color_t c)
{
    const int t = r >= 4 ? 1 : 0;
    kit::fill_rect(layer, cx - t, cy - r, cx + t, cy + r, c, 0);
    kit::fill_rect(layer, cx - r, cy - t, cx + r, cy + t, c, 0);
    circle(layer, cx, cy, r / 2 > 1 ? r / 2 : 1, c);
}

// The serif "B": dark outline (the glyph drawn around in 8 directions),
// then the soft gold letter on top, like the splash title
void bombadil_b(lv_layer_t* layer, int x1, int y1, int x2, int y2, lv_color_t base)
{
    const ui::Palette& P = ui::pal();
    const int pw = x2 - x1, ph = y2 - y1;
    const lv_font_t* f = kBFonts[0];
    for (const lv_font_t* c : kBFonts) {
        const Box b = glyph_box(c, 'B');
        if (b.h <= ph * 62 / 100 && b.w <= pw * 75 / 100) f = c;
    }
    const Box b = glyph_box(f, 'B');
    const int gx = x1 + (pw - b.w) / 2, gy = y1 + (ph - b.h) / 2;
    const int o = b.h >= 30 ? 2 : 1;
    const lv_color_t edge = lv_color_mix(P.stone_dark, base, 200);
    for (int dy = -o; dy <= o; ++dy)
        for (int dx = -o; dx <= o; ++dx)
            if (dx || dy) draw_at(layer, "B", f, edge, gx + dx, gy + dy, 'B');
    draw_at(layer, "B", f, lv_color_mix(P.lit, P.stone_light, 200), gx, gy, 'B');   // mild gold
}

} // namespace

const char* rank_text(int r) { return r >= 1 && r <= 13 ? kRanks[r] : ""; }
const char* suit_text(int s) { return kSuits[s & 3]; }

const char* back_name(int back)
{
    static const char* const pictures[3] = {"Bombadil", "Moon", "Tree"};
    static const char* const patterns[3] = {"Lattice", "Stripes", "Dots"};
    static const char* const colors[3] = {"Blue", "Red", "Green"};
    static char buf[24];
    back = back < 0 || back >= kBacks ? 0 : back;
    if (back < BackLattice) return pictures[back];
    const int k = back - BackLattice;
    snprintf(buf, sizeof buf, "%s %s", patterns[k / 3], colors[k % 3]);
    return buf;
}

uint8_t current_back()
{
    const uint8_t b = ui::settings().card_back;
    return b < kBacks ? b : uint8_t(BackLattice);
}

// The card table: the theme's felt, about 15 % darker (Tom, 2026-10-03)
lv_color_t felt() { return lv_color_darken(ui::pal().felt, 38); }

int index_h(int w, int h)
{
    int s = h * 36 / 100;                   // bigger than it was: the rank must read on a 2.8"
    if (s < 12) s = 12;
    if (s > w / 2) s = w / 2;
    return s;
}

namespace {
// The index and body sizes for one card size, worked out once (font fitting
// for 52 cards on every redraw was slow - Tom felt taps lag)
struct Layout {
    int w = 0, h = 0, pad = 0, sh = 0, rank_h = 0, squeeze = 0;
    const lv_font_t* rank = nullptr;
    const lv_font_t* suit = nullptr;
    const lv_font_t* body[4] = {};
};
Layout cache[6];
int cache_next = 0;

int ten_w(const lv_font_t* f, int squeeze) { return text_w("1", f) - squeeze + text_w("0", f); }

const Layout& layout(int w, int h)
{
    for (const Layout& c : cache) if (c.w == w && c.h == h) return c;
    Layout& L = cache[cache_next];
    cache_next = (cache_next + 1) % 6;
    L = Layout{};
    L.w = w; L.h = h;
    L.pad = w >= 40 ? 3 : w >= 26 ? 2 : 1;
    L.sh = index_h(w, h);
    const int ink_h = L.sh * 80 / 100, avail = w - L.pad - 1;
    L.rank = kFonts[0];
    L.suit = kFonts[0];
    for (int i = 0; i < kFontCount; ++i) {
        const lv_font_t* sf = kFonts[i > 0 ? i - 1 : 0];
        const int sq = lv_font_get_line_height(kFonts[i]) / 9;
        const Box rb = glyph_box(kFonts[i], '8');
        if (rb.h <= ink_h && ten_w(kFonts[i], sq) + 1 + glyph_box(sf, kSuitCode[0]).w <= avail) {
            L.rank = kFonts[i];
            L.suit = sf;
            L.squeeze = sq;
        }
    }
    L.rank_h = glyph_box(L.rank, '8').h;
    const int by = L.sh + 1, bh = h - L.pad - by;
    for (int s = 0; s < 4; ++s) L.body[s] = fit(kSuits[s], kSuitCode[s], w - 2 * L.pad - 2, bh - 2);
    return L;
}
} // namespace

void draw_face(lv_layer_t* layer, int x, int y, int w, int h, uint8_t card, bool selected)
{
    if (card >= 52) return;                     // never index the suit tables with junk
    const ui::Palette& P = ui::pal();
    const int rad = w / 8;
    // Picked cards (Tom: must be obvious): a thick amber edge and the whole
    // face tinted gold, strong enough to read on a TN panel at an angle
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, selected ? P.selected : P.key_border, rad);
    const int bw = selected ? (w >= 40 ? 3 : 2) : 1;
    const lv_color_t face = selected ? lv_color_mix(P.same, P.stone_light, 150) : P.stone_light;
    kit::fill_rect(layer, x + bw, y + bw, x + w - 1 - bw, y + h - 1 - bw, face, rad > bw ? rad - bw : 0);

    const int rank = rank_of(card), suit = suit_of(card);
    const lv_color_t ink = is_red(card) ? P.piece_a : P.stone_dark;
    const char* rs = kRanks[rank];
    const uint32_t sc = kSuitCode[suit];

    // Index strip: the rank as big as fits (Tom: on the 2.8" the digits must
    // read apart), then the suit a size smaller right after it. Sized for
    // the widest rank, "10" (its digits drawn a little closer), so every
    // card's index matches. Worked out once per card size.
    const Layout& L = layout(w, h);
    const int ty = y + L.pad + (L.sh - L.pad - L.rank_h) / 2;
    int rx = x + L.pad;
    if (rank == 10) {
        draw_at(layer, "1", L.rank, ink, rx, ty, '8');
        rx += text_w("1", L.rank) - L.squeeze;
        draw_at(layer, "0", L.rank, ink, rx, ty, '8');
        rx += text_w("0", L.rank);
    } else {
        draw_at(layer, rs, L.rank, ink, rx, ty, '8');
        rx += text_w(rs, L.rank);
    }
    const Box sb = glyph_box(L.suit, sc);
    draw_at(layer, kSuits[suit], L.suit, ink, rx + 1, ty + (L.rank_h - sb.h) / 2, sc);

    // Body: one big suit in the space below the strip
    const int by = y + L.sh + 1, bh = y + h - L.pad - by;
    const lv_font_t* bf = L.body[suit];
    const Box bb = glyph_box(bf, sc);
    draw_at(layer, kSuits[suit], bf, ink, x + (w - bb.w) / 2, by + (bh - bb.h) / 2, sc);
}

void draw_back(lv_layer_t* layer, int x, int y, int w, int h, int back)
{
    const ui::Palette& P = ui::pal();
    back = back < 0 || back >= kBacks ? 0 : back;
    const int rad = w / 8;
    const lv_color_t cream = P.stone_light;
    const lv_color_t blue = back_color(0);
    lv_color_t base = blue;
    if (back == BackTree) base = lv_color_mix(P.felt, P.stone_dark, 150);
    if (back == BackMoon) base = P.stone_dark;                       // a black night (Tom)
    if (back >= BackLattice) base = back_color((back - BackLattice) % 3);
    const lv_color_t fine = lv_color_mix(cream, base, 90);    // pattern lines: cream, a third strength
    kit::fill_rect(layer, x, y, x + w - 1, y + h - 1, P.key_border, rad);
    kit::fill_rect(layer, x + 1, y + 1, x + w - 2, y + h - 2, cream, rad > 1 ? rad - 1 : 0);
    // Inner panel inside a cream margin
    const int m = w >= 40 ? 4 : 3;
    const int x1 = x + m, y1 = y + m, x2 = x + w - 1 - m, y2 = y + h - 1 - m;
    kit::fill_rect(layer, x1, y1, x2, y2, base, rad / 2);
    // patterns stay inside the panel, off its rounded corners
    const int cx1 = x1 + 2, cy1 = y1 + 2, cx2 = x2 - 2, cy2 = y2 - 2;
    const int pw = x2 - x1, ph = y2 - y1;
    const int step = w >= 40 ? 8 : 6;
    const int pattern = back >= BackLattice ? (back - BackLattice) / 3 : -1;
    switch (back >= BackLattice ? -1 : back) {
        case BackBombadil:
            bombadil_b(layer, x1, y1, x2, y2, base);
            return;
        case BackMoon: {                                   // a crescent and one star, nothing else
            const lv_color_t moon = lv_color_mix(P.lit, cream, 190);
            const int r = (pw < ph ? pw : ph) * 30 / 100;
            const int mx = x1 + pw * 44 / 100, my = y1 + ph * 56 / 100;
            circle(layer, mx, my, r, moon);
            circle(layer, mx + r * 45 / 100, my - r * 30 / 100, r * 85 / 100, base);   // the bite
            const int sr = w >= 40 ? 4 : 2;
            star(layer, x1 + pw * 78 / 100, y1 + ph * 17 / 100, sr, moon);              // top right corner
            return;
        }
        case BackTree: {                                   // round crown on a short trunk
            const lv_color_t leaf = lv_color_mix(P.win, P.felt, 140), leaf2 = lv_color_mix(leaf, cream, 215);
            const lv_color_t bark = P.sq_dark;
            const int cx = x1 + pw / 2, r = pw * 30 / 100;
            const int ground = y2 - ph * 14 / 100;
            const int tw = pw / 7 > 2 ? pw / 7 : 2;
            kit::fill_rect(layer, cx - tw / 2, ground - ph * 34 / 100, cx + tw / 2, ground, bark, 0);
            kit::fill_rect(layer, x1 + pw / 6, ground, x2 - pw / 6, ground + (w >= 40 ? 2 : 1), bark, 1);
            const int cy = ground - ph * 34 / 100 - r / 3;
            circle(layer, cx - r * 6 / 10, cy + r / 4, r * 7 / 10, leaf);
            circle(layer, cx + r * 6 / 10, cy + r / 4, r * 7 / 10, leaf);
            circle(layer, cx, cy - r / 3, r * 8 / 10, leaf);
            circle(layer, cx - r / 4, cy - r / 2, r / 4 > 1 ? r / 4 : 1, leaf2);    // a touch of light
            return;
        }
        default: break;
    }
    switch (pattern) {
        case 0:                      // diamond lattice: both diagonals
            for (int k = -ph; k < pw + ph; k += step) {
                clipped_line(layer, x1 + k, y1, x1 + k + ph, y2, 1, fine, cx1, cy1, cx2, cy2);
                clipped_line(layer, x1 + k, y2, x1 + k + ph, y1, 1, fine, cx1, cy1, cx2, cy2);
            }
            break;
        case 1:                      // one diagonal, wider bands
            for (int k = -ph; k < pw + ph; k += step)
                clipped_line(layer, x1 + k, y1, x1 + k + ph, y2, step / 3 > 1 ? step / 3 : 2, fine,
                             cx1 + 1, cy1 + 1, cx2 - 1, cy2 - 1);
            break;
        default: {                   // offset dot grid
            const int r = w >= 40 ? 2 : 1;
            for (int j = 0, yy = y1 + step / 2; yy + r < y2 - 1; yy += step, ++j)
                for (int xx = x1 + step / 2 + (j % 2) * step / 2; xx + r < x2 - 1; xx += step)
                    kit::fill_rect(layer, xx - r, yy - r, xx + r, yy + r, fine, r);
            break;
        }
    }
}

namespace {
int picker_tile = 0, picker_gap = 0;

void picker_draw_cb(lv_event_t* e)
{
    lv_obj_t* o = lv_event_get_target_obj(e);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int tw = picker_tile, th = tw * 7 / 5, cur = current_back();
    for (int k = 0; k < kBacks; ++k) {
        const int x = a.x1 + (k % 6) * (tw + picker_gap) + 3, y = a.y1 + (k / 6) * (th + picker_gap) + 3;
        if (k == cur) kit::fill_rect(layer, x - 3, y - 3, x + tw + 2, y + th + 2, ui::pal().selected, tw / 6);
        draw_back(layer, x, y, tw, th, k);
    }
}

void picker_press_cb(lv_event_t* e)
{
    lv_obj_t* o = lv_event_get_target_obj(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    const int tw = picker_tile, th = tw * 7 / 5;
    const int c = (p.x - a.x1) / (tw + picker_gap), r = (p.y - a.y1) / (th + picker_gap);
    if (c < 0 || c >= 6 || r < 0 || r >= 2) return;
    ui::settings().card_back = uint8_t(r * 6 + c);
    ui::save_settings();
    lv_obj_invalidate(o);
}
} // namespace

namespace {
void (*back_screen_return)() = nullptr;
lv_obj_t* back_name_label = nullptr;
void back_screen_cb(lv_event_t*) { if (back_screen_return) back_screen_return(); }
void back_name_cb(lv_event_t*) { if (back_name_label) lv_label_set_text(back_name_label, back_name(current_back())); }
}

void back_screen(void (*back)())
{
    back_screen_return = back;
    ui::overlay_begin("Card Back");
    const ui::Metrics& m = ui::metrics();
    lv_obj_t* pk = back_picker(ui::overlay(), m.w - 2 * (m.large ? 16 : 10));
    back_name_label = ui::overlay_text(back_name(current_back()), false);
    lv_obj_add_event_cb(pk, back_name_cb, LV_EVENT_PRESSED, nullptr);
    ui::overlay_text("Used by every card game.", true);
    ui::overlay_bottom_button("Back", back_screen_cb, 0);
}

lv_obj_t* back_picker(lv_obj_t* parent, int w)
{
    picker_gap = w >= 300 ? 8 : 6;
    picker_tile = (w - 6 - 5 * picker_gap) / 6;
    const int th = picker_tile * 7 / 5;
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, 2 * th + picker_gap + 6);
    lv_obj_set_clickable(o, true);
    lv_obj_add_event_cb(o, picker_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(o, picker_press_cb, LV_EVENT_PRESSED, nullptr);
    return o;
}

// ---- The win show ------------------------------------------------------------------
namespace {

struct Show {
    Launch*     list = nullptr;
    int         n = 0, i = 0, cw = 0, ch = 0, W = 0, H = 0;
    bool        flying = false;
    float       x = 0, y = 0, vx = 0, vy = 0, g = 0;
    uint32_t    rnd = 1;
    lv_timer_t* timer = nullptr;
    lv_obj_t*   obj = nullptr;
    void      (*done)() = nullptr;
    // piles already emptied: their position and what they show now
    int16_t     px[32] = {}, py[32] = {};
    uint8_t     shown[32] = {};
    int         piles = 0;
};
Show* show = nullptr;

uint32_t show_rand() { show->rnd = show->rnd * 1103515245u + 12345u; return show->rnd >> 8; }

void invalidate(int x, int y, int w, int h)
{
    lv_area_t a{x, y, x + w - 1, y + h - 1};
    lv_obj_invalidate_area(show->obj, &a);
}

void show_draw_cb(lv_event_t* e)
{
    if (!show) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    for (int k = 0; k < show->piles; ++k) {
        if (show->shown[k] == 0xFE) continue;
        if (show->shown[k] == 0xFF) draw_slot(layer, show->px[k], show->py[k], show->cw, show->ch);
        else draw_face(layer, show->px[k], show->py[k], show->cw, show->ch, show->shown[k]);
    }
    if (show->flying) draw_face(layer, int(show->x), int(show->y), show->cw, show->ch, show->list[show->i].card);
}

void show_timer_cb(lv_timer_t*)
{
    Show& s = *show;
    if (!s.flying) {
        if (s.i >= s.n) return;                       // all gone: wait for the tap
        const Launch& L = s.list[s.i];
        s.x = L.x; s.y = L.y;
        const float speed = s.W * (0.006f + 0.012f * float(show_rand() % 100) / 100.0f);
        s.vx = (show_rand() & 1) ? speed : -speed;
        s.vy = -float(show_rand() % 100) / 100.0f * s.H * 0.012f;
        s.flying = true;
        // the pile now shows the card under it
        int k = 0;
        while (k < s.piles && (s.px[k] != L.x || s.py[k] != L.y)) ++k;
        if (k == s.piles && k < 32) { s.px[k] = L.x; s.py[k] = L.y; ++s.piles; }
        if (k < 32) s.shown[k] = L.under;
        invalidate(L.x, L.y, s.cw, s.ch);
        return;
    }
    s.vy += s.g;
    s.x += s.vx;
    s.y += s.vy;
    if (s.y + s.ch > s.H) {                           // bounce off the bottom
        s.y = float(s.H - s.ch);
        s.vy = -s.vy * 0.78f;
    }
    if (s.x + s.cw < 0 || s.x > s.W) { s.flying = false; ++s.i; return; }
    invalidate(int(s.x), int(s.y), s.cw, s.ch);
}

void show_press_cb(lv_event_t*)
{
    void (*done)() = show ? show->done : nullptr;
    celebrate_stop();
    if (done) done();
}

} // namespace

void celebrate(const Launch* list, int n, int cw, int ch, void (*done)())
{
    celebrate_stop();
    show = new (std::nothrow) Show();
    if (!show) { if (done) done(); return; }
    show->list = new (std::nothrow) Launch[n > 0 ? n : 1];
    if (!show->list) { delete show; show = nullptr; if (done) done(); return; }
    for (int i = 0; i < n; ++i) show->list[i] = list[i];
    show->n = n;
    show->cw = cw;
    show->ch = ch;
    show->W = lv_display_get_horizontal_resolution(nullptr);
    show->H = lv_display_get_vertical_resolution(nullptr);
    show->g = show->H * 0.0012f;
    show->rnd = lv_tick_get() | 1;
    show->done = done;
    show->obj = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(show->obj);
    lv_obj_set_size(show->obj, show->W, show->H);
    lv_obj_set_clickable(show->obj, true);
    lv_obj_add_event_cb(show->obj, show_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(show->obj, show_press_cb, LV_EVENT_PRESSED, nullptr);
    show->timer = lv_timer_create(show_timer_cb, 25, nullptr);
}

void celebrate_stop()
{
    if (!show) return;
    if (show->timer) lv_timer_delete(show->timer);
    if (show->obj) {
        lv_obj_delete_async(show->obj);
        lv_obj_invalidate(lv_screen_active());        // wipe the trails
    }
    delete[] show->list;
    delete show;
    show = nullptr;
}

bool celebrating() { return show != nullptr; }

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
