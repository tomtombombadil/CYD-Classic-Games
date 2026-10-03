// RPG Dice: registry entry, save file and screens (Tom, 2026-10-03).
//
// Main screen, top to bottom: top bar; the tray (the last roll drawn as
// dice - a d4 triangle, d6 with pips, d8 diamond, d10 kite, d12 pentagon,
// d20 hexagon, d100 as two d10s, the coin - each showing its roll, and the
// total); the pool being built ("2d6 + 1d8 + 3"); die keys (tap to add one);
// [-1] [+1] [Clear] [Roll]; [Presets] [History].
// Presets: up to 8 named sets of up to 4 labelled pools (Hit, Damage, ...)
// rolled with one tap and shown one row per line. History: every roll as a
// line of text, newest first, paged.
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/registry.h"
#include "rpgdice_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace rpgdice;
using namespace ui;

constexpr const char* kId = "rpgdice";

State*      S = nullptr;
bool        fresh = true;               // the next die tap starts a new pool
bool        dirty = false;
uint32_t    last_save_ms = 0;
kit::TopBar bar;
lv_obj_t*   tray_obj = nullptr;
lv_obj_t*   pool_l = nullptr;

void build();
void open_menu();
void open_presets();
void open_history(int page = 0);
void edit_preset(int slot);
void edit_line(int slot, int line);

// ---- Save --------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[State::kSaveBytes];
    if (!buf) return;
    const size_t n = S->serialize(buf, State::kSaveBytes);
    if (n) shell().save_game(kId, buf, n);
    delete[] buf;
    dirty = false;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[State::kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, State::kSaveBytes) : 0;
    const bool ok = n && st.deserialize(buf, n);
    delete[] buf;
    return ok;
}

// A first preset to show how they work (Tom's example: a fighter's two attacks)
void sample_preset(State& st)
{
    Preset& p = st.presets[0];
    snprintf(p.name, sizeof p.name, "Fighter Attacks");
    p.lines = 4;
    const int8_t mods[4] = {7, 5, 4, 5};
    const uint8_t labels[4] = {1, 2, 1, 2};               // Hit, Damage, Hit, Damage
    const Die dice[4] = {D20, D8, D20, D6};
    for (int l = 0; l < 4; ++l) {
        p.pool[l].clear();
        p.pool[l].count[dice[l]] = 1;
        p.pool[l].mod = mods[l];
        p.pool[l].label = labels[l];
    }
}

// ---- Dice pictures -----------------------------------------------------------------------
struct Pt { float x, y; };

void fill_poly(lv_layer_t* layer, const lv_point_precise_t* p, int n, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    for (int k = 1; k + 1 < n; ++k) {
        d.p[0] = p[0];
        d.p[1] = p[k];
        d.p[2] = p[k + 1];
        lv_draw_triangle(layer, &d);
    }
}

void outline(lv_layer_t* layer, const lv_point_precise_t* p, int n, int w, lv_color_t c)
{
    for (int k = 0; k < n; ++k) {
        const lv_point_precise_t& a = p[k];
        const lv_point_precise_t& b = p[(k + 1) % n];
        kit::line(layer, int32_t(a.x), int32_t(a.y), int32_t(b.x), int32_t(b.y), w, c);
    }
}

// Unit-shape corners (y down, radius 1) scaled to the die's box
int shape(int die, float cx, float cy, float r, lv_point_precise_t* out)
{
    static const Pt kD4[]  = {{0, -1}, {0.98f, 0.72f}, {-0.98f, 0.72f}};
    static const Pt kD8[]  = {{0, -1}, {0.82f, 0}, {0, 1}, {-0.82f, 0}};
    static const Pt kD10[] = {{0, -1}, {0.92f, 0.22f}, {0, 0.82f}, {-0.92f, 0.22f}};
    static const Pt kD12[] = {{0, -1}, {0.95f, -0.31f}, {0.59f, 0.81f}, {-0.59f, 0.81f}, {-0.95f, -0.31f}};
    static const Pt kD20[] = {{0, -1}, {0.87f, -0.5f}, {0.87f, 0.5f}, {0, 1}, {-0.87f, 0.5f}, {-0.87f, -0.5f}};
    const Pt* s = nullptr;
    int n = 0;
    switch (die) {
        case D4:   s = kD4;  n = 3; break;
        case D8:   s = kD8;  n = 4; break;
        case D10:
        case D100: s = kD10; n = 4; break;
        case D12:  s = kD12; n = 5; break;
        case D20:  s = kD20; n = 6; break;
        default:   return 0;
    }
    for (int k = 0; k < n; ++k) {
        out[k].x = lv_value_precise_t(cx + s[k].x * r);
        out[k].y = lv_value_precise_t(cy + s[k].y * r);
    }
    return n;
}

lv_color_t body_color(int die)
{
    const Palette& P = pal();
    switch (die) {
        case Coin: return P.lit;
        case D4:   return P.piece_a;
        case D6:   return P.cell;
        case D8:   return P.frame;
        case D10:  return P.felt;
        case D12:  return P.sq_dark;
        case D20:  return P.stone_dark;
        default:   return P.selected;            // d100
    }
}

