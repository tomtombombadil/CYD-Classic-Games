// Escape from CYD: registry entry, save file and screen. Rules and the
// computer players in escape_core.*.
//
// Screen: the hex sea (9 x 11) filling the width, the island in the
// middle (beach sand, forest green, mountain brown), a palm-tree safe
// island in each corner, boats, swimmers, sharks, whales and sea serpents.
// Two lines under it: what to do (or what just happened), and how many
// explorers each colour has saved. One key: Place For Me / Done Moving /
// Skip / Play Again.
//
// Your explorers show their value; the others' are hidden until the end.
// Moving: tap a hex with your piece in it - your boat first if you may
// move it, then your explorers (best first); tap the same hex again for
// the next one. Framed hexes are where it can go: tap one. Sinking: the
// tiles you may sink are framed. Creatures: tap one of the kind rolled,
// then a framed hex.
//
// You are Red against Max (Blue), Zoe (Yellow) and Ada (Green), or 2-4
// people pass and play. Sounds: your step (Place), theirs (Turn), a tile
// sinks (Move), a creature or whirlpool takes explorers ("aww" when one is
// yours), a boat (Hint), the Volcano (Boom), the end (Win / Lose).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "escape_core.h"
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace escape;
using namespace ui;

constexpr const char* kId = "escape";
const char* const kNames[kColors] = {"You", "Max", "Zoe", "Ada"};
const char* const kColorNames[kColors] = {"Red", "Blue", "Yellow", "Green"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};
constexpr uint32_t kPlaceMs = 120, kStepMs = 450, kSinkMs = 650, kBannerMs = 1300, kRollMs = 800;

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint8_t  level = 1;
    uint8_t  people = 1;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 7;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;
lv_obj_t*   key = nullptr;
float       R = 13.0f, W = 22.5f;
int         bx = 0, by = 0, info_y = 0, lh = 14;
uint32_t    now_ms = 0, wait_until = 0, banner_until = 0, last_save_ms = 0;
bool        frozen = false;
lv_point_t  press_pt{0, 0};
int8_t      sel_kind = 0;       // 0 none, 1 explorer, 2 boat, 3 creature
int8_t      sel = -1;
int8_t      sel_hex = -1;
char        news[64] = "";
bool        banner = false;
bool        roll_shown = false;          // a computer's creature roll has been on the info line

void open_menu();
void update();

bool human(int c) { return S && c < S->people; }
bool pnp() { return S && S->people > 1; }
const char* name(int c)
{
    if (c < 0 || c >= kColors) return "";
    return pnp() && human(c) ? kColorNames[c] : kNames[c];
}

lv_color_t color_of(int c)
{
    const Palette& P = pal();
    switch (c) {
        case 0:  return P.piece_a;
        case 1:  return P.frame;
        case 2:  return P.piece_b;
        default: return P.win;
    }
}

// ---- Save / stats -----------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    const size_t n = S->g.serialize(buf, kSaveBytes);
    buf[n] = S->recorded;
    buf[n + 1] = S->level;
    for (int k = 0; k < 4; ++k) buf[n + 2 + k] = uint8_t(S->seconds >> (8 * k));
    buf[n + 6] = S->people;
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) {
        const size_t b = Game::kSaveBytes;
        st.recorded = buf[b] ? 1 : 0;
        st.level = buf[b + 1] < 3 ? buf[b + 1] : 1;
        st.seconds = 0;
        for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[b + 2 + k]) << (8 * k);
        st.people = buf[b + 6] >= 1 && buf[b + 6] <= kColors ? buf[b + 6] : 1;
    }
    delete[] buf;
    return ok;
}

void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

int my_place()
{
    int place = 1;
    for (int c = 1; c < kColors; ++c) place += S->g.score(c) > S->g.score(0);
    return place;
}

