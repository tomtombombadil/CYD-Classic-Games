// You Sank My CYD!: screen and registry entry. The rules live in
// sank_core.*; turns, the computer, menus, stats and wireless play come
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
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "sank_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace sank;
using namespace ui;

constexpr const char* kId = "sank";
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
bool manual = true;                 // Options -> Ship Placement (saved in /games/sank_opt.bin)

int cell = 20, lab = 12;            // cell size, label strip (px)

// A shot on screen (Tom, 2026-10-05: "the anticipation of the hit or miss"):
// the shell falls on the target with a whistle, then a splash or an
// explosion and a big "B5 MISS!" / "C6 HIT!" that stays a while. Then the
// view turns to the sea the next shot will land in. The board already holds
// the result; this is only how it is shown.
// (Tom, 2026-10-05: a third faster than the first 900 / 1500 / 1900 / 2600 / 900)
constexpr uint32_t kFallMs = 600, kMissHoldMs = 1000, kHitHoldMs = 1250, kSunkHoldMs = 1750;
constexpr uint32_t kAimMs = 600;      // the other side "aims" with your fleet on screen
struct ShotAnim {
    bool     on = false, impact = false, final = false, hit = false, sunk = false;
    int      cell = -1, shooter = -1, ship = -1;
    uint32_t start = 0;
} SA;
uint32_t   now_ms = 0, aim_until = 0;
lv_timer_t* anim_timer = nullptr;
uint32_t hold_ms() { return SA.sunk ? kSunkHoldMs : SA.hit ? kHitHoldMs : kMissHoldMs; }
bool aiming() { return int32_t(aim_until - now_ms) > 0; }
bool busy() { return SA.on || aiming(); }
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

// Manual: how far the ship can be aimed from the anchor toward direction d
// (0 up, 1 right, 2 down, 3 left): the squares to tap, up to the ship's
// length - 1, or fewer near the edge (Tom, 2026-10-05: aiming at an edge
// slides the ship in so it fits, its end against the edge)
int ray(int anchor, int d)
{
    const int r = anchor / kN, c = anchor % kN, len = kLen[V.n];
    const int to_edge = d == 0 ? r : d == 1 ? kN - 1 - c : d == 2 ? kN - 1 - r : c;
    return to_edge < len - 1 ? to_edge : len - 1;
}

// The ship placed from the anchor toward d; 0xFFFF when it doesn't fit
uint32_t aimed(int anchor, int d)
{
    if (ray(anchor, d) == 0) return 0xFFFF;
    const int len = kLen[V.n], r = anchor / kN, c = anchor % kN;
    int tr = r, tc = c;                                    // top / left cell
    if (d == 0) tr = r - len + 1 < 0 ? 0 : r - len + 1;
    if (d == 1) tc = c + len - 1 > kN - 1 ? kN - len : c;
    if (d == 2) tr = r + len - 1 > kN - 1 ? kN - len : r;
    if (d == 3) tc = c - len + 1 < 0 ? 0 : c - len + 1;
    const uint32_t key = ship_key(tr * kN + tc, d == 0 || d == 2);
    return ship_fits(V.preview, V.n, key) ? key : 0xFFFF;
}

// Manual: which way (0-3) a tap on cell `t` aims the ship from the anchor, -1 none
int aim_of(int t)
{
    if (V.anchor < 0 || t == V.anchor) return -1;
    const int ar = V.anchor / kN, ac = V.anchor % kN, r = t / kN, c = t % kN;
    int d = -1, k = 0;
    if (c == ac && r < ar) { d = 0; k = ar - r; }
    else if (r == ar && c > ac) { d = 1; k = c - ac; }
    else if (c == ac && r > ar) { d = 2; k = r - ar; }
    else if (r == ar && c < ac) { d = 3; k = ac - c; }
    return d >= 0 && k <= ray(V.anchor, d) && aimed(V.anchor, d) != 0xFFFF ? d : -1;
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
    if (H.load_game && H.load_game("sank_opt", b, sizeof b) == sizeof b && memcmp(b, "SKO1", 4) == 0)
        manual = !(b[4] & 1);
}