const lv_font_t* number_font(int s)
{
    if (s >= 60) return &lv_font_montserrat_28;
    if (s >= 38) return &lv_font_montserrat_20;
    if (s >= 24) return &lv_font_montserrat_14;
    if (s >= 17) return &lv_font_montserrat_12;
    return &lv_font_montserrat_10;
}

// One die in an s x s box at (x, y), showing `text` (or pips on a d6)
void draw_one(lv_layer_t* layer, int die, int value, const char* text, int x, int y, int s)
{
    const Palette& P = pal();
    const float cx = x + s / 2.0f, cy = y + s / 2.0f, r = s / 2.0f - 1;
    const lv_color_t body = body_color(die), edge = P.ink;
    const int w = s >= 40 ? 2 : 1;
    lv_color_t ink = contrast_text(body);
    lv_point_precise_t pts[6];
    float ty = cy;                                        // where the number sits
    if (die == Coin) {
        kit::fill_circle(layer, int32_t(cx), int32_t(cy), int32_t(r), body);
        kit::ring(layer, int32_t(cx), int32_t(cy), int32_t(r), w, edge);
        kit::ring(layer, int32_t(cx), int32_t(cy), int32_t(r * 0.78f), 1, lv_color_darken(body, 60));
    } else if (die == D6) {
        kit::fill_rect(layer, x + 1, y + 1, x + s - 2, y + s - 2, edge, s / 5);
        kit::fill_rect(layer, x + 1 + w, y + 1 + w, x + s - 2 - w, y + s - 2 - w, body, s / 5 - 1);
        if (s >= 16 && value >= 1 && value <= 6) {        // pips, like a real die
            static const uint16_t kPips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};
            const int pr = s / 11 > 1 ? s / 11 : 1, step = s * 27 / 100;
            for (int k = 0; k < 9; ++k)
                if ((kPips[value] >> k) & 1)
                    kit::fill_circle(layer, int32_t(cx) + (k % 3 - 1) * step, int32_t(cy) + (k / 3 - 1) * step, pr, P.ink);
            return;
        }
        ink = P.ink;
    } else {
        const int n = shape(die, cx, cy, r, pts);
        fill_poly(layer, pts, n, body);
        if (die == D20) {                                  // the face toward us
            lv_point_precise_t f[3];
            const float fr = r * 0.62f;
            f[0] = {lv_value_precise_t(cx), lv_value_precise_t(cy - fr)};
            f[1] = {lv_value_precise_t(cx + fr * 0.87f), lv_value_precise_t(cy + fr * 0.5f)};
            f[2] = {lv_value_precise_t(cx - fr * 0.87f), lv_value_precise_t(cy + fr * 0.5f)};
            const lv_color_t face = lv_color_mix(lv_color_white(), body, 120);
            fill_poly(layer, f, 3, face);
            for (int k = 0; k < 3; ++k) {                  // edges out to the corners
                const lv_point_precise_t& a = f[k];
                const lv_point_precise_t& b = pts[k * 2];
                kit::line(layer, int32_t(a.x), int32_t(a.y), int32_t(b.x), int32_t(b.y), 1, edge);
            }
            outline(layer, f, 3, 1, edge);
            ink = contrast_text(face);
            ty = cy + r * 0.12f;
        } else if (die == D4) {
            ty = cy + r * 0.22f;
        } else if (die == D10 || die == D100) {
            ty = cy - r * 0.05f;
        } else if (die == D12) {
            ty = cy + r * 0.05f;
        }
        outline(layer, pts, n, w, edge);
    }
    const lv_font_t* f = number_font(s);
    const int lh = lv_font_get_line_height(f);
    kit::text(layer, text, f, ink, x, int32_t(ty) - lh / 2, s, lh);
}

// How many pictures a roll needs (a d100 is two d10s)
int pictures(const Rolled& r)
{
    int n = 0;
    for (int k = 0; k < r.n; ++k) n += r.die[k] == D100 ? 2 : 1;
    return n;
}

// The k-th picture of a roll: die type, value, label text. Returns false past the end.
bool picture(const Rolled& r, int k, int* die, int* value, char* text, size_t cap)
{
    for (int i = 0; i < r.n; ++i) {
        const int v = r.value[i];
        if (r.die[i] == D100) {
            if (k == 0) { *die = D100; *value = v; snprintf(text, cap, "%02d", v == 100 ? 0 : v / 10 * 10); return true; }
            if (k == 1) { *die = D10; *value = v; snprintf(text, cap, "%d", v % 10); return true; }
            k -= 2;
        } else {
            if (k == 0) {
                *die = r.die[i];
                *value = v;
                if (r.die[i] == Coin) snprintf(text, cap, "%s", v == 2 ? "H" : "T");
                else snprintf(text, cap, "%d", v);
                return true;
            }
            --k;
        }
    }
    return false;
}

// Natural 20 / natural 1 on a d20: a coloured ring behind it
void crit_mark(lv_layer_t* layer, const Rolled& r, int k, int x, int y, int s)
{
    int die, value;
    char t[8];
    if (!picture(r, k, &die, &value, t, sizeof t) || die != D20 || (value != 20 && value != 1)) return;
    const Palette& P = pal();
    kit::fill_circle(layer, x + s / 2, y + s / 2, s / 2 + (s >= 30 ? 4 : 2), value == 20 ? P.lit : P.conflict);
}