void record()
{
    Record r;
    r.place = uint8_t(my_place());
    r.saved = uint8_t(S->g.score(0));
    r.level = S->level;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

bool recordable() { return S && S->people == 1; }

// ---- Geometry ---------------------------------------------------------------------------------
void centre(int h, float* x, float* y)
{
    const int c = col_of(h), r = row_of(h);
    *x = float(bx) + W * (float(c) + 0.5f * float(r & 1) + 0.5f);
    *y = float(by) + R + 1.5f * R * float(r);
}

void centre_px(const lv_area_t& a, int h, int* x, int* y)
{
    float fx, fy;
    centre(h, &fx, &fy);
    *x = a.x1 + int(fx + 0.5f);
    *y = a.y1 + int(fy + 0.5f);
}

int hex_from_point(const lv_area_t& a, int px, int py)
{
    int best = -1;
    float bd = 1e9f;
    for (int h = 0; h < kHexes; ++h) {
        int x, y;
        centre_px(a, h, &x, &y);
        const float d = float((x - px) * (x - px) + (y - py) * (y - py));
        if (d < bd) { bd = d; best = h; }
    }
    return bd < R * R * 1.1f ? best : -1;
}

const float kCos[6] = {0.8660f, 0.0f, -0.8660f, -0.8660f, 0.0f, 0.8660f};
const float kSin[6] = {-0.5f, -1.0f, -0.5f, 0.5f, 1.0f, 0.5f};

void fill_hex(lv_layer_t* layer, int x, int y, float r, lv_color_t col)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = col;
    d.opa = LV_OPA_COVER;
    for (int k = 0; k < 6; ++k) {
        const int j = (k + 1) % 6;
        d.p[0].x = x; d.p[0].y = y;
        d.p[1].x = x + int(lroundf(kCos[k] * r)); d.p[1].y = y + int(lroundf(kSin[k] * r));
        d.p[2].x = x + int(lroundf(kCos[j] * r)); d.p[2].y = y + int(lroundf(kSin[j] * r));
        lv_draw_triangle(layer, &d);
    }
}

void hex_outline(lv_layer_t* layer, int x, int y, float r, int w, lv_color_t col)
{
    for (int k = 0; k < 6; ++k) {
        const int j = (k + 1) % 6;
        kit::line(layer, x + int(lroundf(kCos[k] * r)), y + int(lroundf(kSin[k] * r)),
                  x + int(lroundf(kCos[j] * r)), y + int(lroundf(kSin[j] * r)), w, col);
    }
}

// ---- What can be tapped -----------------------------------------------------------------------
constexpr int kMaxActs = 200;
Action acts[kMaxActs];
int    acts_n = 0;
bool   waiting = false;

bool my_turn() { return S && S->g.phase != Phase::Over && human(S->g.turn) && !waiting && !banner; }
void refresh() { acts_n = my_turn() ? S->g.actions(acts, kMaxActs) : 0; }

bool is_target(int h)
{
    for (int i = 0; i < acts_n; ++i) {
        const Action& a = acts[i];
        if (a.to != h) continue;
        if (a.act == kPlace || a.act == kSinkTile) return true;
        if (a.act == kStepExplorer && sel_kind == 1 && a.who == sel) return true;
        if (a.act == kStepBoat && sel_kind == 2 && a.who == sel) return true;
        if (a.act == kMoveCreature && sel_kind == 3 && a.who == sel) return true;
    }
    return false;
}

bool has_moves(Act act, int who)
{
    for (int i = 0; i < acts_n; ++i) if (acts[i].act == act && acts[i].who == who) return true;
    return false;
}

void clear_sel() { sel_kind = 0; sel = -1; sel_hex = -1; }

// ---- Drawing ----------------------------------------------------------------------------------
lv_color_t sea()       { return lv_color_mix(pal().frame, pal().cell, 120); }
lv_color_t sea_line()  { return lv_color_mix(pal().frame, pal().cell, 160); }

void draw_explorer(lv_layer_t* layer, int x, int y, int r, const Explorer& e, bool show, bool ring)
{
    const Palette& P = pal();
    if (ring) kit::fill_circle(layer, x, y, r + 3, P.lit);
    kit::fill_circle(layer, x, y, r + 1, P.stone_dark);
    kit::fill_circle(layer, x, y, r, color_of(e.color));
    if (show) {
        char t[4];
        snprintf(t, sizeof t, "%u", unsigned(e.value));
        const lv_font_t* f = metrics().large ? &lv_font_montserrat_12 : &lv_font_montserrat_8;
        kit::text(layer, t, f, e.color == 2 ? P.stone_dark : P.stone_light, x - r, y - r, 2 * r, 2 * r);
    }
}

void draw_palm(lv_layer_t* layer, int x, int y, float r)
{
    const Palette& P = pal();
    kit::line(layer, x, y + int(r * 0.45f), x + int(r * 0.1f), y - int(r * 0.25f), r > 16 ? 3 : 2, P.sq_dark);
    for (int k = -1; k <= 1; k += 2) {
        kit::line(layer, x + int(r * 0.1f), y - int(r * 0.25f), x + k * int(r * 0.45f), y - int(r * 0.05f), 2, P.felt);
        kit::line(layer, x + int(r * 0.1f), y - int(r * 0.25f), x + k * int(r * 0.35f), y - int(r * 0.5f), 2, P.felt);
    }
}

