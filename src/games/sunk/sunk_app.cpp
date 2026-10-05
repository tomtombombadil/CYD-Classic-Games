// You Sunk My CYD!: screen and registry entry. The rules live in
// sunk_core.*; turns, the computer, menus, stats and wireless play come
// from the shared two-player controller (games/common/match.*).
//
// One sea on screen at a time, as large as fits: Their Waters (where you
// fire; tap a cell) or My Fleet (your ships and their shots), switched with
// the two keys at the bottom. The setup (Options -> Ship Placement, Tom,
// 2026-10-05): Manual (the default) - largest ship first, tap the square
// where one end goes, then a lit square in the direction it should point;
// Undo takes one back - or Random: Shuffle until you like it. Then Ready.
//
// Pass-and-play hides each player's sea from the other: after a move the
// board says "Pass to Gold" and waits for Ready.
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "sunk_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace sunk;
using namespace ui;

constexpr const char* kId = "sunk";
const twoplayer::Sides kSides = {"Blue", "Gold"};

Board*    B = nullptr;
lv_obj_t* sea_obj = nullptr;
lv_obj_t* cover_l = nullptr;
lv_obj_t* key_a = nullptr;      // left half: Their Waters / Shuffle / Undo
lv_obj_t* key_b = nullptr;      // right half: My Fleet / Ready
lv_obj_t* key_c = nullptr;      // full width: Ready (pass-and-play) / Pass to ...

// What the screen shows (not saved: both boards of a wireless game save the same game)
struct View {
    int      page = 0;              // 0 Their Waters, 1 My Fleet
    int      viewer = 0;            // pass-and-play: whose seas are showing
    bool     cover = false;         // pass-and-play: "Pass the board to ..." until Ready
    bool     result = false;        // pass-and-play: a shot's result is up, then Pass
    bool     ready_pending = false; // Ready tapped before this side's turn to set up
    uint32_t seed = 0;              // Random: the fleet being shuffled
    Fleet    preview;               // the fleet being placed (not played yet)
    int      n = 0;                 // Manual: ships placed so far
    int      anchor = -1;           // Manual: the square picked for the next ship's end
} V;
bool manual = true;                 // Options -> Ship Placement (saved in /games/sunk_opt.bin)

int cell = 20, lab = 12;            // cell size, label strip (px)
bool numbers_left = true;           // row numbers away from the stylus hand

bool pnp()     { return match::state().mode == twoplayer::Mode::PassAndPlay; }
int  viewer()  { return pnp() ? V.viewer : match::my_side(); }
bool over()    { return B->result() != -1; }
// The viewer still sets up a fleet (Manual or Random, then Ready)
bool placing() { return B->setup() && viewer() >= 0 && B->placed(viewer()) < kShips && !over(); }
bool fleet_done() { return manual ? V.n == kShips : true; }

// Ships this side has already sent (a fleet cut short, e.g. by a restart)
int sent() { return B && B->setup() && viewer() >= 0 ? B->placed(viewer()) : 0; }

// The preview starts with the ships already sent
void base_fleet(Fleet& f)
{
    f = Fleet{};
    for (int i = 0; i < sent(); ++i) place_ship(f, i, ship_key(B->fleet[viewer()].ship[i]));
}

void new_seed()
{
    const uint32_t r = shell().random_seed ? shell().random_seed() : lv_tick_get();
    V.seed = (r ^ (V.seed * 2654435761u)) & kSeedMax;
    if (!sent()) { random_fleet(V.seed, V.preview); return; }
    base_fleet(V.preview);
    uint32_t seed = V.seed;
    for (int i = sent(); i < kShips; ++i) {
        uint32_t places[2 * kCells];
        const int n = ship_places(V.preview, i, places);
        seed = seed * 1103515245u + 12345u;
        if (n) place_ship(V.preview, i, places[(seed >> 8) % uint32_t(n)]);
    }
}