void save_options()
{
    uint8_t b[5] = {'S', 'K', 'O', '1', uint8_t(manual ? 0 : 1)};
    if (shell().save_game) shell().save_game("sank_opt", b, sizeof b);
}

void coord(int c, char* buf, size_t cap) { snprintf(buf, cap, "%c%u", 'A' + c % kN, unsigned(c) / kN % kN + 1); }

void redraw() { if (sea_obj) lv_obj_invalidate(sea_obj); }
void update_keys();

// ---- Rules for the controller ---------------------------------------------------------------
int result() { return B->result(); }
int turn()   { return B->turn(); }
int moves()  { return B->moves; }

// Each animation frame redraws only the squares round the shot (the whole sea
// with its ships was a heavy frame on the device), and nothing once the
// splash / explosion has grown - the banner is drawn with the impact frame.
void anim_tick_cb(lv_timer_t*)
{
    if (!sea_obj || !SA.on) return;
    const uint32_t t = now_ms - SA.start;
    if (SA.impact && t > kFallMs + 340) return;          // grown: it stays as it is
    lv_area_t a;
    lv_obj_get_coords(sea_obj, &a);
    const int x0 = a.x1 + (numbers_left ? lab : 0), y0 = a.y1 + lab;
    const int X = x0 + (SA.cell % kN) * cell + cell / 2, Y = y0 + (SA.cell / kN) * cell + cell / 2;
    const int r = cell + cell / 2 + 2;
    lv_area_t z{int32_t(X - r), int32_t(Y - r), int32_t(X + r), int32_t(Y + r)};
    lv_obj_invalidate_area(sea_obj, &z);
}

void anim_timer_on(bool on)
{
    if (on && !anim_timer) anim_timer = lv_timer_create(anim_tick_cb, 40, nullptr);
    if (!on && anim_timer) { lv_timer_delete(anim_timer); anim_timer = nullptr; }
}

// The next shot's sea: yours to fire at, or your fleet while they aim
void show_next_turn()
{
    if (over() || pnp() || B->setup()) return;
    if (B->turn() == match::my_side()) V.page = 0;
    else { V.page = 1; aim_until = now_ms + kAimMs; }
}

void play(int m)
{
    const bool shot = !B->setup();
    const int mover = B->turn();
    if (!B->play(uint32_t(m))) return;
    if (shot) {
        SA = ShotAnim{};
        SA.on = true;
        SA.cell = m;
        SA.shooter = mover;
        SA.ship = B->fleet[mover ^ 1].at[m] - 1;
        SA.hit = SA.ship >= 0;
        SA.sunk = SA.hit && B->sunk(mover ^ 1, SA.ship);
        SA.final = over();
        SA.start = SA.final ? now_ms - kFallMs : now_ms;   // the last shot lands at once (the win sounds)
        if (!pnp()) V.page = mover == match::my_side() ? 0 : 1;
        anim_timer_on(true);
    } else if (pnp() && !over() && B->turn() != mover) {
        V.cover = true;                         // the fleet is set: pass at once
    }
    if (B->moves == kSetupPlies) { V.page = 0; show_next_turn(); }    // both fleets set: fire away
    update_keys();
    redraw();
}

// Every tick: the shot's splash or boom, its end, the turn to the next sea
void anim_step()
{
    if (!SA.on) return;
    const uint32_t t = now_ms - SA.start;
    if (!SA.impact && t >= kFallMs) {
        SA.impact = true;
        if (!SA.final) sound(SA.hit ? Sound::Boom : Sound::Splash);
        redraw();
        match::refresh();
    }
    if (t < kFallMs + hold_ms()) return;
    SA.on = false;
    anim_timer_on(false);
    if (!over()) {
        if (pnp()) V.result = true;             // seen where it landed: now pass
        else show_next_turn();
    }
    update_keys();
    redraw();
    match::refresh();
}

