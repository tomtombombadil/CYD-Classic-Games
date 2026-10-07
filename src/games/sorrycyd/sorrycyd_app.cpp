// Sorry-CYD!: registry entry, save file and screen. Rules and the computer
// players in sorrycyd_core.*.
//
// Screen: the square board (16 x 16 squares, the track round the edge),
// turned so your colour (Red) is at the bottom; each colour's Start
// circle, Safety Zone and Home inside its side; slides drawn in their
// colour along the track. The card just drawn sits in the middle with
// whose card it is. A line under the board says what the card does (on
// your move) or what just happened. One key at the bottom: Draw Card /
// Pass (an 11 you'd rather not use) / Play Again.
//
// Your move: the pawns that can move get a gold ring; tap one and dots
// show where it can go; tap a dot. When only one pawn can move it is
// picked for you. A 7 split: the dot short of 7 moves that pawn, then
// the other pawn goes the rest (tap it if more than one could). Sorry! /
// an 11 switch: the dots are on the pawns you can take the place of.
//
// You play Max (Blue), Zoe (Yellow) and Ada (Green). Sounds: your move
// (Place), theirs (Turn), one of your pawns sent back ("aww"), your pawn
// Home (Hint), the end (Win / Lose). Drawing a card is silent.
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "sorrycyd_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace sorry;
using namespace ui;

constexpr const char* kId = "sorrycyd";
const char* const kNames[kColors] = {"You", "Max", "Zoe", "Ada"};   // vs computer
const char* const kColorNames[kColors] = {"Red", "Blue", "Yellow", "Green"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};
constexpr uint32_t kCpuDrawMs = 650, kCpuShowMs = 900, kAfterMs = 600, kNoMoveMs = 1600;

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint8_t  level = 1;
    uint8_t  people = 1;      // colours 0 .. people-1 are played by people (2+ = pass-and-play)
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 7;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   area = nullptr;
lv_obj_t*   key = nullptr;
int         cell = 14, bx = 0, by = 0, info_y = 0;
uint32_t    now_ms = 0, wait_until = 0, last_save_ms = 0;
bool        waiting = false;
bool        frozen = false;              // the preview stops the clock here
lv_point_t  press_pt{0, 0};
int8_t      picked = -1;                  // your pawn picked
int8_t      split_a = -1, split_to = 0;   // a 7 split: the first part chosen, waiting for the second pawn
char        news[56] = "";

void open_menu();
void update();

bool human(int c) { return S && c < S->people; }
bool pnp() { return S && S->people > 1; }
// A colour's player: "You" / a computer by name, or the colour in pass-and-play
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
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = S->recorded;
    buf[n + 1] = S->level;
    for (int k = 0; k < 4; ++k) buf[n + 2 + k] = uint8_t(S->seconds >> (8 * k));
    buf[n + 6] = S->people;
    shell().save_game(kId, buf, sizeof buf);
}

bool load(State& st)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    if (n != kSaveBytes || !st.g.deserialize(buf, n)) return false;
    const size_t b = Game::kSaveBytes;
    st.recorded = buf[b] ? 1 : 0;
    st.level = buf[b + 1] < 3 ? buf[b + 1] : 1;
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[b + 2 + k]) << (8 * k);
    st.people = buf[b + 6] >= 1 && buf[b + 6] <= kColors ? buf[b + 6] : 1;
    return true;
}

void stats_line(const char* line, void* ctx)
{
    Record r;
    if (parse_line(line, r)) static_cast<Summary*>(ctx)->add(r);
}

int my_place()
{
    const Game& g = S->g;
    if (g.winner == 0) return 1;
    int place = g.winner > 0 ? 2 : 1;
    for (int c = 1; c < kColors; ++c)
        if (c != g.winner && g.progress_sum(c) > g.progress_sum(0)) ++place;
    return place;
}