// A fresh fleet to place: empty (Manual) or shuffled (Random)
void start_placing()
{
    V.anchor = -1;
    V.ready_pending = false;
    if (manual) { base_fleet(V.preview); V.n = sent(); }
    else { new_seed(); V.n = kShips; }
}

// Manual: the ship placed from the anchor toward direction d (0 up, 1 right,
// 2 down, 3 left); 0xFFFF when it doesn't fit there
uint32_t aimed(int anchor, int d)
{
    const int len = kLen[V.n], r = anchor / kN, c = anchor % kN;
    int top = anchor;
    if (d == 0) top = (r - len + 1) * kN + c;
    if (d == 3) top = r * kN + c - len + 1;
    if ((d == 0 && r - len + 1 < 0) || (d == 3 && c - len + 1 < 0)) return 0xFFFF;
    const uint32_t key = ship_key(top, d == 0 || d == 2);
    return ship_fits(V.preview, V.n, key) ? key : 0xFFFF;
}

// Manual: which way (0-3) a tap on cell `t` aims the ship from the anchor, -1 none
int aim_of(int t)
{
    if (V.anchor < 0 || t == V.anchor) return -1;
    const int len = kLen[V.n], ar = V.anchor / kN, ac = V.anchor % kN, r = t / kN, c = t % kN;
    int d = -1;
    if (c == ac && r < ar && ar - r < len) d = 0;
    else if (r == ar && c > ac && c - ac < len) d = 1;
    else if (c == ac && r > ar && r - ar < len) d = 2;
    else if (r == ar && c < ac && ac - c < len) d = 3;
    return d >= 0 && aimed(V.anchor, d) != 0xFFFF ? d : -1;
}

bool can_anchor(int t)
{
    if (V.preview.at[t]) return false;
    for (int d = 0; d < 4; ++d) if (aimed(t, d) != 0xFFFF) return true;
    return false;
}

// Options file: "SKO1" + flags (bit 0 = Random placement)
void load_options()
{
    uint8_t b[5];
    const Shell& H = shell();
    manual = true;
    if (H.load_game && H.load_game("sunk_opt", b, sizeof b) == sizeof b && memcmp(b, "SKO1", 4) == 0)
        manual = !(b[4] & 1);
}

void save_options()
{
    uint8_t b[5] = {'S', 'K', 'O', '1', uint8_t(manual ? 0 : 1)};
    if (shell().save_game) shell().save_game("sunk_opt", b, sizeof b);
}

void coord(int c, char* buf, size_t cap) { snprintf(buf, cap, "%c%u", 'A' + c % kN, unsigned(c) / kN % kN + 1); }

void redraw() { if (sea_obj) lv_obj_invalidate(sea_obj); }
void update_keys();

// ---- Rules for the controller ---------------------------------------------------------------
int result() { return B->result(); }
int turn()   { return B->turn(); }
int moves()  { return B->moves; }

void play(int m)
{
    const bool shot = !B->setup();
    const int mover = B->turn();
    if (!B->play(uint32_t(m))) return;
    if (pnp() && !over()) {
        if (shot) V.result = true;              // see where it landed, then pass
        else if (B->turn() != mover) V.cover = true;   // the fleet is set: pass at once
    }
    if (B->moves == kSetupPlies) V.page = 0;    // both fleets set: fire away
    update_keys();
    redraw();
}

void reset()
{
    *B = Board{};
    V.cover = pnp();
    V.viewer = 0;
    V.result = false;
    V.page = 0;
    start_placing();
    update_keys();
}

int think(int level, uint32_t seed, volatile bool*) { return int(best_move(*B, level, seed)); }

// The last shot: who fired it, the cell, and what it did
struct LastShot { int side = -1, cell = -1, ship = -1; bool hit = false, sunk = false; };
LastShot last_shot()
{
    LastShot s;
    if (B->moves <= kSetupPlies) return s;
    s.side = (B->moves - 1 - kSetupPlies) & 1;
    s.cell = B->last[s.side];
    if (s.cell < 0) return LastShot{};
    s.ship = B->fleet[s.side ^ 1].at[s.cell] - 1;
    s.hit = s.ship >= 0;
    s.sunk = s.hit && B->sunk(s.side ^ 1, s.ship);
    return s;
}