void draw_creature(lv_layer_t* layer, int x, int y, float r, Kind k, bool ring)
{
    const Palette& P = pal();
    if (ring) kit::ring(layer, x, y, int(r * 0.8f), 2, P.lit);
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.opa = LV_OPA_COVER;
    if (k == kShark) {                                       // a grey fin over a wave line
        d.color = P.muted;
        d.p[0].x = x - int(r * 0.4f); d.p[0].y = y + int(r * 0.25f);
        d.p[1].x = x + int(r * 0.35f); d.p[1].y = y + int(r * 0.25f);
        d.p[2].x = x + int(r * 0.05f); d.p[2].y = y - int(r * 0.5f);
        lv_draw_triangle(layer, &d);
        kit::line(layer, x - int(r * 0.55f), y + int(r * 0.3f), x + int(r * 0.55f), y + int(r * 0.3f), 2, P.stone_dark);
    } else if (k == kWhale) {                                // a dark back with a tail
        const lv_color_t c = lv_color_mix(P.stone_dark, P.frame, 160);
        kit::fill_circle(layer, x - int(r * 0.1f), y, int(r * 0.38f), c);
        kit::fill_circle(layer, x + int(r * 0.2f), y + int(r * 0.05f), int(r * 0.3f), c);
        d.color = c;
        d.p[0].x = x - int(r * 0.4f); d.p[0].y = y;
        d.p[1].x = x - int(r * 0.7f); d.p[1].y = y - int(r * 0.3f);
        d.p[2].x = x - int(r * 0.7f); d.p[2].y = y + int(r * 0.25f);
        lv_draw_triangle(layer, &d);
        kit::fill_circle(layer, x + int(r * 0.25f), y - int(r * 0.08f), r > 16 ? 2 : 1, P.stone_light);
    } else {                                                 // the sea serpent: humps and a head
        const lv_color_t c = lv_color_mix(P.piece_a, P.frame, 110);
        for (int i = 0; i < 3; ++i) kit::fill_circle(layer, x - int(r * 0.45f) + i * int(r * 0.3f), y + (i & 1 ? int(r * 0.12f) : 0), int(r * 0.18f), c);
        kit::fill_circle(layer, x + int(r * 0.48f), y - int(r * 0.2f), int(r * 0.22f), c);
        kit::fill_circle(layer, x + int(r * 0.54f), y - int(r * 0.26f), r > 16 ? 2 : 1, P.stone_light);
    }
}