void record()
{
    Record r;
    r.place = uint8_t(my_place());
    r.home = uint8_t(S->g.home_count(0));
    r.level = S->level;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

bool recordable() { return S && S->people == 1; }      // pass-and-play games aren't recorded

// ---- Geometry ---------------------------------------------------------------------------------
// Board coordinates in squares (0..16), before the view turns: Red's side
// on top. The view turns it half round so Red is at the bottom.
struct Pt { float x, y; };

Pt turn_cw(Pt p, int steps)
{
    for (int k = 0; k < steps; ++k) p = Pt{16.0f - p.y, p.x};
    return p;
}

Pt track_pt(int q)
{
    int x, y;
    if (q < 16)      { x = q; y = 0; }
    else if (q < 31) { x = 15; y = q - 15; }
    else if (q < 46) { x = 45 - q; y = 15; }
    else             { x = 0; y = 60 - q; }
    return Pt{x + 0.5f, y + 0.5f};
}

Pt safe_pt(int c, int k) { return turn_cw(Pt{2.5f, k + 0.5f}, c); }        // k = 1..5
Pt start_pt(int c)       { return turn_cw(Pt{4.5f, 2.65f}, c); }
Pt home_pt(int c)        { return turn_cw(Pt{2.5f, 7.45f}, c); }
constexpr float kCircleR = 1.3f;

Pt slot_pt(Pt centre, int slot)
{
    const float d = 0.42f;
    return Pt{centre.x + ((slot & 1) ? d : -d), centre.y + ((slot & 2) ? d : -d)};
}

// Where pawn p of colour c is drawn
Pt pawn_pt(const Game& g, int c, int p)
{
    const int v = g.pos[c][p];
    if (v == kStart || v == kHome) {
        int slot = 0;
        for (int q = 0; q < p; ++q) slot += g.pos[c][q] == v;
        return slot_pt(v == kStart ? start_pt(c) : home_pt(c), slot);
    }
    if (v >= kSafe0) return safe_pt(c, v - kSafe0 + 1);
    return track_pt(track_square(c, v));
}

// Where progress `to` of colour c is (a target dot)
Pt place_pt(const Game& g, int c, int to)
{
    if (to == kHome) return slot_pt(home_pt(c), g.home_count(c) & 3);
    if (to >= kSafe0) return safe_pt(c, to - kSafe0 + 1);
    return track_pt(track_square(c, to));
}

void to_screen(const lv_area_t& a, Pt p, int* x, int* y)
{
    p = turn_cw(p, 2);
    *x = a.x1 + bx + int(p.x * cell + 0.5f);
    *y = a.y1 + by + int(p.y * cell + 0.5f);
}

// ---- Your move: what can be tapped ------------------------------------------------------------
constexpr int kMaxMoves = 96;
Move  ms[kMaxMoves];
int   ms_n = 0;

bool my_play() { return S && S->g.phase == Phase::Play && human(S->g.turn) && !waiting; }

void refresh_moves() { ms_n = my_play() ? S->g.moves(ms, kMaxMoves) : 0; }

bool pawn_can_move(int p)
{
    for (int i = 0; i < ms_n; ++i) if (ms[i].pawn == p) return true;
    return false;
}

int movable_count()
{
    int n = 0;
    for (int p = 0; p < kPawns; ++p) n += pawn_can_move(p);
    return n;
}

// The target of move i for the picked pawn
Pt target_pt(const Move& m)
{
    const Game& g = S->g;
    if (m.kind == Kind::Switch || m.kind == Kind::Sorry) return pawn_pt(g, m.oc, m.op);
    return place_pt(g, g.turn, m.to);
}

// ---- Drawing ----------------------------------------------------------------------------------
void draw_pawn(lv_layer_t* layer, int x, int y, int r, lv_color_t c)
{
    const Palette& P = pal();
    kit::fill_circle(layer, x, y, r + 1, P.stone_dark);
    kit::fill_circle(layer, x, y, r, c);
    kit::fill_circle(layer, x - r / 3, y - r / 3, r / 3 > 1 ? r / 3 : 1, lv_color_mix(P.stone_light, c, 150));
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
    const int B = 16 * cell;
    const int x0 = a.x1 + bx, y0 = a.y1 + by;
    kit::fill_rect(layer, x0 - 2, y0 - 2, x0 + B + 1, y0 + B + 1, P.stone_dark, 4);
    kit::fill_rect(layer, x0, y0, x0 + B - 1, y0 + B - 1, P.cell, 2);
    auto sq_rect = [&](Pt c, lv_color_t fill) {
        int x, y;
        to_screen(a, c, &x, &y);
        const int h = cell / 2;
        kit::fill_rect(layer, x - h, y - h, x - h + cell - 1, y - h + cell - 1, P.line_thin);
        kit::fill_rect(layer, x - h + 1, y - h + 1, x - h + cell - 2, y - h + cell - 2, fill);
    };
    for (int q = 0; q < kTrack; ++q) sq_rect(track_pt(q), P.cell);
    for (int c = 0; c < kColors; ++c) {
        const lv_color_t col = color_of(c);
        const lv_color_t pale = lv_color_mix(col, P.cell, 90);
        for (int k = 1; k <= 5; ++k) sq_rect(safe_pt(c, k), pale);
        // the two slides on this colour's side
        for (int s = 0; s < 2; ++s) {
            const int q0 = 15 * c + (s ? 9 : 1), len = s ? 4 : 3;
            int xa, ya, xb, yb;
            to_screen(a, track_pt(q0), &xa, &ya);
            to_screen(a, track_pt(q0 + len), &xb, &yb);
            const int w = cell / 4 > 2 ? cell / 4 : 2;
            kit::line(layer, xa, ya, xb, yb, w, col);
            kit::fill_circle(layer, xb, yb, w, col);
            // a triangle at the start pointing along the slide
            lv_draw_triangle_dsc_t d;
            lv_draw_triangle_dsc_init(&d);
            d.color = col;
            d.opa = LV_OPA_COVER;
            const int dx = (xb > xa) - (xb < xa), dy = (yb > ya) - (yb < ya);
            const int t = cell * 4 / 10;
            d.p[0].x = xa + dx * t; d.p[0].y = ya + dy * t;
            d.p[1].x = xa - dx * t / 2 + dy * t; d.p[1].y = ya - dy * t / 2 + dx * t;
            d.p[2].x = xa - dx * t / 2 - dy * t; d.p[2].y = ya - dy * t / 2 - dx * t;
            lv_draw_triangle(layer, &d);
        }
        // Start and Home circles
        int x, y;
        const int r = int(kCircleR * cell);
        to_screen(a, start_pt(c), &x, &y);
        kit::fill_circle(layer, x, y, r, col);
        kit::fill_circle(layer, x, y, r - 2, pale);
        to_screen(a, home_pt(c), &x, &y);
        kit::fill_circle(layer, x, y, r, col);
        kit::fill_circle(layer, x, y, r - 2, lv_color_mix(col, P.cell, 160));
    }
    // The card in the middle
    {
        int cx, cy;
        to_screen(a, Pt{8.0f, 8.0f}, &cx, &cy);
        const int cw = cell * 26 / 10, ch = cell * 36 / 10;
        const int top = cy - ch / 2 - cell / 2;
        if (g.card || g.last.card) {
            const uint8_t k = g.card ? g.card : g.last.card;
            const int who = g.card ? g.turn : (g.last.color >= 0 ? g.last.color : g.turn);
            kit::fill_rect(layer, cx - cw / 2 - 2, top - 2, cx + cw / 2 + 1, top + ch + 1, color_of(who), 6);
            kit::fill_rect(layer, cx - cw / 2, top, cx + cw / 2 - 1, top + ch - 1, g.card ? P.screen : lv_color_mix(P.screen, P.muted, 200), 5);
            char t[8];
            if (k == kSorry) snprintf(t, sizeof t, "Sorry!");
            else snprintf(t, sizeof t, "%d", k);
            const lv_font_t* f = k == kSorry ? (large ? &lv_font_montserrat_14 : &lv_font_montserrat_12)
                                             : (large ? &lv_font_montserrat_28 : &lv_font_montserrat_20);
            kit::text(layer, t, f, g.card ? P.ink : P.muted, cx - cw / 2, top, cw, ch);
            const lv_font_t* nf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
            const int nh = lv_font_get_line_height(nf);
            snprintf(t, sizeof t, "%s", name(who));
            kit::text(layer, t, nf, P.ink, cx - 3 * cell, top + ch + 3, 6 * cell, nh);
        } else {
            // the deck, face down
            kit::fill_rect(layer, cx - cw / 2 - 2, top - 2, cx + cw / 2 + 1, top + ch + 1, P.stone_dark, 6);
            kit::fill_rect(layer, cx - cw / 2, top, cx + cw / 2 - 1, top + ch - 1, P.frame, 5);
            kit::text(layer, "S!", large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, P.lit, cx - cw / 2, top, cw, ch);
        }
    }
    // Pawns
    const int pr = cell * 36 / 100 > 3 ? cell * 36 / 100 : 3;
    const bool mine = my_play();
    for (int c = 0; c < kColors; ++c)
        for (int p = 0; p < kPawns; ++p) {
            int x, y;
            to_screen(a, pawn_pt(g, c, p), &x, &y);
            if (c == g.turn && mine) {
                bool ring = false;
                if (split_a >= 0) {
                    for (int i = 0; i < ms_n && !ring; ++i)
                        ring = ms[i].kind == Kind::Split && ms[i].pawn == split_a && ms[i].to == split_to && ms[i].pawn2 == p;
                } else ring = pawn_can_move(p);
                if (p == picked) kit::fill_circle(layer, x, y, pr + 4, P.selected);
                else if (ring) kit::ring(layer, x, y, pr + 3, 2, P.lit);
            }
            draw_pawn(layer, x, y, pr, color_of(c));
        }
    // Where the picked pawn can go
    if (mine && picked >= 0 && split_a < 0) {
        for (int i = 0; i < ms_n; ++i) {
            if (ms[i].pawn != picked) continue;
            int x, y;
            to_screen(a, target_pt(ms[i]), &x, &y);
            bool on_pawn = ms[i].kind == Kind::Switch || ms[i].kind == Kind::Sorry;
            for (int c = 0; c < kColors && !on_pawn; ++c)                 // a pawn there goes back
                for (int p = 0; p < kPawns && !on_pawn && c != g.turn; ++p) {
                    int px, py;
                    to_screen(a, pawn_pt(g, c, p), &px, &py);
                    on_pawn = g.pos[c][p] != kStart && g.pos[c][p] != kHome && px == x && py == y;
                }
            // a frame round the square (a pawn there goes back)
            const int h = cell / 2 + 1, w = cell / 6 > 2 ? cell / 6 : 2;
            kit::fill_rect(layer, x - h - 1, y - h - 1, x + h + 1, y + h + 1, P.stone_dark, 3);
            kit::fill_rect(layer, x - h, y - h, x + h, y + h, P.target, 3);
            kit::fill_rect(layer, x - h + w, y - h + w, x + h - w, y + h - w, P.cell, 1);
            if (on_pawn) {
                for (int c = 0; c < kColors; ++c)
                    for (int p = 0; p < kPawns && c != g.turn; ++p) {
                        int px, py;
                        to_screen(a, pawn_pt(g, c, p), &px, &py);
                        if (px == x && py == y) draw_pawn(layer, x, y, pr, color_of(c));
                    }
            }
        }
    }
    // The line under the board
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const char* line = news;
    char t[56];
    if (mine && g.card) {
        snprintf(t, sizeof t, "%s%s", card_text(g.card), split_a >= 0 ? " - tap the other pawn" : "");
        line = t;
    }
    kit::text(layer, line, f, P.ink, a.x1, a.y1 + info_y, lv_area_get_width(&a), lv_font_get_line_height(f));
}

// ---- Flow -------------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void clear_pick() { picked = -1; split_a = -1; }

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    refresh_moves();
    char s[40], sh[24];
    if (g.phase == Phase::Over) {
        const bool you = g.winner == 0 && !pnp();
        snprintf(s, sizeof s, you ? "You win!" : "%s wins", name(g.winner));
        snprintf(sh, sizeof sh, you ? "You win!" : "%s wins", name(g.winner));
    } else if (pnp() && human(g.turn)) {
        snprintf(s, sizeof s, g.phase == Phase::Draw ? "%s's turn: draw" : "%s's move", name(g.turn));
        snprintf(sh, sizeof sh, g.phase == Phase::Draw ? "%s's turn" : "%s's move", name(g.turn));
    } else if (g.turn == 0) {
        snprintf(s, sizeof s, g.phase == Phase::Draw ? "Your turn (Red): draw" : "Your move (Red)");
        snprintf(sh, sizeof sh, g.phase == Phase::Draw ? "Your turn" : "Your move");
    } else {
        snprintf(s, sizeof s, "%s's turn (%s)", name(g.turn), kColorNames[g.turn]);
        snprintf(sh, sizeof sh, "%s's turn", name(g.turn));
    }
    kit::top_bar_status(bar, s, sh);
    if (key) {
        const char* label = nullptr;
        bool show = true;
        if (g.phase == Phase::Over) label = "Play Again";
        else if (human(g.turn) && g.phase == Phase::Draw && !waiting) label = "Draw Card";
        else if (my_play() && g.may_pass() && split_a < 0) label = "Pass";
        else show = false;
        lv_obj_set_hidden(key, !show);
        if (show) lv_label_set_text(lv_obj_get_child(key, 0), label);
    }
    if (area) lv_obj_invalidate(area);
}