void reset()
{
    *B = Board{};
    SA = ShotAnim{};
    aim_until = now_ms;
    anim_timer_on(false);
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
    (void)by_other;
    sound(Sound::Whistle);                      // the splash or boom comes when it lands
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (SA.on && !SA.impact) {                  // in flight: no telling yet
        char at[8];
        coord(SA.cell, at, sizeof at);
        if (pnp()) snprintf(buf, cap, "%s fires at %s...", SA.shooter == 0 ? kSides.side1 : kSides.side2, at);
        else if (SA.shooter == match::my_side()) snprintf(buf, cap, "Firing at %s...", at);
        else snprintf(buf, cap, "%s fires at %s...", match::opponent_name() ? match::opponent_name() : "They", at);
        return;
    }
    if (aiming() && !pnp() && match::opponent_name()) {
        snprintf(buf, cap, "%s is aiming...", match::opponent_name());
        return;
    }
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
    match::Game g{kId, "You Sank My CYD!", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.move_sound = move_sound;
    g.busy = busy;
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

// ---- Ships, drawn from above --------------------------------------------------------------
// A ship's own frame: u runs along it (0 at the stern, the bow at the far
// end), v across (0..cell); `down` turns it to run down the sea.
struct Frame { lv_layer_t* layer; int x, y; bool down; };
void map_pt(const Frame& f, int u, int v, int32_t* x, int32_t* y) { *x = f.x + (f.down ? v : u); *y = f.y + (f.down ? u : v); }

void lrect(const Frame& f, int u1, int v1, int u2, int v2, lv_color_t c, int radius = 0)
{
    int32_t x1, y1, x2, y2;
    map_pt(f, u1, v1, &x1, &y1);
    map_pt(f, u2, v2, &x2, &y2);
    kit::fill_rect(f.layer, x1 < x2 ? x1 : x2, y1 < y2 ? y1 : y2, x1 < x2 ? x2 : x1, y1 < y2 ? y2 : y1, c, radius);
}

void ltri(const Frame& f, int u1, int v1, int u2, int v2, int u3, int v3, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    int32_t x, y;
    map_pt(f, u1, v1, &x, &y); d.p[0].x = x; d.p[0].y = y;
    map_pt(f, u2, v2, &x, &y); d.p[1].x = x; d.p[1].y = y;
    map_pt(f, u3, v3, &x, &y); d.p[2].x = x; d.p[2].y = y;
    lv_draw_triangle(f.layer, &d);
}

void lcircle(const Frame& f, int u, int v, int r, lv_color_t c)
{
    int32_t x, y;
    map_pt(f, u, v, &x, &y);
    kit::fill_circle(f.layer, x, y, r, c);
}

void lline(const Frame& f, int u1, int v1, int u2, int v2, int w, lv_color_t c)
{
    int32_t x1, y1, x2, y2;
    map_pt(f, u1, v1, &x1, &y1);
    map_pt(f, u2, v2, &x2, &y2);
    kit::line(f.layer, x1, y1, x2, y2, w, c);
}

struct ShipPaint { lv_color_t edge, hull, deck, top, dark, light; };

// how: 0 afloat, 1 sunk (a dark red wreck), 2 shown at the end of the game (faded)
ShipPaint ship_paint(int how)
{
    const Palette& P = pal();
    auto g = [&](int k) { return lv_color_mix(P.stone_light, P.stone_dark, uint8_t(k)); };   // grey ramp
    ShipPaint s{g(30), g(100), g(145), g(185), g(20), g(225)};
    if (how == 1) {
        s.edge = P.stone_dark;
        s.hull = lv_color_mix(P.piece_a, P.stone_dark, 95);
        s.deck = lv_color_mix(P.piece_a, P.stone_dark, 135);
        s.top  = lv_color_mix(P.piece_a, P.stone_light, 150);
        s.dark = P.stone_dark;
        s.light = lv_color_mix(P.piece_a, P.stone_light, 90);
    } else if (how == 2) {
        lv_color_t* c[6] = {&s.edge, &s.hull, &s.deck, &s.top, &s.dark, &s.light};
        for (lv_color_t* x : c) *x = lv_color_mix(*x, P.frame, 140);
    }
    return s;
}

// The hull's half-width along it, 0..1 (t: 0 stern .. 1 bow)
// kind 0 carrier, 1 warship, 2 submarine
float hull_half(int kind, float t)
{
    if (kind == 2) {                                   // rounded nose, long tapered tail
        if (t > 0.78f) { const float x = (t - 0.78f) / 0.22f; return sqrtf(1 - x * x > 0 ? 1 - x * x : 0); }
        if (t < 0.32f) return 0.3f + 0.7f * powf(t / 0.32f, 0.7f);
        return 1.0f;
    }
    const float stern = kind == 0 ? 0.03f : 0.06f, bow = kind == 0 ? 0.80f : 0.58f;
    if (t < stern) return 0.78f + 0.22f * t / stern;   // a rounded-off stern
    if (t <= bow) return 1.0f;
    return powf((1 - t) / (1 - bow), 0.55f);           // a curved, pointed bow
}

void hull(const Frame& f, int u0, int u1, int mid, float half, int kind, lv_color_t c)
{
    // First a solid core of rectangles (no anti-aliasing), then the smooth
    // outline as triangle strips over it: the strips' shared edges blend
    // into the same colour underneath, so no seams show and the edges stay smooth.
    constexpr int kSeg = 14;
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    for (int pass = 0; pass < 2; ++pass) {
        int pu = u0;
        float ph = half * hull_half(kind, 0);
        for (int i = 1; i <= kSeg; ++i) {
            const float t = float(i) / kSeg;
            const int u = u0 + int(lroundf((u1 - u0) * t));
            const float h = half * hull_half(kind, t);
            if (pass == 0) {
                const int lo = int(ph < h ? ph : h) - 1;
                if (lo > 0) lrect(f, pu, mid - lo, u, mid + lo, c);
            } else {
                int32_t x, y;
                map_pt(f, pu, mid - int(lroundf(ph)), &x, &y); d.p[0].x = x; d.p[0].y = y;
                map_pt(f, pu, mid + int(lroundf(ph)), &x, &y); d.p[1].x = x; d.p[1].y = y;
                map_pt(f, u, mid - int(lroundf(h)), &x, &y);   d.p[2].x = x; d.p[2].y = y;
                lv_draw_triangle(f.layer, &d);
                map_pt(f, pu, mid + int(lroundf(ph)), &x, &y); d.p[0].x = x; d.p[0].y = y;
                map_pt(f, u, mid + int(lroundf(h)), &x, &y);   d.p[1].x = x; d.p[1].y = y;
                map_pt(f, u, mid - int(lroundf(h)), &x, &y);   d.p[2].x = x; d.p[2].y = y;
                lv_draw_triangle(f.layer, &d);
            }
            pu = u;
            ph = h;
        }
    }
}

// A gun turret at u with twin barrels toward the bow (`fore`) or the stern
void turret(const Frame& f, int u, int mid, int r, bool fore, const ShipPaint& c)
{
    const int len = r * 2 + r / 2, off = r * 2 / 5 > 0 ? r * 2 / 5 : 1, w = r >= 5 ? 2 : 1;
    const int tip = fore ? u + len : u - len;
    lline(f, u, mid - off, tip, mid - off, w, c.dark);
    lline(f, u, mid + off, tip, mid + off, w, c.dark);
    lcircle(f, u, mid, r, c.dark);
    lcircle(f, u, mid, r - 1 > 1 ? r - 1 : 1, c.top);
    lcircle(f, u + (fore ? -r / 3 : r / 3), mid, r / 3 > 1 ? r / 3 : 1, c.deck);    // the hatch, aft side
}

// The bridge: a block with a lighter top deck and a dark window line toward the bow; a funnel aft
void bridge(const Frame& f, int ua, int ub, int mid, int half, const ShipPaint& c)
{
    lrect(f, ua - 1, mid - half - 1, ub + 1, mid + half + 1, c.dark, 3);
    lrect(f, ua, mid - half, ub, mid + half, c.top, 2);
    const int inner = half * 6 / 10;
    lrect(f, ua + (ub - ua) / 4, mid - inner, ub - (ub - ua) / 5, mid + inner, c.light, 2);
    lrect(f, ub - (ub - ua) / 5 - 1, mid - inner, ub - (ub - ua) / 5 + 1, mid + inner, c.dark);
}

void funnel(const Frame& f, int u, int mid, int r, const ShipPaint& c)
{
    lcircle(f, u, mid, r + 1, c.dark);
    lcircle(f, u, mid, r, c.deck);
    lcircle(f, u, mid, r * 6 / 10 > 1 ? r * 6 / 10 : 1, c.dark);
}

void draw_ship(lv_layer_t* layer, int x0, int y0, int i, const Ship& sh, const ShipPaint& c)
{
    const int len = kLen[i];
    const Frame f{layer, x0 + (sh.cell % kN) * cell, y0 + (sh.cell / kN) * cell, sh.down};
    const int L = len * cell, mid = cell / 2;
    const int u0 = cell / 10 + 1, u1 = L - cell / 10 - 1;          // stern, bow
    const float half = cell * 0.40f;
    const int W = int(half * 2);
    auto at = [&](int pct) { return u0 + (u1 - u0) * pct / 100; };
    if (i == 3) {                                                  // Submarine: dark, slim, the sail amidships
        const float sh_half = half * 0.72f;
        const lv_color_t body = lv_color_mix(c.dark, c.hull, 150), tone = lv_color_mix(c.hull, c.dark, 150);
        // the stern planes and rudder
        ltri(f, at(4), mid - int(sh_half * 1.25f), at(14), mid, at(4), mid + int(sh_half * 1.25f), c.edge);
        hull(f, u0 - 1, u1 + 1, mid, sh_half + 1, 2, c.edge);
        hull(f, u0, u1, mid, sh_half, 2, body);
        lline(f, at(18), mid - int(sh_half * 0.35f), at(86), mid - int(sh_half * 0.35f), 1, tone);   // a sheen
        lrect(f, at(40) - 1, mid - int(sh_half * 0.55f) - 1, at(56) + 1, mid + int(sh_half * 0.55f) + 1, c.edge, 4);
        lrect(f, at(40), mid - int(sh_half * 0.55f), at(56), mid + int(sh_half * 0.55f), tone, 3);
        lline(f, at(44), mid - 1, at(52), mid - 1, 1, c.light);                                   // periscope
        return;
    }
    if (i == 0) {                                                  // Carrier: a broad flight deck, the island to one side
        hull(f, u0 - 1, u1 + 1, mid, half + 1, 0, c.edge);
        hull(f, u0, u1, mid, half, 0, c.hull);
        const lv_color_t tarmac = lv_color_mix(c.dark, c.hull, 120);
        hull(f, u0 + 2, u1 - 3, mid, half - 2, 0, tarmac);
        for (int u = at(6); u < at(80); u += cell / 2 + 1)                 // the centre line
            lline(f, u, mid, u + cell / 4, mid, 1, c.light);
        lline(f, at(4), mid + W / 4, at(46), mid - W / 3, 1, c.deck);     // the angled landing line
        lrect(f, at(55) - 1, mid - int(half) - 1, at(72) + 1, mid - int(half) + W * 30 / 100 + 1, c.dark, 2);
        lrect(f, at(55), mid - int(half), at(72), mid - int(half) + W * 30 / 100, c.top, 2);
        lrect(f, at(66), mid - int(half) + 1, at(68), mid - int(half) + W * 30 / 100 - 1, c.dark);
        return;
    }
    // Battleship, Cruiser, Destroyer: grey hull, lighter deck, bridge, funnel, guns
    hull(f, u0 - 1, u1 + 1, mid, half + 1, 1, c.edge);
    hull(f, u0, u1, mid, half, 1, c.hull);
    hull(f, u0 + 2, u1 - 4, mid, half - 2, 1, c.deck);
    const int r = int(half * 0.55f) > 2 ? int(half * 0.55f) : 2;
    const int bh = int(half * 0.62f);
    if (i == 1) {                                                  // Battleship: three big turrets
        funnel(f, at(32), mid, r * 7 / 10, c);
        bridge(f, at(38), at(54), mid, bh, c);
        turret(f, at(16), mid, r, false, c);
        turret(f, at(64), mid, r, true, c);
        turret(f, at(78), mid, r, true, c);
    } else if (i == 2) {                                           // Cruiser: two
        funnel(f, at(30), mid, r * 6 / 10, c);
        bridge(f, at(37), at(55), mid, bh, c);
        turret(f, at(16), mid, r * 9 / 10, false, c);
        turret(f, at(68), mid, r * 9 / 10, true, c);
    } else {                                                       // Destroyer: a bridge and one gun
        funnel(f, at(22), mid, r * 5 / 10, c);
        bridge(f, at(30), at(52), mid, bh * 9 / 10, c);
        turret(f, at(70), mid, r * 8 / 10, true, c);
    }
}

// A star of `n` points (explosions, hit marks)
void burst(lv_layer_t* layer, int cx, int cy, int r_out, int r_in, int n, float turn, lv_color_t c)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = c;
    d.opa = LV_OPA_COVER;
    for (int k = 0; k < n; ++k) {
        const float a0 = turn + 6.2831853f * k / n, a1 = turn + 6.2831853f * (k + 0.5f) / n, a2 = turn + 6.2831853f * (k + 1) / n;
        // the point, and the inner corners either side of it
        d.p[0].x = cx + int32_t(lroundf(cosf(a1) * r_out)); d.p[0].y = cy + int32_t(lroundf(sinf(a1) * r_out));
        d.p[1].x = cx + int32_t(lroundf(cosf(a0) * r_in));  d.p[1].y = cy + int32_t(lroundf(sinf(a0) * r_in));
        d.p[2].x = cx + int32_t(lroundf(cosf(a2) * r_in));  d.p[2].y = cy + int32_t(lroundf(sinf(a2) * r_in));
        lv_draw_triangle(layer, &d);
    }
    kit::fill_circle(layer, cx, cy, r_in, c);
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
    kit::fill_rect(layer, x0, y0, x0 + sea, y0 + sea, deep);
    const bool covered = V.cover && !over();
    for (int c = 0; c < kCells; ++c) {
        const int x = x0 + (c % kN) * cell, y = y0 + (c / kN) * cell;
        kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, covered ? deep : water);
    }
    if (covered) return;
    const int me = viewer();
    const int pr = cell / 7 > 2 ? cell / 7 : 2;
    auto cx = [&](int c) { return x0 + (c % kN) * cell + cell / 2; };
    auto cy = [&](int c) { return y0 + (c / kN) * cell + cell / 2; };
    // A hit: a small red burst with a gold heart
    auto hit_mark = [&](int c, bool small) {
        const int r = small ? cell * 30 / 100 : cell * 40 / 100;
        burst(layer, cx(c), cy(c), r, r / 2, 7, 0.3f, P.piece_a);
        kit::fill_circle(layer, cx(c), cy(c), r / 3 > 1 ? r / 3 : 1, P.piece_b);
    };
    auto miss_mark = [&](int c) {
        kit::fill_circle(layer, cx(c), cy(c), pr + 1, lv_color_mix(P.stone_dark, water, 60));
        kit::fill_circle(layer, cx(c), cy(c), pr, P.stone_light);
    };
    if (placing()) {                                     // the fleet being placed
        const int shown = manual ? V.n : kShips;
        for (int i = 0; i < shown; ++i) draw_ship(layer, x0, y0, i, V.preview.ship[i], ship_paint(0));
        if (manual && V.anchor >= 0 && V.n < kShips) {
            // The squares the ship can point to light up; the anchor is gold
            const lv_color_t lit = lv_color_mix(P.lit, water, 225);     // strong: TN panels wash out pale tints
            static const int kDr[4] = {-1, 0, 1, 0}, kDc[4] = {0, 1, 0, -1};
            for (int d = 0; d < 4; ++d) {
                if (aimed(V.anchor, d) == 0xFFFF) continue;
                for (int k = 1; k <= ray(V.anchor, d); ++k) {     // the squares to tap that way
                    const int c = (V.anchor / kN + k * kDr[d]) * kN + V.anchor % kN + k * kDc[d];
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
    // A shot in flight here: its result isn't shown until it lands
    const int sea_side = own ? me : me ^ 1;             // whose ships are in this sea
    const bool anim_here = SA.on && (SA.shooter ^ 1) == sea_side;
    const bool pending = anim_here && !SA.impact;
    const int last = B->last[sea_side ^ 1];
    if (own) {                                           // my ships and the other side's shots
        const Fleet& f = B->fleet[me];
        for (int i = 0; i < B->placed(me); ++i) {
            const bool sunk = B->sunk(me, i) && !(pending && SA.ship == i);
            draw_ship(layer, x0, y0, i, f.ship[i], ship_paint(sunk ? 1 : 0));
        }
        for (int c = 0; c < kCells; ++c) {
            if (!B->shot[me ^ 1][c] || (pending && c == SA.cell)) continue;
            if (f.at[c]) hit_mark(c, B->sunk(me, f.at[c] - 1) && !(pending && SA.ship == f.at[c] - 1));
            else miss_mark(c);
        }
    } else {
        // Their waters: what I know; sunk ships show; at the end every ship does
        const Fleet& f = B->fleet[me ^ 1];
        for (int i = 0; i < kShips; ++i) {
            if (B->sunk(me ^ 1, i) && !(pending && SA.ship == i)) draw_ship(layer, x0, y0, i, f.ship[i], ship_paint(1));
            else if (over() && !B->sunk(me ^ 1, i)) draw_ship(layer, x0, y0, i, f.ship[i], ship_paint(2));
        }
        for (int c = 0; c < kCells; ++c) {
            if (pending && c == SA.cell) continue;
            Known k = B->known(me, c);
            if (k == kSunk && pending && f.at[c] - 1 == SA.ship) k = kHit;      // not sunk until it lands
            switch (k) {
                case kMiss: miss_mark(c); break;
                case kHit:  hit_mark(c, false); break;
                case kSunk: hit_mark(c, true); break;
                default: break;
            }
        }
    }
    if (last >= 0 && B->moves > kSetupPlies && !anim_here)
        frame_rect(layer, cx(last) - cell / 2, cy(last) - cell / 2, cx(last) + cell / 2, cy(last) + cell / 2, 2, P.piece_b, 2);
    if (!anim_here) return;
    // ---- The shot
    const uint32_t t = now_ms - SA.start;
    const int X = cx(SA.cell), Y = cy(SA.cell);
    if (!SA.impact) {
        // The target in the sights, and the shell coming down on it (shrinking as it falls)
        const float fall = t >= kFallMs ? 1.0f : float(t) / kFallMs;
        kit::ring(layer, X, Y, cell / 2 + 1, 2, P.piece_b);
        kit::line(layer, X - cell, Y, X - cell / 2, Y, 2, P.piece_b);
        kit::line(layer, X + cell / 2, Y, X + cell, Y, 2, P.piece_b);
        kit::line(layer, X, Y - cell, X, Y - cell / 2, 2, P.piece_b);
        kit::line(layer, X, Y + cell / 2, X, Y + cell, 2, P.piece_b);
        const int r = int(cell * 0.16f + cell * 0.9f * (1 - fall));
        kit::fill_circle(layer, X, Y, r + 1, P.stone_light);
        kit::fill_circle(layer, X, Y, r, P.stone_dark);
        return;
    }
    const uint32_t ti = t - kFallMs;
    const float grow = ti >= 300 ? 1.0f : float(ti) / 300;
    if (SA.hit) {                                        // the explosion
        const int ro = int(cell * (0.4f + 0.75f * grow));
        burst(layer, X, Y, ro, ro * 55 / 100, 9, 0.0f, P.piece_a);
        burst(layer, X, Y, ro * 65 / 100, ro * 35 / 100, 7, 0.4f, P.piece_b);
        kit::fill_circle(layer, X, Y, ro / 5 > 1 ? ro / 5 : 1, P.stone_light);
    } else {                                             // the splash
        const lv_color_t foam = lv_color_mix(P.stone_light, water, 200);
        kit::fill_circle(layer, X, Y, int(cell * (0.25f + 0.15f * grow)), foam);
        kit::ring(layer, X, Y, int(cell * (0.35f + 0.6f * grow)), 2, P.stone_light);
        for (int k = 0; k < 8; ++k) {
            const float a = 6.2831853f * k / 8 + 0.2f;
            const float d = cell * (0.4f + 0.55f * grow);
            kit::fill_circle(layer, X + int(cosf(a) * d), Y + int(sinf(a) * d), cell / 12 > 1 ? cell / 12 : 1, foam);
        }
    }
    // The banner: "C6 HIT!" (and a sunk ship, or who fired), clear of the shot
    char at[8], l1[24], l2[48] = "";
    coord(SA.cell, at, sizeof at);
    snprintf(l1, sizeof l1, "%s %s", at, SA.hit ? "HIT!" : "MISS!");
    const bool mine = SA.shooter == me;
    if (SA.sunk) {
        if (pnp()) snprintf(l2, sizeof l2, "%s's %s sank!", SA.shooter == 0 ? kSides.side2 : kSides.side1, ship_name(SA.ship));
        else if (mine) snprintf(l2, sizeof l2, "You sank their %s!", ship_name(SA.ship));
        else snprintf(l2, sizeof l2, "Your %s sank!", ship_name(SA.ship));
    } else if (!mine && !pnp() && match::opponent_name()) {
        snprintf(l2, sizeof l2, "%s fired", match::opponent_name());
    }
    const bool large = metrics().large;
    const lv_font_t* f1 = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    const lv_font_t* f2 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int h1 = lv_font_get_line_height(f1), h2 = l2[0] ? lv_font_get_line_height(f2) : 0;
    const int bh = h1 + h2 + (large ? 14 : 8), bw = sea - 2 * cell;
    const int bx = x0 + cell, by = SA.cell / kN >= kN / 2 ? y0 + cell : y0 + sea - cell - bh;
    kit::fill_rect(layer, bx - 2, by - 2, bx + bw + 1, by + bh + 1, SA.hit ? P.piece_a : P.stone_dark, 8);
    kit::fill_rect(layer, bx, by, bx + bw - 1, by + bh - 1, P.cell, 7);
    kit::text(layer, l1, f1, SA.hit ? P.piece_a : P.frame, bx, by + (large ? 6 : 3), bw, h1);
    if (l2[0]) kit::text(layer, l2, f2, P.ink, bx, by + (large ? 6 : 3) + h1, bw, h2);
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
    if (V.page != 0 || B->setup() || busy() || !match::human_may_move()) return;
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
    anim_timer_on(false);
    SA = ShotAnim{};
    aim_until = now_ms;
    save();
    match::closed();
    sea_obj = cover_l = key_a = key_b = key_c = nullptr;
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    now_ms = now;
    match::tick(now);
    if (!B) return;
    anim_step();
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
extern const GameOps sank_ops;
const GameOps sank_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace sank_preview {
sank::Board* board() { return B; }
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
bool idle() { return !busy(); }
bool shooting() { return SA.on; }
void cover_ready() { if (V.cover) { V.cover = false; V.viewer = B->turn(); V.page = 0; changed(); } }
void pass() { if (V.result) { V.result = false; V.cover = true; changed(); } }
void page(int p) { V.page = p; changed(); }
} // namespace sank_preview
#endif