// Lays `n` dice out in a w x h box (rows of up to `cols`), as large as fit up to `cap`
int fit_size(int n, int w, int h, int gap, int cap, int* cols_out)
{
    int best = 8, best_cols = n;
    for (int s = cap; s >= 8; --s) {
        const int cols = (w + gap) / (s + gap);
        if (cols < 1) continue;
        const int rows = (n + cols - 1) / cols;
        if (rows * (s + gap) - gap <= h) { best = s; best_cols = cols; break; }
    }
    // Even rows: 6 dice as 3 + 3, not 5 + 1
    if (best_cols > n) best_cols = n;
    const int rows = (n + best_cols - 1) / best_cols;
    *cols_out = (n + rows - 1) / rows;
    return best;
}

void draw_dice_row(lv_layer_t* layer, const Rolled& r, int x, int y, int w, int h, int cap, bool centre)
{
    const int n = pictures(r);
    if (!n) return;
    const int gap = h >= 40 ? 6 : 4;
    int cols;
    const int s = fit_size(n, w, h, gap, cap, &cols);
    const int rows = (n + cols - 1) / cols;
    const int total_h = rows * (s + gap) - gap;
    int k = 0;
    for (int row = 0; row < rows; ++row) {
        const int in_row = n - k < cols ? n - k : cols;
        const int row_w = in_row * (s + gap) - gap;
        int dx = centre ? x + (w - row_w) / 2 : x;
        const int dy = y + (h - total_h) / 2 + row * (s + gap);
        for (int c = 0; c < in_row; ++c, ++k) {
            int die, value;
            char t[8];
            if (!picture(r, k, &die, &value, t, sizeof t)) return;
            crit_mark(layer, r, k, dx, dy, s);
            draw_one(layer, die, value, t, dx, dy, s);
            dx += s + gap;
        }
    }
}

void tray_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Metrics& M = metrics();
    const int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), M.large ? 8 : 6);
    if (!S->shown) {
        kit::text(layer, "Tap dice below, then Roll", &lv_font_montserrat_14, contrast_text(cards::felt()),
                  a.x1, a.y1 + h / 2 - 9, w, 18);
        return;
    }
    const lv_color_t on_felt = contrast_text(cards::felt());
    const int pad = M.large ? 8 : 5;
    const lv_font_t* big = M.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    if (S->shown == 1 && S->preset < 0) {
        // One pool: the dice large, then "+3" and the total
        const Rolled& r = S->last[0];
        const int th = lv_font_get_line_height(big) + 2;
        draw_dice_row(layer, r, a.x1 + pad, a.y1 + pad, w - 2 * pad, h - 2 * pad - th, M.large ? 76 : 56, true);
        char tot[24], mod[12] = "";
        const bool coin_only = r.n == 1 && r.die[0] == Coin && !r.mod;
        if (coin_only) snprintf(tot, sizeof tot, "%s", r.value[0] == 2 ? "Heads" : "Tails");
        else snprintf(tot, sizeof tot, "Total %d", int(r.total));
        if (r.mod) snprintf(mod, sizeof mod, "%+d", int(r.mod));
        const int ty = a.y2 - pad - lv_font_get_line_height(big);
        const int tw = text_width(tot, big);
        kit::text(layer, tot, big, on_felt, a.x2 - pad - tw, ty, tw, lv_font_get_line_height(big));
        if (mod[0]) kit::text(layer, mod, big, on_felt, a.x1 + pad, ty, text_width(mod, big), lv_font_get_line_height(big));
        (void)P;
        return;
    }
    // A preset: one row per line - label, dice, total
    const lv_font_t* f = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int rows = S->shown;
    const int rh = (h - 2 * pad) / rows;
    const int lw = text_width("Damage", f) + 4;
    const int vw = text_width("-999", big) + 4;
    for (int l = 0; l < rows; ++l) {
        const Rolled& r = S->last[l];
        const int y = a.y1 + pad + l * rh;
        const int lh = lv_font_get_line_height(f);
        kit::text(layer, label_name(r.label), f, on_felt, a.x1 + pad, y + (rh - lh) / 2, lw, lh);
        char v[16];
        snprintf(v, sizeof v, "%d", int(r.total));
        const int bh = lv_font_get_line_height(big);
        const int vtw = text_width(v, big);
        kit::text(layer, v, big, on_felt, a.x2 - pad - vtw, y + (rh - bh) / 2, vtw, bh);
        // dice (and the modifier) between them
        char mod[8] = "";
        if (r.mod) snprintf(mod, sizeof mod, "%+d", int(r.mod));
        const int mw = mod[0] ? text_width(mod, f) + 6 : 0;
        const int dx = a.x1 + pad + lw + 4, dw = w - 2 * pad - lw - 4 - vw - mw;
        draw_dice_row(layer, r, dx, y + 1, dw, rh - 2, M.large ? 48 : 34, false);
        if (mod[0]) {
            // right after the dice
            int cols;
            const int n = pictures(r);
            const int gap = rh - 2 >= 40 ? 6 : 4;
            const int s = fit_size(n, dw, rh - 2, gap, M.large ? 48 : 34, &cols);
            const int used = (n < cols ? n : cols) * (s + gap) - gap;
            kit::text(layer, mod, f, on_felt, dx + (n ? used : 0) + 6, y + (rh - lh) / 2, mw, lh);
        }
        if (l + 1 < rows)
            kit::fill_rect(layer, a.x1 + pad, y + rh - 1, a.x2 - pad, y + rh - 1, lv_color_mix(on_felt, cards::felt(), 60));
    }
}