void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int bw = int(W * 9.5f), bh = int(R * 17.0f);
    kit::fill_rect(layer, a.x1 + bx - 2, a.y1 + by - 2, a.x1 + bx + bw + 1, a.y1 + by + bh + 1, sea(), 4);
    const bool mine = my_turn();
    // Hexes: sea lines, land tiles, safe islands, frames on targets
    for (int h = 0; h < kHexes; ++h) {
        int x, y;
        centre_px(a, h, &x, &y);
        const Terrain t = g.terrain[h];
        if (t == kSea) hex_outline(layer, x, y, R, 1, sea_line());
        else {
            const lv_color_t col = t == kBeach ? lv_color_mix(P.lit, P.cell, 110)
                                 : t == kForest ? P.felt
                                 : t == kMountain ? P.sq_dark
                                 : lv_color_mix(P.lit, P.cell, 150);
            fill_hex(layer, x, y, R + 0.5f, P.stone_dark);
            fill_hex(layer, x, y, R - 1.0f, col);
            if (t == kSafe) draw_palm(layer, x, y, R);
        }
    }
    const int er = int(R * (large ? 0.34f : 0.36f));
    // Boats
    for (int b = 0; b < g.boats; ++b) {
        if (g.boat[b] < 0) continue;
        int x, y;
        centre_px(a, g.boat[b], &x, &y);
        const bool picked = mine && sel_kind == 2 && sel == b;
        const int hw = int(R * 0.78f), hh = int(R * 0.3f);
        if (picked) kit::fill_rect(layer, x - hw - 3, y - hh - 3 + int(R * 0.25f), x + hw + 3, y + hh + 3 + int(R * 0.25f), P.lit, hh + 3);
        kit::fill_rect(layer, x - hw, y - hh + int(R * 0.25f), x + hw, y + hh + int(R * 0.25f), lv_color_mix(P.sq_dark, P.stone_dark, 180), hh);
    }
    // Creatures
    for (int k = 0; k < g.creatures; ++k) {
        if (g.cr[k].hex < 0) continue;
        int x, y;
        centre_px(a, g.cr[k].hex, &x, &y);
        const bool ring = mine && g.phase == Phase::Creature && g.cr[k].kind == g.die && has_moves(kMoveCreature, k);
        draw_creature(layer, x, y - (g.swim_count(g.cr[k].hex) ? int(R * 0.35f) : 0), R, g.cr[k].kind, ring || (sel_kind == 3 && sel == k));
    }
    // Explorers, up to three in a hex (boat seats in a row)
    const bool over = g.phase == Phase::Over;
    for (int h = 0; h < kHexes; ++h) {
        int idx[kExplorers], n = 0;
        for (int i = 0; i < kExplorers; ++i)
            if (g.ex[i].hex == h && (g.ex[i].where == kLand || g.ex[i].where == kSwim || g.ex[i].where == kAboard)) idx[n++] = i;
        if (!n) continue;
        int x, y;
        centre_px(a, h, &x, &y);
        for (int j = 0; j < n; ++j) {
            const Explorer& xp = g.ex[idx[j]];
            int dx = 0, dy = 0;
            if (xp.where == kAboard) {
                dx = n == 1 ? 0 : int(R * 0.55f) * (2 * j - (n - 1)) / (n - 1 > 0 ? n - 1 : 1);
                dy = -int(R * 0.05f);
            } else if (n == 2) { dx = (j ? 1 : -1) * int(R * 0.4f); }
            else if (n >= 3) {
                static const float ox[3] = {0.0f, -0.42f, 0.42f}, oy[3] = {-0.36f, 0.3f, 0.3f};
                dx = int(R * ox[j % 3]); dy = int(R * oy[j % 3]);
            }
            const bool show = over || (human(xp.color) && (xp.color == g.turn || !pnp()));
            const bool ring = mine && sel_kind == 1 && sel == idx[j];
            if (xp.where == kSwim) kit::ring(layer, x + dx, y + dy, er + 3, 2, P.stone_light);
            draw_explorer(layer, x + dx, y + dy, xp.where == kAboard && n > 2 ? er - 1 : er, xp, show, ring);
        }
    }
    // Frames on the hexes you may tap now
    if (mine) {
        for (int h = 0; h < kHexes; ++h) {
            if (!is_target(h)) continue;
            int x, y;
            centre_px(a, h, &x, &y);
            hex_outline(layer, x, y, R - 1.5f, large ? 6 : 5, P.stone_dark);
            hex_outline(layer, x, y, R - 1.5f, large ? 3 : 2, g.phase == Phase::Sink ? P.conflict : P.lit);
        }
    }
    // The banner: what a sunk tile had under it
    if (banner && g.news.sunk >= 0) {
        const char* l1 = "";
        switch (g.news.effect) {
            case kSharkTile: l1 = "A Shark!"; break;
            case kWhaleTile: l1 = "A Whale!"; break;
            case kBoatTile:  l1 = "A Boat!"; break;
            case kWhirlpool: l1 = "A Whirlpool!"; break;
            case kVolcano:   l1 = "The Volcano!"; break;
            default: break;
        }
        const lv_font_t* bf = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const int bh2 = lv_font_get_line_height(bf) + 16;
        const int y0 = a.y1 + by + bh / 2 - bh2 / 2;
        kit::fill_rect(layer, a.x1 + 8, y0 - 2, a.x2 - 8, y0 + bh2 + 1, g.news.effect == kVolcano ? P.piece_a : P.lit, 8);
        kit::fill_rect(layer, a.x1 + 10, y0, a.x2 - 10, y0 + bh2 - 1, P.cell, 7);
        kit::text(layer, l1, bf, P.ink, a.x1 + 10, y0, lv_area_get_width(&a) - 20, bh2);
    }
    // Two lines: what to do / what happened, and who has saved how many
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    char t[80];
    const char* line = news;
    if (mine && !over) {
        switch (g.phase) {
            case Phase::Place: {
                const int e = g.next_to_place();
                snprintf(t, sizeof t, "Place your %u: tap an empty tile", e >= 0 ? unsigned(g.ex[e].value) : 0u);
                break;
            }
            case Phase::Move:
                snprintf(t, sizeof t, sel_kind ? "%d move%s left: tap a framed hex" : "%d move%s left: tap your piece",
                         g.moves_left, g.moves_left == 1 ? "" : "s");
                break;
            case Phase::Sink: snprintf(t, sizeof t, "Sink a tile: tap a framed one"); break;
            case Phase::Creature:
                snprintf(t, sizeof t, "%s: move one up to %d", kind_name(Kind(g.die)), kind_range(Kind(g.die)));
                break;
            default: t[0] = 0; break;
        }
        line = t;
    }
    kit::text(layer, line, f, P.ink, a.x1, a.y1 + info_y, lv_area_get_width(&a), lh);
    char s[80];
    if (over) snprintf(s, sizeof s, "%.6s %d  %.6s %d  %.6s %d  %.6s %d", name(0), g.score(0), name(1), g.score(1), name(2), g.score(2), name(3), g.score(3));
    else {
        int n[kColors] = {};
        for (const Explorer& x : g.ex) if (x.where == kSaved) ++n[x.color];
        snprintf(s, sizeof s, "Saved: %.6s %d  %.6s %d  %.6s %d  %.6s %d", name(0), n[0], name(1), n[1], name(2), n[2], name(3), n[3]);
    }
    kit::text(layer, s, f, over ? P.ink : P.muted, a.x1, a.y1 + info_y + lh, lv_area_get_width(&a), lh);
}