void move_sound(bool by_other)
{
    const LastShot s = last_shot();
    if (B->moves <= kSetupPlies || s.side < 0) {
        // A fleet: one sound when its last ship is in
        if (B->moves % kShips == 0) sound(by_other ? Sound::Turn : Sound::Place);
        return;
    }
    if (!by_other) sound(s.sunk ? Sound::Trill : s.hit ? Sound::Move : Sound::Place);
    else           sound(s.sunk ? Sound::Error : s.hit ? Sound::Move : Sound::Turn);
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (V.cover) {
        snprintf(buf, cap, "Pass the board to %s", B->turn() == 0 ? kSides.side1 : kSides.side2);
        return;
    }
    if (placing()) {
        if (V.ready_pending) snprintf(buf, cap, "Your fleet is ready");
        else if (!manual) snprintf(buf, cap, "Shuffle, then tap Ready");
        else if (V.n == kShips) snprintf(buf, cap, "All placed: tap Ready");
        else if (V.anchor >= 0) snprintf(buf, cap, "Tap a lit square to aim it");
        else snprintf(buf, cap, "%s (%d): tap one end", ship_name(V.n), kLen[V.n]);
        return;
    }
    const LastShot s = last_shot();
    if (s.side < 0) return;
    char at[8];
    coord(s.cell, at, sizeof at);
    const char* side_name = s.side == 0 ? kSides.side1 : kSides.side2;
    if (pnp()) {
        if (s.sunk) snprintf(buf, cap, "%s sank %s's %s!", side_name, s.side == 0 ? kSides.side2 : kSides.side1,
                             ship_name(s.ship));
        else snprintf(buf, cap, "%s fired at %s: %s", side_name, at, s.hit ? "hit!" : "miss");
        return;
    }
    const bool mine = s.side == match::my_side();
    const char* them = match::opponent_name();
    if (s.sunk) {
        if (mine) snprintf(buf, cap, "You sank their %s!", ship_name(s.ship));
        else      snprintf(buf, cap, "%s sank your %s", them, ship_name(s.ship));
    } else {
        snprintf(buf, cap, "%s fired at %s: %s", mine ? "You" : them, at, s.hit ? "hit!" : "miss");
    }
}

void score(char* buf, size_t cap)
{
    snprintf(buf, cap, "Ships afloat: %s %d, %s %d", kSides.side1, B->afloat(0), kSides.side2, B->afloat(1));
}

int list(int* out, int cap)
{
    int n = 0;
    if (B->setup()) {
        uint32_t places[2 * kCells];
        const int k = ship_places(B->fleet[B->turn()], B->placed(B->turn()), places);
        for (int i = 0; i < k && n < cap; ++i) out[n++] = int(places[i]);
        return n;
    }
    for (int c = 0; c < kCells && n < cap; ++c) if (B->can_play(uint32_t(c))) out[n++] = c;
    return n;
}

void options_open();