// ---- Main screen ------------------------------------------------------------------------
void update_pool()
{
    if (!pool_l) return;
    char t[64];
    if (S->preset >= 0) snprintf(t, sizeof t, "%s", S->presets[S->preset].name[0] ? S->presets[S->preset].name : "Preset");
    else if (S->pool.empty() && !S->pool.mod) snprintf(t, sizeof t, "Tap dice to build a roll");
    else S->pool.format(t, sizeof t);
    lv_label_set_text(pool_l, t);
}

void after_roll()
{
    // A natural 20 earns a trill, a natural 1 the "aww"; otherwise the dice clatter
    bool nat20 = false, nat1 = false;
    for (int l = 0; l < S->shown; ++l)
        for (int k = 0; k < S->last[l].n; ++k)
            if (S->last[l].die[k] == D20) { nat20 |= S->last[l].value[k] == 20; nat1 |= S->last[l].value[k] == 1; }
    sound(nat20 ? Sound::Trill : nat1 ? Sound::Error : Sound::Move);
    fresh = true;
    dirty = true;
    lv_obj_invalidate(tray_obj);
    update_pool();
}

Rng new_rng() { return Rng(shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u); }

void roll_now()
{
    if (S->preset >= 0) {
        Rng rng = new_rng();
        S->roll_preset(S->preset, rng);
    } else {
        if (S->pool.empty()) { sound(Sound::Error); return; }
        Rng rng = new_rng();
        S->roll_pool(rng);
    }
    after_roll();
}

enum Key : intptr_t { kMinus = 100, kPlus, kClear, kRoll, kPresetsKey, kHistoryKey };

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (id < kDieTypes) {
        if (fresh || S->preset >= 0) { S->pool.clear(); S->preset = -1; fresh = false; }
        if (!S->pool.add(id)) sound(Sound::Error);
    } else switch (id) {
        case kMinus: case kPlus:
            if (S->preset >= 0) { S->pool.clear(); S->preset = -1; }
            fresh = false;
            S->pool.bump_mod(id == kPlus ? 1 : -1);
            break;
        case kClear:  S->pool.clear(); S->preset = -1; fresh = false; break;
        case kRoll:   roll_now(); return;
        case kPresetsKey: open_presets(); return;
        case kHistoryKey: open_history(); return;
    }
    dirty = true;
    update_pool();
}

lv_obj_t* row_of(lv_obj_t* scr, int x, int y, int w, int h)
{
    lv_obj_t* r = lv_obj_create(scr);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w, h);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, metrics().large ? 6 : 4, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

lv_obj_t* add_key(lv_obj_t* row, const char* label, intptr_t id, int grow = 1, lv_event_cb_t cb = key_cb)
{
    lv_obj_t* k = make_key(row, 10, menu_btn_h(), cb, id);
    lv_obj_set_flex_grow(k, grow);
    key_label(k, label, menu_font());
    return k;
}