// ---- Flow -------------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    refresh();
    char s[40], sh[24];
    if (g.phase == Phase::Over) {
        const int w = g.winner();
        if (w < 0) { snprintf(s, sizeof s, "A tie!"); snprintf(sh, sizeof sh, "A tie!"); }
        else {
            const bool you = w == 0 && !pnp();
            snprintf(s, sizeof s, you ? "You win!" : "%s wins", name(w));
            snprintf(sh, sizeof sh, you ? "You win!" : "%s wins", name(w));
        }
    } else if (human(g.turn) && !pnp()) {
        snprintf(s, sizeof s, g.phase == Phase::Place ? "Place your explorers" : "Your turn (Red)");
        snprintf(sh, sizeof sh, g.phase == Phase::Place ? "Place explorers" : "Your turn");
    } else {
        snprintf(s, sizeof s, "%s's turn (%s)", name(g.turn), kColorNames[g.turn]);
        snprintf(sh, sizeof sh, "%s's turn", name(g.turn));
    }
    kit::top_bar_status(bar, s, sh);
    if (key) {
        const char* label = nullptr;
        if (g.phase == Phase::Over) label = "Play Again";
        else if (my_turn()) {
            if (g.phase == Phase::Place && !pnp()) label = "Place For Me";
            else if (g.phase == Phase::Move) label = "Done Moving";
            else if (g.phase == Phase::Creature) label = "Skip";
        }
        lv_obj_set_hidden(key, label == nullptr);
        if (label) lv_label_set_text(lv_obj_get_child(key, 0), label);
    }
    if (area) lv_obj_invalidate(area);
}

void game_over()
{
    if (!S->recorded && recordable()) record();
    const int w = S->g.winner();
    const bool won = w >= 0 && human(w);
    sound(won ? Sound::Win : Sound::Lose);
    if (won && !pnp()) kit::flash();
}