match::Game make_game()
{
    match::Game g{kId, "You Sunk My CYD!", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.move_sound = move_sound;
    g.ai_stack = 6 * 1024;
    g.legal = [](int m) { return B->can_play(uint32_t(m)); };
    g.list = list;
    g.options = options_open;
    return g;
}

// ---- Keys ---------------------------------------------------------------------------------------
enum Key : intptr_t { kWaters = 1, kFleet, kShuffle, kReady, kCoverReady, kPass, kUndo };

void set_key(lv_obj_t* k, bool show, const char* text, bool on)
{
    if (!k) return;
    lv_obj_set_hidden(k, !show);
    if (!show) return;
    lv_label_set_text(lv_obj_get_child(k, 0), text);
    set_checked(k, on);
}

void update_keys()
{
    if (!key_a) return;
    const bool ov = over();
    lv_obj_set_hidden(cover_l, !V.cover || ov);
    if (V.cover && !ov) {
        char t[96];
        const char* who = B->turn() == 0 ? kSides.side1 : kSides.side2;
        const char* other = B->turn() == 0 ? kSides.side2 : kSides.side1;
        snprintf(t, sizeof t, "Pass the board to %s.\n\n%s: tap Ready when %s can't see the screen.", who, who, other);
        lv_label_set_text(cover_l, t);
    }
    if (ov) {                                        // the controller's Play Again keys
        set_key(key_a, false, "", false);
        set_key(key_b, false, "", false);
        set_key(key_c, false, "", false);
    } else if (V.cover || V.result) {
        char t[32];
        const char* next = B->turn() == 0 ? kSides.side1 : kSides.side2;
        if (V.cover) snprintf(t, sizeof t, "Ready");
        else snprintf(t, sizeof t, "Pass to %s", next);
        set_key(key_a, false, "", false);
        set_key(key_b, false, "", false);
        set_key(key_c, true, t, true);
    } else if (placing()) {
        set_key(key_a, true, manual ? "Undo" : "Shuffle", false);
        set_key(key_b, true, V.ready_pending ? "Waiting..." : "Ready", !V.ready_pending && fleet_done());
        set_dim(key_a, V.ready_pending || (manual && V.n == sent() && V.anchor < 0));
        set_dim(key_b, !V.ready_pending && !fleet_done());
        set_key(key_c, false, "", false);
    } else {
        set_dim(key_a, false);
        set_dim(key_b, false);
        set_key(key_a, true, "Their Waters", V.page == 0);
        set_key(key_b, true, "My Fleet", V.page == 1);
        set_key(key_c, false, "", false);
    }
}

void changed()
{
    update_keys();
    redraw();
    match::refresh();
}

// Ready: the fleet's ships go in as moves, Carrier first. Before this
// side's turn to set up, they wait and go when it comes (tick()).
bool send_fleet()
{
    const int me = viewer();
    while (B->setup() && B->placed(me) < kShips && B->turn() == me && match::human_may_move()) {
        const uint32_t key = ship_key(V.preview.ship[B->placed(me)]);
        if (!B->can_play(key)) break;
        match::human_move(int(key));
    }
    return B->placed(me) == kShips;
}

void ready()
{
    if (!fleet_done()) return;
    V.ready_pending = !send_fleet();
    changed();
}

void do_key(Key k)
{
    if (!B) return;
    switch (k) {
        case kWaters:  V.page = 0; break;
        case kFleet:   V.page = 1; break;
        case kShuffle: if (!V.ready_pending) new_seed(); break;
        case kUndo:
            if (V.ready_pending) break;
            if (V.anchor >= 0) V.anchor = -1;
            else if (V.n > sent()) {
                --V.n;
                Fleet f;                                 // rebuild without the last ship
                for (int i = 0; i < V.n; ++i) place_ship(f, i, ship_key(V.preview.ship[i]));
                V.preview = f;
            }
            break;
        case kReady:   if (!V.ready_pending) { ready(); return; } break;
        case kCoverReady:
            V.cover = false;
            V.viewer = B->turn();
            V.page = 0;
            if (placing()) start_placing();
            break;
        case kPass:
            V.result = false;
            V.cover = true;
            break;
    }
    changed();
}

// The bottom keys share the controller's Play Again row (hidden while a game is on)
void key_row_cb(lv_event_t* e)
{
    lv_obj_t* k = lv_event_get_target_obj(e);
    if (k == key_a)      do_key(placing() ? (manual ? kUndo : kShuffle) : kWaters);
    else if (k == key_b) do_key(placing() ? kReady : kFleet);
    else                 do_key(V.cover ? kCoverReady : kPass);
}

// ---- The sea ------------------------------------------------------------------------------------
void frame_rect(lv_layer_t* layer, int x1, int y1, int x2, int y2, int w, lv_color_t c, int radius)
{
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_opa = LV_OPA_TRANSP;
    d.border_color = c;
    d.border_width = w;
    d.border_opa = LV_OPA_COVER;
    d.radius = radius;
    lv_area_t a{x1, y1, x2, y2};
    lv_draw_rect(layer, &d, &a);
}

void ship_shape(lv_layer_t* layer, int x0, int y0, const Ship& s, int len, lv_color_t c)
{
    const int r = s.cell / kN, col = s.cell % kN;
    const int x1 = x0 + col * cell + 2, y1 = y0 + r * cell + 2;
    const int x2 = x0 + (col + (s.down ? 1 : len)) * cell - 2, y2 = y0 + (r + (s.down ? len : 1)) * cell - 2;
    kit::fill_rect(layer, x1, y1, x2, y2, c, (cell - 4) / 2);
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int sea = kN * cell;
    const int x0 = a.x1 + (numbers_left ? lab : 0), y0 = a.y1 + lab;
    const lv_font_t* lf = metrics().large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    // Labels: A-J along the top, 1-10 down the side away from the hand
    for (int i = 0; i < kN; ++i) {
        char t[4];
        snprintf(t, sizeof t, "%c", 'A' + i);
        kit::text(layer, t, lf, P.muted, x0 + i * cell, a.y1, cell, lab);
        snprintf(t, sizeof t, "%d", i + 1);
        kit::text(layer, t, lf, P.muted, numbers_left ? a.x1 : x0 + sea, y0 + i * cell, lab, cell);
    }
    const lv_color_t water = P.frame, deep = lv_color_darken(P.frame, 70);
    const lv_color_t ship_c = lv_color_mix(P.stone_light, P.stone_dark, 120);
    const lv_color_t hit_c = P.piece_a, peg = P.stone_light;
    kit::fill_rect(layer, x0, y0, x0 + sea, y0 + sea, deep);
    const bool covered = V.cover && !over();
    for (int c = 0; c < kCells; ++c) {
        const int x = x0 + (c % kN) * cell, y = y0 + (c / kN) * cell;
        kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, covered ? deep : water);
    }
    if (covered) return;
    const int me = viewer();
    const int pr = cell / 7 > 2 ? cell / 7 : 2, hr = cell / 3;
    auto cx = [&](int c) { return x0 + (c % kN) * cell + cell / 2; };
    auto cy = [&](int c) { return y0 + (c / kN) * cell + cell / 2; };
    const lv_color_t wreck = lv_color_mix(hit_c, P.stone_dark, 100);      // a sunk ship
    // A hit: a red peg (smaller on a sunk ship, so the wreck shows round it)
    auto hit_peg = [&](int c, bool on_wreck) {
        const int r = on_wreck ? hr * 2 / 3 : hr;
        kit::fill_circle(layer, cx(c), cy(c), r + 1, lv_color_darken(hit_c, 90));
        kit::fill_circle(layer, cx(c), cy(c), r, hit_c);
    };
    if (placing()) {                                     // the fleet being placed
        const int shown = manual ? V.n : kShips;
        for (int i = 0; i < shown; ++i) ship_shape(layer, x0, y0, V.preview.ship[i], kLen[i], ship_c);
        if (manual && V.anchor >= 0 && V.n < kShips) {
            // The squares the ship can point to light up; the anchor is gold
            const lv_color_t lit = lv_color_mix(P.lit, water, 225);     // strong: TN panels wash out pale tints
            for (int d = 0; d < 4; ++d) {
                const uint32_t key = aimed(V.anchor, d);
                if (key == 0xFFFF) continue;
                const Ship sh{uint8_t(key & 0x7F), (key & 0x80) != 0};
                for (int k = 0; k < kLen[V.n]; ++k) {
                    const int c = sh.cell_at(k, kLen[V.n]);
                    if (c == V.anchor) continue;
                    const int x = x0 + (c % kN) * cell, y = y0 + (c / kN) * cell;
                    kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, lit);
                }
            }
            const int x = x0 + (V.anchor % kN) * cell, y = y0 + (V.anchor / kN) * cell;
            kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, P.piece_b);
            frame_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, 2, P.stone_dark, 0);
        }
        return;
    }
    if (me < 0) return;
    const bool own = V.page == 1 || B->setup();
    if (own) {                                           // my ships and the other side's shots
        const Fleet& f = B->fleet[me];
        for (int i = 0; i < B->placed(me); ++i)
            ship_shape(layer, x0, y0, f.ship[i], kLen[i], B->sunk(me, i) ? wreck : ship_c);
        for (int c = 0; c < kCells; ++c) {
            if (!B->shot[me ^ 1][c]) continue;
            if (f.at[c]) hit_peg(c, B->sunk(me, f.at[c] - 1));
            else kit::fill_circle(layer, cx(c), cy(c), pr, peg);
        }
        const int l = B->last[me ^ 1];
        if (l >= 0 && B->moves > kSetupPlies)
            frame_rect(layer, cx(l) - cell / 2, cy(l) - cell / 2, cx(l) + cell / 2, cy(l) + cell / 2, 2, P.piece_b, 2);
        return;
    }
    // Their waters: what I know; sunk ships show; at the end every ship does
    const Fleet& f = B->fleet[me ^ 1];
    for (int i = 0; i < kShips; ++i) {
        if (B->sunk(me ^ 1, i)) ship_shape(layer, x0, y0, f.ship[i], kLen[i], wreck);
        else if (over()) ship_shape(layer, x0, y0, f.ship[i], kLen[i], lv_color_mix(ship_c, water, 150));
    }
    for (int c = 0; c < kCells; ++c) {
        switch (B->known(me, c)) {
            case kMiss:  kit::fill_circle(layer, cx(c), cy(c), pr, peg); break;
            case kHit:   hit_peg(c, false); break;
            case kSunk:  hit_peg(c, true); break;
            default: break;
        }
    }
    const int l = B->last[me];
    if (l >= 0 && B->moves > kSetupPlies)
        frame_rect(layer, cx(l) - cell / 2, cy(l) - cell / 2, cx(l) + cell / 2, cy(l) + cell / 2, 2, P.piece_b, 2);
}