// Die keys in two rows of four: d4 d6 d8 d10 / d12 d20 d100 Coin
const int kKeyOrder[8] = {D4, D6, D8, D10, D12, D20, D100, Coin};

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    lv_label_set_text(bar.left, "");
    kit::top_bar_status(bar, "RPG Dice");
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 6 : 4, gap = m.large ? 6 : 4;
    const int kh = menu_btn_h();
    const int pool_h = lv_font_get_line_height(menu_font()) + 2;
    const int keys_h = 4 * kh + 3 * gap;
    const int y0 = bar.h + (m.large ? 4 : 2);
    const int tray_h = m.h - pad - keys_h - gap - pool_h - gap - y0;
    tray_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(tray_obj);
    lv_obj_set_size(tray_obj, m.w - 2 * pad, tray_h);
    lv_obj_set_pos(tray_obj, pad, y0);
    lv_obj_add_event_cb(tray_obj, tray_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    int y = y0 + tray_h + gap;
    pool_l = lv_label_create(scr);
    lv_label_set_long_mode(pool_l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_size(pool_l, m.w - 2 * pad, pool_h);
    lv_obj_set_pos(pool_l, pad, y);
    lv_obj_set_style_text_font(pool_l, menu_font(), 0);
    lv_obj_set_style_text_color(pool_l, pal().ink, 0);
    lv_obj_set_style_text_align(pool_l, LV_TEXT_ALIGN_CENTER, 0);
    y += pool_h + gap;
    for (int r = 0; r < 2; ++r) {
        lv_obj_t* row = row_of(scr, pad, y, m.w - 2 * pad, kh);
        for (int c = 0; c < 4; ++c) add_key(row, die_name(kKeyOrder[r * 4 + c]), kKeyOrder[r * 4 + c]);
        y += kh + gap;
    }
    lv_obj_t* row = row_of(scr, pad, y, m.w - 2 * pad, kh);
    add_key(row, "-1", kMinus);
    add_key(row, "+1", kPlus);
    add_key(row, "Clear", kClear);
    lv_obj_t* roll = add_key(row, "Roll", kRoll, 2);
    lv_obj_add_state(roll, LV_STATE_CHECKED);
    y += kh + gap;
    row = row_of(scr, pad, y, m.w - 2 * pad, kh);
    add_key(row, "Presets", kPresetsKey);
    add_key(row, "History", kHistoryKey);
    update_pool();
}

// ---- Presets ------------------------------------------------------------------------------
bool edit_mode = false;
int  edit_slot = 0, edit_line_no = 0;

void preset_cb(lv_event_t* e)
{
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (id == 100) { edit_mode = !edit_mode; open_presets(); return; }
    if (id == 101) { close_overlays(); return; }
    if (id < 0 || id >= kPresets) return;
    if (edit_mode || !S->presets[id].lines) { edit_preset(id); return; }
    close_overlays();
    Rng rng = new_rng();
    S->roll_preset(id, rng);
    after_roll();
}

void open_presets()
{
    overlay_begin(edit_mode ? "Edit Presets" : "Presets");
    const int kh = menu_btn_h();
    for (int r = 0; r < kPresets / 2; ++r) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), kh);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        lv_obj_set_scrollable(row, false);
        for (int c = 0; c < 2; ++c) {
            const int slot = r * 2 + c;
            const Preset& p = S->presets[slot];
            lv_obj_t* k = make_key(row, 10, kh, preset_cb, slot);
            lv_obj_set_flex_grow(k, 1);
            // A long name gets a smaller font, then is cut at the key's edge
            const char* name = p.lines ? (p.name[0] ? p.name : "Preset") : "+ New";
            const Metrics& M = metrics();
            const int room = (M.w - 2 * (M.large ? 16 : 10) - 6) / 2 - 8;
            const lv_font_t* f = menu_font();
            if (text_width(name, f) > room) f = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
            lv_obj_t* l = key_label(k, name, f);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
            lv_obj_set_width(l, room);
            lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(l);
            if (!p.lines) set_dim(k, true);
        }
    }
    overlay_text(edit_mode ? "Tap a preset to change it." : "Tap a preset to roll it all at once.", true);
    lv_obj_t* row = lv_obj_create(overlay());
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), kh);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_ignore_layout(row, true);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* ed = make_key(row, 10, kh, preset_cb, 100);
    lv_obj_set_flex_grow(ed, 1);
    key_label(ed, edit_mode ? "Done Editing" : "Edit Presets", menu_font());
    lv_obj_t* back = make_key(row, 10, kh, preset_cb, 101);
    lv_obj_set_flex_grow(back, 1);
    lv_obj_add_state(back, LV_STATE_CHECKED);
    key_label(back, "Back", menu_font());
}

// Preset editor: name, its lines, Add Line, [Delete | Done]
void editor_cb(lv_event_t* e);
void open_keyboard();

void edit_preset(int slot)
{
    edit_slot = slot;
    Preset& p = S->presets[slot];
    if (!p.lines) {                         // a new preset starts with one Hit d20 line
        p = Preset{};
        snprintf(p.name, sizeof p.name, "Preset %d", (slot + 1) % 100);
        p.lines = 1;
        p.pool[0].count[D20] = 1;
        p.pool[0].label = 1;
        dirty = true;
    }
    char t[40];
    snprintf(t, sizeof t, "Preset %d", slot + 1);
    overlay_begin(t);
    char nm[48];
    snprintf(nm, sizeof nm, "Name: %s", p.name);
    lv_obj_t* nk = overlay_button(overlay(), nm, editor_cb, 50);
    (void)nk;
    for (int l = 0; l < p.lines; ++l) {
        char pt[48], line[64];
        p.pool[l].format(pt, sizeof pt);
        snprintf(line, sizeof line, "%s: %s", label_name(p.pool[l].label), pt);
        overlay_button(overlay(), line, editor_cb, l);
    }
    if (p.lines < kPresetLines) overlay_button(overlay(), "Add Line", editor_cb, 40);
    lv_obj_t* row = lv_obj_create(overlay());
    lv_obj_remove_style_all(row);
    const int kh = menu_btn_h();
    lv_obj_set_size(row, lv_pct(100), kh);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_ignore_layout(row, true);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* del = make_key(row, 10, kh, editor_cb, 41);
    lv_obj_set_flex_grow(del, 1);
    key_label(del, "Delete", menu_font());
    lv_obj_t* done = make_key(row, 10, kh, editor_cb, 42);
    lv_obj_set_flex_grow(done, 1);
    lv_obj_add_state(done, LV_STATE_CHECKED);
    key_label(done, "Done", menu_font());
}