// After any action: news, sounds, the pause before the next
void after(const Action& a, int who)
{
    Game& g = S->g;
    const bool me = human(who);
    char m[64] = "";
    if (a.act == kSinkTile) {
        roll_shown = false;
        const News& n = g.news;
        if (n.effect == kVolcano) { snprintf(m, sizeof m, "The Volcano erupts! The game is over"); sound(Sound::Boom); }
        else if (n.lost) snprintf(m, sizeof m, "%s sank a tile: %d lost!", name(who), n.lost);
        else if (n.effect == kBoatTile) snprintf(m, sizeof m, "%s sank a tile: a boat!", name(who));
        else snprintf(m, sizeof m, "%s sank a tile", name(who));
        if (n.effect != kVolcano) sound(n.lost ? Sound::Error : n.effect == kBoatTile ? Sound::Hint : Sound::Move);
        banner = n.effect != kNothing;
        banner_until = now_ms + kBannerMs;
    } else if (a.act == kMoveCreature) {
        const News& n = g.news;
        const char* k = kind_name(g.cr[a.who].kind);
        if (n.c_lost) { snprintf(m, sizeof m, "%s's %s took %d!", name(who), k, n.c_lost); sound(Sound::Error); }
        else if (n.c_tipped) { snprintf(m, sizeof m, "%s's %s sank a boat!", name(who), k); sound(Sound::Error); }
        else { snprintf(m, sizeof m, "%s moved a %s", name(who), k); sound(Sound::Turn); }
    } else if (a.act == kSkipCreature) {
        if (!news[0] || strncmp(news, "No ", 3) != 0) snprintf(m, sizeof m, "%s let the creatures be", name(who));
    } else if (a.act == kStepExplorer || a.act == kStepBoat) {
        sound(me ? Sound::Place : Sound::Turn);
        if (a.act == kStepExplorer && g.ex[a.who].where == kSaved) snprintf(m, sizeof m, "%s saved an explorer!", name(who));
    } else if (a.act == kPlace && me) {
        sound(Sound::Place);
    }
    if (m[0]) snprintf(news, sizeof news, "%s", m);
    // keep the selection on a piece that can go on moving
    if (me && g.phase == Phase::Move && (a.act == kStepExplorer || a.act == kStepBoat)) {
        sel_hex = a.to;
        refresh();
        if (!has_moves(a.act, a.who)) clear_sel();
    } else clear_sel();
    if (g.phase == Phase::Over) game_over();
    save();
    if (!me) { waiting = true; wait_until = now_ms + (a.act == kPlace ? kPlaceMs : a.act == kSinkTile ? kSinkMs : kStepMs); }
    update();
}

void do_action(const Action& a)
{
    const int who = S->g.turn;
    if (!S->g.apply(a)) return;
    after(a, who);
}

void new_game(int level, int people)
{
    kit::flash_stop();
    if (S->g.phase != Phase::Over && S->g.phase != Phase::Place && !S->recorded && recordable()) record();   // left part-way
    *S = State{};
    S->level = uint8_t(level);
    S->people = uint8_t(people);
    S->g.start(seed_now());
    clear_sel();
    news[0] = 0;
    banner = false;
    save();
    update();
}

void key_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.phase == Phase::Over) { new_game(S->level, S->people); return; }
    if (!my_turn()) return;
    if (g.phase == Phase::Place) {
        while (g.phase == Phase::Place && human(g.turn)) {                 // the rest of yours, sensibly
            const Action a = g.ai(1);
            if (!g.apply(a)) break;
            while (g.phase == Phase::Place && !human(g.turn)) g.apply(g.ai(pnp() ? 1 : S->level));
        }
        save();
        update();
        return;
    }
    Action a;
    a.act = g.phase == Phase::Move ? kEndMoves : kSkipCreature;
    if (g.phase == Phase::Move || g.phase == Phase::Creature) do_action(a);
}

void press_cb(lv_event_t*) { lv_indev_get_point(lv_indev_active(), &press_pt); }