void say_move(int who, const Last& l)
{
    char what[24] = "";
    if (l.kind == Kind::Sorry) snprintf(what, sizeof what, "Sorry!");
    else if (l.kind == Kind::Switch) snprintf(what, sizeof what, "switched places");
    else if (l.kind == Kind::Pass) snprintf(what, sizeof what, "no move with a %d", l.card);
    else if (l.home) snprintf(what, sizeof what, "a pawn Home");
    else if (l.slid) snprintf(what, sizeof what, "slide!");
    else if (l.card == kSorry) snprintf(what, sizeof what, "Sorry!");
    else snprintf(what, sizeof what, "played a %d", l.card);
    int bumped = 0, mine = 0;
    for (int b = 0; b < 16; ++b) if (l.bumped & (1u << b)) { ++bumped; mine += b < 4; }
    if (pnp()) mine = 0;                                  // "your pawn" means one player
    if (mine && who != 0) snprintf(news, sizeof news, "%s sent your pawn back!", name(who));
    else if (bumped) snprintf(news, sizeof news, "%s: %s, %d sent back", name(who), what, bumped);
    else snprintf(news, sizeof news, "%s: %s", name(who), what);
    if (mine) sound(Sound::Error);
    else if (l.home && human(who)) sound(Sound::Hint);
    else if (l.kind != Kind::Pass) sound(human(who) ? Sound::Place : Sound::Turn);
}