void editor_cb(lv_event_t* e)
{
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    Preset& p = S->presets[edit_slot];
    dirty = true;
    if (id < kPresetLines) { edit_line(edit_slot, id); return; }
    switch (id) {
        case 40:                                          // Add Line: a damage line after a hit
            if (p.lines < kPresetLines) {
                Pool& q = p.pool[p.lines];
                q.clear();
                const bool after_hit = p.pool[p.lines - 1].label == 1;
                q.count[after_hit ? D8 : D20] = 1;
                q.label = after_hit ? 2 : 1;
                ++p.lines;
                edit_line(edit_slot, p.lines - 1);
            }
            return;
        case 41:                                          // Delete (no confirmation - Tom's rule)
            p = Preset{};
            if (S->preset == edit_slot) S->preset = -1;
            update_pool();
            open_presets();
            return;
        case 42: open_presets(); return;
        case 50: open_keyboard(); return;
    }
}

// Line editor: label, dice, modifier
lv_obj_t* line_title = nullptr;

void line_refresh()
{
    if (!line_title) return;
    const Pool& q = S->presets[edit_slot].pool[edit_line_no];
    char pt[48], t[64];
    q.format(pt, sizeof pt);
    snprintf(t, sizeof t, "%s: %s", label_name(q.label), pt);
    lv_label_set_text(line_title, t);
}

void line_cb(lv_event_t* e)
{
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    Preset& p = S->presets[edit_slot];
    Pool& q = p.pool[edit_line_no];
    dirty = true;
    if (id < kDieTypes) { if (!q.add(id)) sound(Sound::Error); line_refresh(); return; }
    switch (id) {
        case kMinus: q.bump_mod(-1); break;
        case kPlus:  q.bump_mod(1); break;
        case kClear: { const uint8_t lab = q.label; q.clear(); q.label = lab; break; }
        case 60:     q.label = uint8_t((q.label + 1) % kLabels); edit_line(edit_slot, edit_line_no); return;
        case 61:                                          // Remove Line
            if (p.lines > 1) {
                for (int l = edit_line_no; l + 1 < p.lines; ++l) p.pool[l] = p.pool[l + 1];
                --p.lines;
            } else {
                p = Preset{};                             // the last line: the preset goes
                if (S->preset == edit_slot) S->preset = -1;
                open_presets();
                return;
            }
            edit_preset(edit_slot);
            return;
        case 62: edit_preset(edit_slot); return;
    }
    line_refresh();
}