void board_cb(lv_event_t*)
{
    if (!S || overlay_open() || !area || !my_turn()) return;
    Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const int h = hex_from_point(a, press_pt.x, press_pt.y);
    if (h < 0) { clear_sel(); update(); return; }
    // a target?
    for (int i = 0; i < acts_n; ++i) {
        const Action& x = acts[i];
        if (x.to != h) continue;
        const bool ok = x.act == kPlace || x.act == kSinkTile ||
                        (x.act == kStepExplorer && sel_kind == 1 && x.who == sel) ||
                        (x.act == kStepBoat && sel_kind == 2 && x.who == sel) ||
                        (x.act == kMoveCreature && sel_kind == 3 && x.who == sel);
        if (ok) { do_action(x); return; }
    }
    // pick a piece in that hex: your boat, then your explorers best first; again = the next one
    struct Item { int8_t kind, idx; };
    Item items[8];
    int n = 0;
    if (g.phase == Phase::Move) {
        const int b = g.boat_at(h);
        if (b >= 0 && has_moves(kStepBoat, b)) items[n++] = Item{2, int8_t(b)};
        for (int v = 6; v >= 1 && n < 8; --v)
            for (int e = 0; e < kExplorers && n < 8; ++e)
                if (g.ex[e].hex == h && g.ex[e].value == v && has_moves(kStepExplorer, e)) items[n++] = Item{1, int8_t(e)};
    } else if (g.phase == Phase::Creature) {
        const int k = g.creature_at(h);
        if (k >= 0 && has_moves(kMoveCreature, k)) items[n++] = Item{3, int8_t(k)};
    }
    if (!n) { clear_sel(); update(); return; }
    int at = 0;
    if (sel_hex == h)
        for (int i = 0; i < n; ++i) if (items[i].kind == sel_kind && items[i].idx == sel) at = (i + 1) % n;
    sel_kind = items[at].kind;
    sel = items[at].idx;
    sel_hex = int8_t(h);
    update();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const lv_font_t* f = m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    lh = lv_font_get_line_height(f);
    const int top = bar.h;
    const int room_h = m.h - kh - 2 * pad - 2 * lh - 6;
    const float rw = float(m.w - 2 * pad) / (9.5f * 1.7320508f);
    const float rh = float(room_h) / 17.0f;
    R = rw < rh ? rw : rh;
    W = R * 1.7320508f;
    bx = (m.w - int(W * 9.5f)) / 2;
    by = 2;
    info_y = by + int(R * 17.0f) + 4;
    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, info_y + 2 * lh + 2);
    lv_obj_set_pos(area, 0, top + pad / 2);
    lv_obj_set_clickable(area, true);
    lv_obj_add_event_cb(area, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(area, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(area, board_cb, LV_EVENT_CLICKED, nullptr);
    key = make_key(scr, m.w - 2 * pad, kh, key_cb, 0);
    key_label(key, "", menu_font());
    set_checked(key, true);
    lv_obj_set_pos(key, pad, m.h - pad - kh);
    clock_ = kit::Clock{};
    update();
}

// ---- Stats ------------------------------------------------------------------------------------
void stats_back_cb(lv_event_t*) { open_menu(); }
void stats_action_cb(lv_event_t* e);

void open_stats()
{
    overlay_begin("Stats");
    static Summary sum;
    sum = Summary{};
    const bool ok = shell().stats_read && shell().stats_read(kId, stats_line, &sum);
    const bool large = metrics().large;
    const lv_font_t* hf = &lv_font_montserrat_14;
    const lv_font_t* bf = large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    if (!ok || sum.games == 0) {
        overlay_text("No games recorded yet. Every finished game is listed here.", false);
    } else {
        Table& t = scratch_table(0);
        table_clear(t);
        char a[12], b[12], c[12], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.wins);
        snprintf(c, sizeof c, "%lu%%", (unsigned long)(sum.wins * 100 / sum.games));
        snprintf(d, sizeof d, "%u", unsigned(sum.best));
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Wins", "Won", "Best"};
        static const int8_t pct[4] = {25, 25, 25, 25};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[8], s2[8], tm[16];
            snprintf(s1, sizeof s1, "%u", unsigned(r.place));
            snprintf(s2, sizeof s2, "%u", unsigned(r.saved));
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, kLevels[r.level < 3 ? r.level : 0], tm);
        }
        const char* const head2[4] = {"Place", "Saved", "Level", "Time"};
        static const int8_t pct2[4] = {22, 22, 28, 28};
        table_show(rt, head2, pct2, hf, hf);
    }
    char where[64];
    snprintf(where, sizeof where, "Saved on the %s.", shell().stats_location ? shell().stats_location() : "board");
    overlay_text(where, true);
    const int bh = menu_btn_h(), g2 = large ? 10 : 6;
    lv_obj_t* back = overlay_bottom_button("Back", stats_back_cb, 0);
    if (ok && sum.games > 0 && shell().stats_delete_last && shell().stats_clear) {
        lv_obj_t* row = lv_obj_create(overlay());
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, lv_pct(100), bh);
        lv_obj_set_ignore_layout(row, true);
        lv_obj_align_to(row, back, LV_ALIGN_OUT_TOP_MID, 0, -g2);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_style_pad_column(row, 6, 0);
        const char* labels[2] = {"Delete Last", "Clear All"};
        for (int k = 0; k < 2; ++k) {
            lv_obj_t* b = make_key(row, 10, bh, stats_action_cb, k + 1);
            lv_obj_set_flex_grow(b, 1);
            key_label(b, labels[k], menu_font());
        }
    }
}

void stats_action_cb(lv_event_t* e)
{
    const int which = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (which == 1 && shell().stats_delete_last) shell().stats_delete_last(kId);
    if (which == 2 && shell().stats_clear) shell().stats_clear(kId);
    open_stats();
}

void people_cb(lv_event_t* e)
{
    const int n = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    close_overlays();
    new_game(1, n);
}