void game_over()
{
    if (!S->recorded && recordable()) record();
    const bool won = human(S->g.winner);
    sound(won ? Sound::Win : Sound::Lose);
    if (won) kit::flash();
}

void after_play(int who)
{
    say_move(who, S->g.last);
    clear_pick();
    save();
    if (S->g.phase == Phase::Over) game_over();
    waiting = true;
    // after your move, a moment before a computer draws; after theirs, time to see it
    wait_until = now_ms + (human(who) ? (human(S->g.turn) ? 250 : kCpuDrawMs) : kAfterMs);
    update();
}

void play_mine(const Move& m)
{
    if (!S->g.play(m)) return;
    after_play(0);
}

void draw_mine()
{
    Game& g = S->g;
    if (g.phase != Phase::Draw || !human(g.turn) || waiting) return;
    g.draw();
    clear_pick();
    refresh_moves();
    if (ms_n == 0) {
        if (g.card == kSorry) snprintf(news, sizeof news, "No move with a Sorry!");
        else snprintf(news, sizeof news, "No move with a%s %d", g.card == 8 || g.card == 11 ? "n" : "", g.card);
        waiting = true;
        wait_until = now_ms + kNoMoveMs;
    } else if (movable_count() == 1) {
        for (int p = 0; p < kPawns; ++p) if (pawn_can_move(p)) picked = int8_t(p);
    }
    save();
    update();
}