void edit_line(int slot, int line)
{
    edit_slot = slot;
    edit_line_no = line;
    char t[32];
    snprintf(t, sizeof t, "Line %d", line + 1);
    overlay_begin(t, [] { line_title = nullptr; });
    line_title = lv_label_create(overlay());
    lv_obj_set_width(line_title, lv_pct(100));
    lv_label_set_long_mode(line_title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(line_title, metrics().large ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(line_title, pal().ink, 0);
    line_refresh();
    const Pool& q = S->presets[slot].pool[line];
    char lab[32];
    snprintf(lab, sizeof lab, "Label: %s", label_name(q.label));
    overlay_button(overlay(), lab, line_cb, 60);
    const int kh = menu_btn_h();
    auto mk_row = [&]() {
        lv_obj_t* r = lv_obj_create(overlay());
        lv_obj_remove_style_all(r);
        lv_obj_set_size(r, lv_pct(100), kh);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(r, 4, 0);
        lv_obj_set_scrollable(r, false);
        return r;
    };
    for (int r = 0; r < 2; ++r) {
        lv_obj_t* row = mk_row();
        for (int c = 0; c < 4; ++c) add_key(row, die_name(kKeyOrder[r * 4 + c]), kKeyOrder[r * 4 + c], 1, line_cb);
    }
    lv_obj_t* row = mk_row();
    add_key(row, "-1", kMinus, 1, line_cb);
    add_key(row, "+1", kPlus, 1, line_cb);
    add_key(row, "Clear Dice", kClear, 2, line_cb);
    lv_obj_t* bot = lv_obj_create(overlay());
    lv_obj_remove_style_all(bot);
    lv_obj_set_size(bot, lv_pct(100), kh);
    lv_obj_set_flex_flow(bot, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bot, 6, 0);
    lv_obj_set_ignore_layout(bot, true);
    lv_obj_align(bot, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* rm = make_key(bot, 10, kh, line_cb, 61);
    lv_obj_set_flex_grow(rm, 1);
    key_label(rm, "Remove Line", menu_font());
    lv_obj_t* done = make_key(bot, 10, kh, line_cb, 62);
    lv_obj_set_flex_grow(done, 1);
    lv_obj_add_state(done, LV_STATE_CHECKED);
    key_label(done, "Done", menu_font());
}

// ---- Name keyboard: letters (each word starts with a capital), digits page ----------------
char     kb_text[kNameLen];
bool     kb_digits = false;
lv_obj_t* kb_label = nullptr;

void kb_show()
{
    if (!kb_label) return;
    char t[kNameLen + 4];
    snprintf(t, sizeof t, "%s_", kb_text);
    lv_label_set_text(kb_label, t);
}

void kb_cb(lv_event_t* e)
{
    const int c = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    size_t n = strlen(kb_text);
    if (c == 1) { if (n) kb_text[n - 1] = 0; kb_show(); return; }          // backspace
    if (c == 2) { kb_digits = !kb_digits; open_keyboard(); return; }       // ABC / 123
    if (c == 3) {                                                          // Done
        Preset& p = S->presets[edit_slot];
        // trim spaces at the ends
        while (n && kb_text[n - 1] == ' ') kb_text[--n] = 0;
        const char* s = kb_text;
        while (*s == ' ') ++s;
        if (*s) snprintf(p.name, sizeof p.name, "%s", s);
        dirty = true;
        update_pool();
        edit_preset(edit_slot);
        return;
    }
    if (n + 1 >= sizeof kb_text) { sound(Sound::Error); return; }
    char ch = char(c);
    // Auto capitals: the first letter of each word
    if (ch >= 'a' && ch <= 'z' && (n == 0 || kb_text[n - 1] == ' ')) ch = char(ch - 'a' + 'A');
    kb_text[n] = ch;
    kb_text[n + 1] = 0;
    kb_show();
}

void open_keyboard()
{
    if (!kb_label) {                       // coming from the editor: start from the name
        snprintf(kb_text, sizeof kb_text, "%s", S->presets[edit_slot].name);
    }
    overlay_begin("Preset Name", [] { kb_label = nullptr; });
    kb_label = lv_label_create(overlay());
    lv_obj_set_width(kb_label, lv_pct(100));
    lv_obj_set_style_text_font(kb_label, metrics().large ? &lv_font_montserrat_28 : &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(kb_label, pal().ink, 0);
    lv_obj_set_style_bg_color(kb_label, pal().cell, 0);
    lv_obj_set_style_bg_opa(kb_label, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(kb_label, 4, 0);
    kb_show();
    static const char* const kLetters = "abcdefghijklmnopqrstuvwxyz'-";
    static const char* const kDigits = "1234567890+-'#&.";
    const char* keys = kb_digits ? kDigits : kLetters;
    const int n = int(strlen(keys)), per = 6;
    const int kh = menu_btn_h();
    for (int i = 0; i < n; i += per) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), kh);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_scrollable(row, false);
        for (int k = i; k < i + per; ++k) {
            if (k < n) {
                char lab[2] = {keys[k] >= 'a' && keys[k] <= 'z' ? char(keys[k] - 'a' + 'A') : keys[k], 0};
                lv_obj_t* b = make_key(row, 10, kh, kb_cb, intptr_t(keys[k]));
                lv_obj_set_flex_grow(b, 1);
                key_label(b, lab, menu_font());
            } else if (k == n) {
                lv_obj_t* b = make_key(row, 10, kh, kb_cb, 1);
                lv_obj_set_flex_grow(b, per - (n % per) == 0 ? 1 : per - (n % per));
                key_label(b, LV_SYMBOL_BACKSPACE, menu_font());
                break;
            }
        }
        if (n % per == 0 && i + per >= n) {                // full last row: backspace on its own
            lv_obj_t* r2 = lv_obj_create(overlay());
            lv_obj_remove_style_all(r2);
            lv_obj_set_size(r2, lv_pct(100), kh);
            lv_obj_t* b = make_key(r2, lv_pct(100), kh, kb_cb, 1);
            key_label(b, LV_SYMBOL_BACKSPACE, menu_font());
        }
    }
    lv_obj_t* row = lv_obj_create(overlay());
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), kh);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_ignore_layout(row, true);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* tg = make_key(row, 10, kh, kb_cb, 2);
    lv_obj_set_flex_grow(tg, 1);
    key_label(tg, kb_digits ? "ABC" : "123", menu_font());
    lv_obj_t* sp = make_key(row, 10, kh, kb_cb, ' ');
    lv_obj_set_flex_grow(sp, 2);
    key_label(sp, "Space", menu_font());
    lv_obj_t* dn = make_key(row, 10, kh, kb_cb, 3);
    lv_obj_set_flex_grow(dn, 1);
    lv_obj_add_state(dn, LV_STATE_CHECKED);
    key_label(dn, "Done", menu_font());
}

// ---- History: newest first, as many as fit per page --------------------------------------
int hist_page = 0;

void hist_cb(lv_event_t* e)
{
    const int id = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (id == 1) { open_history(hist_page + 1); return; }
    if (id == -1) { open_history(hist_page - 1); return; }
    if (id == 2) { S->clear_history(); dirty = true; open_history(0); return; }
    close_overlays();
}