void people_back_cb(lv_event_t*) { open_menu(); }

void open_people()
{
    overlay_begin("Pass and Play");
    overlay_text("Two to four people take turns on one board: Red, then Blue, Yellow, Green. The computer plays any colours left over (Medium).", false);
    for (int n = 2; n <= kColors; ++n) {
        char t[16];
        snprintf(t, sizeof t, "%d Players", n);
        overlay_button(overlay(), t, people_cb, n, n == 2);
    }
    overlay_text("Pass-and-play games aren't kept in the stats.", true);
    overlay_back(people_back_cb, 0);
}

void menu_pick(int id)
{
    if (id >= kit::kLevel0 && id <= kit::kLevel2) new_game(id - kit::kLevel0, 1);
    else if (id == kit::kOptions) open_people();
}
void menu_back() { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Escape from CYD", kLevels, h, false, "Pass and Play");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now()); }
    clear_sel();
    news[0] = 0;
    banner = false;
    waiting = true;
    wait_until = 0;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    area = key = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S || frozen) return;
    now_ms = now;
    Game& g = S->g;
    if (waiting && wait_until == 0) wait_until = now + 400;
    if (overlay_open()) {
        if (waiting) wait_until = now + 300;
        if (banner) banner_until = now + 300;
    } else {
        if (banner && int32_t(now - banner_until) >= 0) { banner = false; update(); }
        if (waiting && !banner && int32_t(now - wait_until) >= 0) { waiting = false; update(); }
    }
    // Your creature roll with nothing of that kind: pass on
    if (my_turn() && g.phase == Phase::Creature) {
        refresh();
        if (acts_n == 1) {
            snprintf(news, sizeof news, "No %s to move", kind_name(Kind(g.die)));
            Action a; a.act = kSkipCreature;
            g.apply(a);
            waiting = true;
            wait_until = now + kRollMs;
            save();
            update();
        }
    }
    // The computers, one action at a time
    if (!waiting && !banner && !overlay_open() && g.phase != Phase::Over && !human(g.turn)) {
        if (g.phase == Phase::Creature && !roll_shown) {
            // show the roll first
            snprintf(news, sizeof news, "%s rolled a %s", name(g.turn), kind_name(Kind(g.die)));
            roll_shown = true;
            waiting = true;
            wait_until = now + kRollMs;
            update();
        } else {
            const Action a = g.ai(pnp() ? 1 : S->level);
            const int who = g.turn;
            if (g.apply(a)) after(a, who);
            else { Action e; e.act = g.phase == Phase::Move ? kEndMoves : kSkipCreature; if (g.apply(e)) after(e, who); }
        }
    }
    clock_.tick(now, g.phase != Phase::Over, S->seconds);
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State* tmp = nullptr;
    const State* st = S;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    const Game& g = st->g;
    if (g.phase == Phase::Over) snprintf(buf, cap, "Game over: you saved %d", g.score(0));
    else if (st->people > 1) snprintf(buf, cap, "Pass and play, %d players", st->people);
    else snprintf(buf, cap, "%s, you have saved %d", kLevels[st->level], g.score(0));
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a sea hex with a palm island, a boat and a fin
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int cx = a.x1 + s / 2, cy = a.y1 + s / 2;
    fill_hex(layer, cx, cy, float(s) * 0.5f, sea());
    fill_hex(layer, cx - s / 6, cy - s / 8, float(s) * 0.2f, lv_color_mix(P.lit, P.cell, 150));
    draw_palm(layer, cx - s / 6, cy - s / 8, float(s) * 0.2f);
    kit::fill_rect(layer, cx - s / 10, cy + s / 6, cx + s / 4, cy + s / 6 + s / 14, lv_color_mix(P.sq_dark, P.stone_dark, 180), s / 28);
    draw_creature(layer, cx + s / 5, cy - s / 8, float(s) * 0.22f, kShark, false);
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

namespace escape_preview {
escape::Game* game() { return S ? &S->g : nullptr; }
void hold(bool on) { frozen = on; waiting = false; banner = false; update(); }
void select(int kind, int idx, int hex) { sel_kind = int8_t(kind); sel = int8_t(idx); sel_hex = int8_t(hex); update(); }
void news_line(const char* t) { snprintf(news, sizeof news, "%s", t); update(); }
void show_banner(bool on) { banner = on; update(); }
}

namespace games {
extern const GameOps escape_ops;
const GameOps escape_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