void new_game(int level, int people)
{
    kit::flash_stop();
    if (S->g.phase != Phase::Over && S->g.turns >= 4 && !S->recorded && recordable()) record();   // a game left part-way
    *S = State{};
    S->level = uint8_t(level);
    S->people = uint8_t(people);
    S->g.start(seed_now());
    clear_pick();
    news[0] = 0;
    waiting = true;
    wait_until = now_ms + 300;
    save();
    update();
}

void key_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.phase == Phase::Over) { new_game(S->level, S->people); return; }
    if (!human(g.turn) || waiting) return;
    if (g.phase == Phase::Draw) { draw_mine(); return; }
    if (my_play() && g.may_pass()) { Move pass; play_mine(pass); }
}

float dist2(int x, int y, int px, int py) { const float dx = float(x - px), dy = float(y - py); return dx * dx + dy * dy; }

void press_cb(lv_event_t*) { lv_indev_get_point(lv_indev_active(), &press_pt); }

// A tap acts on release, where the stylus came down (lift-off readings drift)
void board_cb(lv_event_t*)
{
    if (!S || overlay_open() || !area) return;
    Game& g = S->g;
    if (human(g.turn) && g.phase == Phase::Draw && !waiting) {     // a tap on the deck draws too
        const lv_point_t pt = press_pt;
        lv_area_t a;
        lv_obj_get_coords(area, &a);
        int cx, cy;
        to_screen(a, Pt{8.0f, 8.0f}, &cx, &cy);
        if (dist2(pt.x, pt.y, cx, cy) < float(4 * cell * cell * 4)) draw_mine();
        return;
    }
    if (!my_play()) return;
    const lv_point_t pt = press_pt;
    lv_area_t a;
    lv_obj_get_coords(area, &a);
    const float reach = float(cell * cell) * 0.9f;
    // The second pawn of a split
    if (split_a >= 0) {
        for (int i = 0; i < ms_n; ++i) {
            const Move& m = ms[i];
            if (m.kind != Kind::Split || m.pawn != split_a || m.to != split_to) continue;
            int x, y;
            to_screen(a, pawn_pt(g, g.turn, m.pawn2), &x, &y);
            if (dist2(pt.x, pt.y, x, y) < reach) { play_mine(m); return; }
        }
        clear_pick();
        update();
        return;
    }
    // A target of the picked pawn
    if (picked >= 0) {
        int best = -1;
        float bd = reach;
        for (int i = 0; i < ms_n; ++i) {
            if (ms[i].pawn != picked) continue;
            int x, y;
            to_screen(a, target_pt(ms[i]), &x, &y);
            const float d = dist2(pt.x, pt.y, x, y);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0) {
            const Move m = ms[best];
            if (m.kind != Kind::Split) { play_mine(m); return; }
            int seconds = 0, only = -1;                 // which pawns could take the rest
            for (int i = 0; i < ms_n; ++i)
                if (ms[i].kind == Kind::Split && ms[i].pawn == m.pawn && ms[i].to == m.to) { ++seconds; only = i; }
            if (seconds == 1) { play_mine(ms[only]); return; }
            split_a = m.pawn;
            split_to = m.to;
            picked = -1;
            update();
            return;
        }
    }
    // Pick one of your pawns (any pawn in Start stands for the one that would go)
    int best = -1;
    float bd = reach;
    for (int p = 0; p < kPawns; ++p) {
        int x, y;
        to_screen(a, pawn_pt(g, g.turn, p), &x, &y);
        const float d = dist2(pt.x, pt.y, x, y);
        if (d >= bd) continue;
        int q = p;
        if (g.pos[g.turn][p] == kStart && !pawn_can_move(p))
            for (int r = 0; r < kPawns; ++r) if (g.pos[g.turn][r] == kStart && pawn_can_move(r)) q = r;
        if (pawn_can_move(q)) { bd = d; best = q; }
    }
    picked = int8_t(best);
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
    const int lh = lv_font_get_line_height(f);
    const int top = bar.h;
    const int room = m.h - kh - 2 * pad - lh - 4;
    int side = m.w - 2 * pad;
    if (side > room) side = room;
    cell = side / 16;
    bx = (m.w - 16 * cell) / 2;
    by = 2;
    info_y = by + 16 * cell + 4;
    area = lv_obj_create(scr);
    lv_obj_remove_style_all(area);
    lv_obj_set_size(area, m.w, info_y + lh + 2);
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
        char a[12], b[12], c[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.wins);
        snprintf(c, sizeof c, "%lu%%", (unsigned long)(sum.wins * 100 / sum.games));
        table_add(t, a, b, c, "");
        const char* const head[4] = {"Games", "Wins", "Won", ""};
        static const int8_t pct[4] = {34, 33, 33, 0};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[8], s2[8], tm[16];
            snprintf(s1, sizeof s1, "%u", unsigned(r.place));
            snprintf(s2, sizeof s2, "%u", unsigned(r.home));
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, kLevels[r.level < 3 ? r.level : 0], tm);
        }
        const char* const head2[4] = {"Place", "Home", "Level", "Time"};
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