void open_history(int page)
{
    overlay_begin("History");
    const Metrics& M = metrics();
    const lv_font_t* f = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int kh = menu_btn_h(), gap = M.large ? 10 : 6;
    const int width = M.w - 2 * (M.large ? 16 : 10);
    const int avail = M.h - 2 * (M.large ? 16 : 10) - lv_font_get_line_height(title_font()) - gap
                    - 2 * kh - 2 * gap;
    // Page starts: entries that fit one after another
    int starts[kHistory + 1], pages = 0, used = avail + 1;
    for (int k = 0; S->history(k); ++k) {
        lv_point_t sz;
        lv_text_get_size(&sz, S->history(k), f, 0, 0, width, LV_TEXT_FLAG_NONE);
        const int hgt = sz.y + 4;
        if (used + hgt > avail) { starts[pages++] = k; used = 0; }
        used += hgt;
    }
    starts[pages] = S->hist_n;
    if (page >= pages) page = pages - 1;
    if (page < 0) page = 0;
    hist_page = page;
    if (!pages) {
        overlay_text("No rolls yet.", true);
    } else {
        for (int k = starts[page]; k < starts[page + 1]; ++k) {
            lv_obj_t* l = lv_label_create(overlay());
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
            lv_obj_set_width(l, lv_pct(100));
            lv_label_set_text(l, S->history(k));
            lv_obj_set_style_text_font(l, f, 0);
            lv_obj_set_style_text_color(l, k == 0 ? pal().ink : pal().muted, 0);
        }
    }
    // [Clear History] above [<] [Back] [>]
    lv_obj_t* nav = lv_obj_create(overlay());
    lv_obj_remove_style_all(nav);
    lv_obj_set_size(nav, lv_pct(100), kh);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(nav, 6, 0);
    lv_obj_set_ignore_layout(nav, true);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    auto nav_key = [&](lv_obj_t* r, const char* t, int id, bool grow, bool on) {
        lv_obj_t* b = make_key(r, M.large ? 56 : 48, kh, on ? hist_cb : nullptr, id);
        if (grow) lv_obj_set_flex_grow(b, 1);
        key_label(b, t, menu_font());
        if (!on) set_dim(b, true);
        return b;
    };
    nav_key(nav, LV_SYMBOL_LEFT, -1, false, page > 0);
    lv_obj_t* back = nav_key(nav, "Back", 0, true, true);
    lv_obj_add_state(back, LV_STATE_CHECKED);
    nav_key(nav, LV_SYMBOL_RIGHT, 1, false, page + 1 < pages);
    lv_obj_t* r2 = lv_obj_create(overlay());
    lv_obj_remove_style_all(r2);
    lv_obj_set_size(r2, lv_pct(100), kh);
    lv_obj_set_ignore_layout(r2, true);
    lv_obj_align(r2, LV_ALIGN_BOTTOM_MID, 0, -(kh + gap));
    lv_obj_t* cl = make_key(r2, lv_pct(100), kh, S->hist_n ? hist_cb : nullptr, 2);
    key_label(cl, "Clear History", menu_font());
    if (!S->hist_n) set_dim(cl, true);
}

// ---- Menu -------------------------------------------------------------------------------------
void menu_cb(lv_event_t* e)
{
    switch (reinterpret_cast<intptr_t>(lv_event_get_user_data(e))) {
        case 1: open_presets(); break;
        case 2: open_history(); break;
        case kit::kHowToPlay: kit::how_to_play(open_menu); break;
        case kit::kSettings: settings_open(open_menu); break;
        case kit::kExitMenu: close_overlays(); break;
        case kit::kExitGame: close_overlays(); app_go_home(); break;
    }
}

void open_menu()
{
    overlay_begin("RPG Dice");
    overlay_pair("Presets", menu_cb, 1, "History", menu_cb, 2);
    how_to_play_key(menu_cb, kit::kHowToPlay);
    overlay_button(overlay(), "Settings", menu_cb, kit::kSettings);
    overlay_exit_row(menu_cb, kit::kExitMenu, kit::kExitGame);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; sample_preset(*S); dirty = true; }
    fresh = true;
    edit_mode = false;
    build();
}

void close()
{
    if (!S) return;
    save();
    bar = kit::TopBar{};
    tray_obj = pool_l = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    if (dirty && now - last_save_ms > 15000) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State* st = S;
    State* tmp = nullptr;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    const char* h = st->history(0);
    if (h) snprintf(buf, cap, "%s", h);
    else snprintf(buf, cap, "No rolls yet");
    delete tmp;
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), d20 = size * 70 / 100, d6 = size * 42 / 100;
    draw_one(layer, D20, 20, "20", a.x1, a.y1, d20);
    draw_one(layer, D6, 5, "5", a.x1 + size - d6, a.y1 + size - d6, d6);
}

void icon(lv_obj_t* parent, int size)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_center(o);
    lv_obj_set_clickable(o, false);
    lv_obj_add_event_cb(o, icon_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
}

} // namespace

namespace games {
extern const GameOps rpgdice_ops;
const GameOps rpgdice_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
// Preview staging: roll a pool / a preset with a fixed seed, open screens
namespace rpgdice_preview {
void set_pool(const int* counts, int mod)
{
    if (!S) return;
    S->pool.clear();
    for (int d = 0; d < kDieTypes; ++d) S->pool.count[d] = uint8_t(counts[d]);
    S->pool.mod = int8_t(mod);
    S->preset = -1;
    update_pool();
}
void roll_pool(uint32_t seed) { if (!S) return; Rng r(seed); S->roll_pool(r); after_roll(); }
void roll_preset(int i, uint32_t seed) { if (!S) return; Rng r(seed); S->roll_preset(i, r); after_roll(); }
void presets() { open_presets(); }
void history() { open_history(); }
void editor(int slot) { edit_preset(slot); }
void line_editor(int slot, int line) { edit_line(slot, line); }
void keyboard(int slot) { edit_slot = slot; close_overlays(); open_keyboard(); }
} // namespace rpgdice_preview
#endif