// Taps act on release at the point where the stylus came down (lift-off
// readings drift), like board8
lv_point_t press_at{-1, -1};

void pressed_cb(lv_event_t*)
{
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &press_at);
}

void clicked_cb(lv_event_t*)
{
    if (!B || !sea_obj || press_at.x < 0) return;
    if (over()) {                                         // the end: look at both seas
        V.page ^= 1;
        changed();
        return;
    }
    if (V.cover || V.result) return;
    lv_area_t a;
    lv_obj_get_coords(sea_obj, &a);
    const int x = press_at.x - a.x1 - (numbers_left ? lab : 0), y = press_at.y - a.y1 - lab;
    if (x < 0 || y < 0 || x >= kN * cell || y >= kN * cell) return;
    const int c = (y / cell) * kN + x / cell;
    if (placing()) {
        // Manual: tap one end, then a lit square the way it points (silent)
        if (!manual || V.ready_pending || V.n >= kShips || overlay_open()) return;
        const int d = aim_of(c);
        if (d >= 0) {
            place_ship(V.preview, V.n, aimed(V.anchor, d));
            ++V.n;
            V.anchor = -1;
        } else if (c == V.anchor) {
            V.anchor = -1;
        } else if (can_anchor(c)) {
            V.anchor = c;
        } else {
            sound(Sound::Error);                          // no room for this ship there
            return;
        }
        changed();
        return;
    }
    if (V.page != 0 || B->setup() || !match::human_may_move()) return;
    if (!B->can_play(uint32_t(c))) { sound(Sound::Error); return; }
    match::human_move(c);
}