// Pass and Play: how many people share the board
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
void menu_back()       { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Sorry-CYD!", kLevels, h, false, "Pass and Play");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now()); }
    clear_pick();
    news[0] = 0;
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
    if (waiting && wait_until == 0) wait_until = now + 500;
    if (overlay_open()) { if (waiting) wait_until = now + 400; }
    else if (waiting && int32_t(now - wait_until) >= 0) {
        waiting = false;
        if (g.phase == Phase::Play && human(g.turn)) {
            refresh_moves();
            if (ms_n == 0) {
                g.lose_turn();
                save();
                // the next player is a computer: a moment before it draws
                if (!human(g.turn)) { waiting = true; wait_until = now + kCpuDrawMs; }
            } else if (movable_count() == 1 && picked < 0)
                for (int p = 0; p < kPawns; ++p) if (pawn_can_move(p)) picked = int8_t(p);
        }
        update();
    }
    // The computers: draw, show the card, move
    if (!waiting && !overlay_open() && g.phase != Phase::Over && !human(g.turn)) {
        if (g.phase == Phase::Draw) {
            g.draw();
            news[0] = 0;
            waiting = true;
            wait_until = now + kCpuShowMs;
            update();
        } else {
            const int who = g.turn;
            Move ms2[kMaxMoves];
            if (g.moves(ms2, kMaxMoves) == 0) {
                g.lose_turn();
                say_move(who, g.last);
                save();
                waiting = true;
                wait_until = now + kAfterMs;
                update();
            } else {
                const Move m = g.ai_move(pnp() ? 1 : S->level);
                g.play(m);
                after_play(who);
            }
        }
    }
    clock_.tick(now, g.phase != Phase::Over, S->seconds);
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    const Game& g = st->g;
    const bool p = st->people > 1;
    if (g.phase == Phase::Over) {
        const int w = g.winner >= 0 ? g.winner : 0;
        if (!p && w == 0) snprintf(buf, cap, "Game over: you won");
        else snprintf(buf, cap, "Game over: %s won", p && w < st->people ? kColorNames[w] : kNames[w]);
    } else if (p) snprintf(buf, cap, "Pass and play, %d players", st->people);
    else snprintf(buf, cap, "%s, %d of your pawns Home", kLevels[st->level], g.home_count(0));
    return true;
}

void save_now() { save(); }

// Icon: four pawns on a corner of the track, a card behind
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int cw = s * 34 / 100, ch = s * 46 / 100;
    const int cx = a.x1 + s * 62 / 100, cy = a.y1 + s * 8 / 100;
    kit::fill_rect(layer, cx - 2, cy - 2, cx + cw + 1, cy + ch + 1, P.stone_dark, 4);
    kit::fill_rect(layer, cx, cy, cx + cw - 1, cy + ch - 1, P.screen, 3);
    kit::text(layer, "7", &lv_font_montserrat_20, P.ink, cx, cy, cw, ch);
    const int r = s / 10;
    for (int c = 0; c < kColors; ++c) {
        const int x = a.x1 + s * (20 + (c & 1) * 26) / 100, y = a.y1 + s * (52 + (c >> 1) * 26) / 100;
        draw_pawn(layer, x, y, r, color_of(c));
    }
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

namespace sorrycyd_preview {
// One step for you, as a player would (the live check)
int robot()
{
    if (!S) return -1;
    Game& g = S->g;
    if (g.phase == Phase::Over) return 2;
    if (human(g.turn) && g.phase == Phase::Draw && !waiting) { draw_mine(); return 1; }
    if (my_play()) {
        refresh_moves();
        if (ms_n > 0) play_mine(ms[0]);
        else if (g.may_pass()) { Move pass; play_mine(pass); }
        return 1;
    }
    return 0;
}
int turns() { return S ? S->g.turns : 0; }
sorry::Game* game() { return S ? &S->g : nullptr; }
void pick(int p) { picked = int8_t(p); update(); }
void hold(bool on) { frozen = on; waiting = false; update(); }
void people() { open_people(); }
void news_line(const char* t) { snprintf(news, sizeof news, "%s", t); update(); }
}

namespace games {
extern const GameOps sorrycyd_ops;
const GameOps sorrycyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