// ---- Options ------------------------------------------------------------------------------------
enum Opt : intptr_t { kPlacement = 1, kOptBack };

void opt_cb(lv_event_t* e)
{
    const intptr_t id = intptr_t(lv_event_get_user_data(e));
    if (id == kOptBack) { match::open_menu(); return; }
    if (id == kPlacement) {
        manual = lv_obj_has_state(lv_event_get_target_obj(e), LV_STATE_CHECKED);
        save_options();
        if (B && placing() && !V.ready_pending) start_placing();   // at once, for the fleet being placed
        options_open();
    }
}

void options_open()
{
    overlay_begin("Options");
    overlay_text("Ship Placement", false);
    overlay_choice("Random", "Manual", manual, opt_cb, kPlacement);
    overlay_text(manual ? "Manual: tap where a ship's end goes, then a lit square the way it points. Biggest ship first."
                        : "Random: Shuffle until you like your fleet.", true);
    overlay_back(opt_cb, kOptBack);
}

// ---- Save ---------------------------------------------------------------------------------------
constexpr size_t kSaveBytes = Board::kSaveBytes + match::kStateBytes;
uint32_t last_save_ms = 0;
int      saved_moves = -1;

void save()
{
    if (!B || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = B->serialize(buf, sizeof buf);
    match::save_state(buf + n, sizeof buf - n);
    shell().save_game(kId, buf, sizeof buf);
}

// The board's part of the save, then the controller's (version 1's board part is shorter)
bool load_image(const uint8_t* buf, size_t n, Board& b, match::State* st)
{
    const size_t board = n == kSaveBytes ? Board::kSaveBytes
                       : n == Board::kSaveBytesV1 + match::kStateBytes ? Board::kSaveBytesV1 : 0;
    if (!board || !b.deserialize(buf, board)) return false;
    return st ? match::read_state(buf + board, match::kStateBytes, *st) : match::load_state(buf + board, match::kStateBytes);
}

bool load(Board& b)
{
    uint8_t buf[kSaveBytes];
    const Shell& H = shell();
    const size_t n = H.load_game ? H.load_game(kId, buf, sizeof buf) : 0;
    return load_image(buf, n, b, nullptr);
}

lv_obj_t* make_row_key(lv_obj_t* scr, int w, int h, int x, int y)
{
    lv_obj_t* k = make_key(scr, w, h, key_row_cb, 0);
    key_label(k, "", menu_font());
    lv_obj_set_pos(k, x, y);
    return k;
}

void build()
{
    match::attach(make_game());
    kit::screen_begin();
    int top, bottom;
    match::build_chrome(&top, &bottom);
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4;
    const int kh = menu_btn_h();
    // The sea: as large as fits, labels included
    lab = m.large ? 18 : 12;
    const int margin = m.large ? 6 : 3;
    const int W = m.w - 2 * margin - lab, H = bottom - top - 2 * margin - lab;
    cell = (W < H ? W : H) / kN;
    numbers_left = right_handed();
    const int bw = lab + kN * cell + 1, bh = lab + kN * cell + 1;
    sea_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(sea_obj);
    lv_obj_set_size(sea_obj, bw, bh);
    lv_obj_set_pos(sea_obj, (m.w - bw) / 2, top + (bottom - top - bh) / 2);
    lv_obj_set_clickable(sea_obj, true);
    lv_obj_set_scrollable(sea_obj, false);
    lv_obj_add_event_cb(sea_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(sea_obj, pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(sea_obj, clicked_cb, LV_EVENT_CLICKED, nullptr);
    // Pass-and-play's cover text, over the sea
    cover_l = lv_label_create(scr);
    lv_obj_set_width(cover_l, kN * cell - 16);
    lv_label_set_long_mode(cover_l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(cover_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(cover_l, m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cover_l, pal().stone_light, 0);
    lv_obj_align_to(cover_l, sea_obj, LV_ALIGN_CENTER, numbers_left ? lab / 2 : -lab / 2, lab / 2);
    lv_obj_set_hidden(cover_l, true);
    // The keys, in the Play Again row
    const int ky = bottom + pad;
    const int half = (m.w - 3 * pad) / 2;
    key_a = make_row_key(scr, half, kh, pad, ky);
    key_b = make_row_key(scr, half, kh, pad + half + pad, ky);
    key_c = make_row_key(scr, m.w - 2 * pad, kh, pad, ky);
    update_keys();
    match::restart_view();
    update_keys();
}

// ---- Registry entry -------------------------------------------------------------------------
void open()
{
    B = new (std::nothrow) Board();
    if (!B) { app_go_home(); return; }
    if (!load(*B)) { *B = Board{}; match::state() = match::State{}; }
    load_options();
    V = View{};
    V.cover = pnp() && !over();
    V.viewer = B->turn();
    start_placing();
    saved_moves = B->moves;
    build();
}

void close()
{
    if (!B) return;
    match::detach();
    save();
    match::closed();
    sea_obj = cover_l = key_a = key_b = key_c = nullptr;
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    match::tick(now);
    if (!B) return;
    // Ready was tapped before this side's turn to set up: it goes now
    if (V.ready_pending && placing() && B->turn() == viewer() && match::human_may_move()) {
        V.ready_pending = !send_fleet();
        changed();
    }
    if (B->moves != saved_moves || kit::save_due(now, last_save_ms, match::state().seconds)) {
        saved_moves = B->moves;
        last_save_ms = now;
        save();
    }
}

void restyle()
{
    if (!B) return;
    match::detach();
    build();
}

bool summary(char* buf, size_t cap)
{
    if (B) { match::summary(buf, cap); return true; }
    uint8_t img[kSaveBytes];
    const Shell& H = shell();
    Board* b = new (std::nothrow) Board();
    if (!b) return false;
    match::State st;
    bool ok = H.load_game && load_image(img, H.load_game(kId, img, sizeof img), *b, &st);
    if (ok) match::describe(st, b->result(), b->moves, kSides, buf, cap);
    delete b;
    return ok;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int n = 5, c = s / n, x0 = a.x1 + (s - n * c) / 2, y0 = a.y1 + (s - n * c) / 2;
    kit::fill_rect(layer, x0, y0, x0 + n * c, y0 + n * c, lv_color_darken(P.frame, 70), c / 3);
    for (int i = 0; i < n * n; ++i)
        kit::fill_rect(layer, x0 + (i % n) * c + 1, y0 + (i / n) * c + 1, x0 + (i % n + 1) * c - 1,
                       y0 + (i / n + 1) * c - 1, P.frame);
    const lv_color_t ship_c = lv_color_mix(P.stone_light, P.stone_dark, 120);
    kit::fill_rect(layer, x0 + c + 2, y0 + 2 * c + 2, x0 + 4 * c - 2, y0 + 3 * c - 2, ship_c, (c - 4) / 2);
    kit::fill_rect(layer, x0 + 4 * c + 2, y0 + 2, x0 + 5 * c - 2, y0 + 2 * c - 2, ship_c, (c - 4) / 2);
    kit::fill_circle(layer, x0 + 2 * c + c / 2, y0 + 2 * c + c / 2, c / 3, P.piece_a);
    kit::fill_circle(layer, x0 + c / 2, y0 + 4 * c + c / 2, c / 6, P.stone_light);
    kit::fill_circle(layer, x0 + 3 * c + c / 2, y0 + 4 * c + c / 2, c / 6, P.stone_light);
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
extern const GameOps sunk_ops;
const GameOps sunk_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace sunk_preview {
sunk::Board* board() { return B; }
void ready() { do_key(kReady); }
void undo() { do_key(kUndo); }
void tap(int c)                          // a tap on sea cell c
{
    if (!sea_obj) return;
    lv_area_t a;
    lv_obj_get_coords(sea_obj, &a);
    press_at.x = a.x1 + (numbers_left ? lab : 0) + (c % kN) * cell + cell / 2;
    press_at.y = a.y1 + lab + (c / kN) * cell + cell / 2;
    clicked_cb(nullptr);
}
void options() { options_open(); }
void cover_ready() { if (V.cover) { V.cover = false; V.viewer = B->turn(); V.page = 0; changed(); } }
void pass() { if (V.result) { V.result = false; V.cover = true; changed(); } }
void page(int p) { V.page = p; changed(); }
} // namespace sunk_preview
#endif
