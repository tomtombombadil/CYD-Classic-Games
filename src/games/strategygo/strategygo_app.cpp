// Strategy Go!: screen and registry entry. Rules and the computer live in
// strategygo_core.*; turns, the computer's task, menus, stats and wireless
// play come from the shared two-player controller (games/common/match.*).
//
// The board is drawn from the viewer's side: your army at the bottom (the
// board turns round when you play Blue). Your pieces show their ranks; the
// other side's show only once they have fought (a dot marks one that has
// moved - it is no Bomb or Flag). Tap your piece, then a lit square.
//
// The setup: a sensible random army is laid out for you; tap two of your
// pieces to swap them, Shuffle for another, then Ready. A battle shows a
// banner with both pieces before play goes on. Pass-and-play hides each
// army from the other player: after a move the board says "Pass the board
// to Blue" and waits for Ready.
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/match.h"
#include "games/registry.h"
#include "strategygo_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace sgo;
using namespace ui;

constexpr const char* kId = "strategygo";
const twoplayer::Sides kSides = {"Red", "Blue"};

Board*    B = nullptr;
lv_obj_t* board_obj = nullptr;
lv_obj_t* cover_l = nullptr;
lv_obj_t* key_a = nullptr;      // left half: Shuffle
lv_obj_t* key_b = nullptr;      // right half: Ready
lv_obj_t* key_c = nullptr;      // full width: Pieces / Ready (cover) / Pass to ...

struct View {
    int      viewer = 0;            // pass-and-play: whose army is at the bottom
    bool     cover = false;         // pass-and-play: "Pass the board to ..." until Ready
    bool     result = false;        // pass-and-play: a battle's result is up, then Pass
    bool     ready_pending = false; // Ready tapped before this side's turn to set up
    uint32_t seed = 0;
    Army     army;                  // the army being set up (from the viewer's back row)
    int      pick = -1;             // setup: the piece picked to swap (army index); play: the square picked
} V;

int cell = 22;

// A battle on screen: the board already holds the result; a banner shows both pieces a while
constexpr uint32_t kBattleMs = 1700, kFlagMs = 1200;
struct BattleShow { bool on = false; Battle b; uint32_t start = 0; } BS;
uint32_t now_ms = 0;
lv_timer_t* hold_timer = nullptr;
bool busy() { return BS.on; }

bool pnp()     { return match::state().mode == twoplayer::Mode::PassAndPlay; }
int  viewer()  { return pnp() ? V.viewer : match::my_side(); }
bool over()    { return B->result() != -1; }
bool placing() { return B->setup() && viewer() >= 0 && B->placed(viewer()) < kArmy && !over(); }
int  sent()    { return B && B->setup() && viewer() >= 0 ? B->placed(viewer()) : 0; }

// Screen square s <-> board square: the viewer's army at the bottom
int to_board(int s) { return viewer() == 1 ? kCells - 1 - s : s; }

void redraw() { if (board_obj) lv_obj_invalidate(board_obj); }
void update_keys();

// A fresh shuffled army; pieces already sent keep their places
void new_army()
{
    const uint32_t r = shell().random_seed ? shell().random_seed() : lv_tick_get();
    V.seed = r ^ (V.seed * 2654435761u);
    random_army(V.seed, V.army);
    V.pick = -1;
    const int s = viewer();
    if (s < 0 || !sent()) return;
    // Keep what was sent (a restart mid-setup): swap those ranks into their squares
    for (int i = 0; i < kArmy; ++i) {
        const Square& q = B->sq[army_cell(s, i)];
        if (q.side != s) continue;
        for (int j = 0; j < kArmy; ++j)
            if (j != i && V.army.rank_at[j] == q.rank && B->sq[army_cell(s, j)].side != s) {
                const uint8_t t = V.army.rank_at[i]; V.army.rank_at[i] = V.army.rank_at[j]; V.army.rank_at[j] = t;
                break;
            }
    }
}

void start_placing() { V.ready_pending = false; new_army(); }

// ---- Rules for the controller ---------------------------------------------------------------
int result() { return B->result(); }
int turn()   { return B->turn(); }
int moves()  { return B->moves; }

void hold_cb(lv_timer_t*);
void hold_on(bool on)
{
    if (on && !hold_timer) hold_timer = lv_timer_create(hold_cb, 100, nullptr);
    if (!on && hold_timer) { lv_timer_delete(hold_timer); hold_timer = nullptr; }
}

void play(int m)
{
    const int mover = B->turn();
    const bool was_setup = B->setup();
    if (!B->play(uint32_t(m))) return;
    V.pick = -1;
    if (!was_setup && B->last.outcome != kNoBattle) {
        BS.on = true;
        BS.b = B->last;
        BS.start = now_ms;
        hold_on(true);
        if (pnp()) V.viewer = mover;                 // the mover sees how it went
    } else if (pnp() && !over() && B->turn() != mover) {
        V.cover = true;                              // a move or a finished setup: pass at once
    }
    update_keys();
    redraw();
}

void battle_end()
{
    BS.on = false;
    hold_on(false);
    if (pnp() && !over()) V.result = true;           // seen: now pass
    update_keys();
    redraw();
    match::refresh();
}

void hold_cb(lv_timer_t*)
{
    if (!BS.on) return;
    const uint32_t len = BS.b.outcome == kFlagTaken ? kFlagMs : kBattleMs;
    if (now_ms - BS.start >= len) battle_end();
}

void reset()
{
    *B = Board{};
    BS = BattleShow{};
    hold_on(false);
    V.cover = pnp();
    V.viewer = 0;
    V.result = false;
    V.pick = -1;
    start_placing();
    update_keys();
}

int think(int level, uint32_t seed, volatile bool*) { return int(best_move(*B, level, seed)); }

// "your Captain", "their Scout", "Red's Miner"
void piece_words(char* buf, size_t cap, int side, int rank)
{
    if (pnp()) snprintf(buf, cap, "%s's %s", side == 0 ? kSides.side1 : kSides.side2, rank_name(rank));
    else snprintf(buf, cap, "%s %s", side == viewer() ? "your" : "their", rank_name(rank));
}

void battle_text(const Battle& b, char* l1, size_t c1, char* l2, size_t c2)
{
    char a[32], d[32];
    piece_words(a, sizeof a, b.side, b.attacker);
    piece_words(d, sizeof d, b.side ^ 1, b.defender);
    a[0] = char(a[0] >= 'a' && a[0] <= 'z' ? a[0] - 32 : a[0]);
    switch (b.outcome) {
        case kAttackerWins: snprintf(l1, c1, "%s wins!", rank_name(b.attacker)); snprintf(l2, c2, "%s takes %s", a, d); break;
        case kDefenderWins: snprintf(l1, c1, "%s holds!", rank_name(b.defender)); snprintf(l2, c2, "%s is lost to %s", a, d); break;
        case kBothLost:     snprintf(l1, c1, "Both lost!"); snprintf(l2, c2, "%s and %s", a, d); break;
        case kFlagTaken:    snprintf(l1, c1, "Flag captured!"); snprintf(l2, c2, "%s takes the Flag", a); break;
        case kBombDefused:  snprintf(l1, c1, "Defused!"); snprintf(l2, c2, "%s clears a Bomb", a); break;
        case kBombHit:      snprintf(l1, c1, "BOOM!"); snprintf(l2, c2, "%s hits a Bomb", a); break;
        default: l1[0] = l2[0] = 0; break;
    }
}

void move_sound(bool by_other)
{
    if (B->setup() || B->moves == kSetupPlies) {
        if (B->moves % kArmy == 0) sound(by_other ? Sound::Turn : Sound::Place);   // an army is in
        return;
    }
    const Battle& b = B->last;
    if (b.outcome == kNoBattle) { sound(by_other ? Sound::Turn : Sound::Place); return; }
    if (b.outcome == kBombHit || b.outcome == kBombDefused) { sound(Sound::Boom); return; }
    if (b.outcome == kBothLost) { sound(Sound::Draw); return; }
    const int me = pnp() ? b.side : viewer();
    const bool i_won = (b.outcome == kAttackerWins) == (b.side == me);
    sound(i_won ? Sound::Trill : Sound::Error);
}

void note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (V.cover) { snprintf(buf, cap, "Pass the board to %s", B->turn() == 0 ? kSides.side1 : kSides.side2); return; }
    if (placing()) {
        if (V.ready_pending) snprintf(buf, cap, "Your army is ready");
        else if (V.pick >= 0) snprintf(buf, cap, "Tap another piece to swap");
        else snprintf(buf, cap, "Tap two pieces to swap; then Ready");
        return;
    }
    if (B->setup()) { snprintf(buf, cap, "Waiting for the other army"); return; }
    if (B->last.outcome != kNoBattle && !BS.on) {
        char l1[32], l2[64];
        battle_text(B->last, l1, sizeof l1, l2, sizeof l2);
        snprintf(buf, cap, "%s", l2);
        return;
    }
    if (V.pick >= 0) snprintf(buf, cap, "Tap a lit square");
}

void score(char* buf, size_t cap)
{
    if (B->setup()) { buf[0] = 0; return; }
    snprintf(buf, cap, "Pieces: %s %d, %s %d", kSides.side1, B->alive(0), kSides.side2, B->alive(1));
}

int list(int* out, int cap)
{
    int n = 0;
    if (B->setup()) {
        for (int c = 0; c < kCells; ++c)
            for (int r = 0; r < kRanks && n < cap; ++r)
                if (B->can_play(setup_key(c, r))) out[n++] = int(setup_key(c, r));
        return n;
    }
    uint32_t m[256];
    const int k = B->moves_for(B->turn(), m, 256);
    for (int i = 0; i < k && n < cap; ++i) out[n++] = int(m[i]);
    return n;
}

void open_pieces();

match::Game make_game()
{
    match::Game g{kId, "Strategy Go!", kSides, result, turn, moves, play, reset, think, redraw};
    g.note = note;
    g.score = score;
    g.move_sound = move_sound;
    g.busy = busy;
    g.ai_stack = 12 * 1024;
    g.no_pause = []() { return B && B->setup(); };   // the computer's 40 setup pieces go down at once
    g.legal = [](int m) { return m >= 0 && B->can_play(uint32_t(m)); };
    g.list = list;
    return g;
}

// ---- Keys ---------------------------------------------------------------------------------------
enum Key : intptr_t { kShuffle = 1, kReady, kCoverReady, kPass, kPieces };

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
    if (ov) {
        set_key(key_a, false, "", false); set_key(key_b, false, "", false); set_key(key_c, false, "", false);
    } else if (V.cover || V.result) {
        char t[32];
        if (V.cover) snprintf(t, sizeof t, "Ready");
        else snprintf(t, sizeof t, "Pass to %s", B->turn() == 0 ? kSides.side1 : kSides.side2);
        set_key(key_a, false, "", false); set_key(key_b, false, "", false);
        set_key(key_c, true, t, true);
    } else if (placing()) {
        set_key(key_a, true, "Shuffle", false);
        set_key(key_b, true, V.ready_pending ? "Waiting..." : "Ready", !V.ready_pending);
        set_dim(key_a, V.ready_pending);
        set_dim(key_b, false);
        set_key(key_c, false, "", false);
    } else {
        set_dim(key_a, false);
        set_key(key_a, false, "", false); set_key(key_b, false, "", false);
        set_key(key_c, true, "Pieces", false);
    }
}

void changed()
{
    update_keys();
    redraw();
    match::refresh();
}

// Ready: the army's pieces go in as moves, back row first. Before this
// side's turn to set up, they wait and go when it comes (tick()).
bool send_army()
{
    const int me = viewer();
    while (B->setup() && B->placed(me) < kArmy && B->turn() == me && match::human_may_move()) {
        // the next piece not yet down
        int i = 0;
        while (i < kArmy && B->sq[army_cell(me, i)].side == me) ++i;
        if (i >= kArmy) break;
        const uint32_t key = setup_key(army_cell(me, i), V.army.rank_at[i]);
        if (!B->can_play(key)) break;
        match::human_move(int(key));
    }
    return B->placed(me) == kArmy;
}

void do_key(Key k)
{
    if (!B) return;
    switch (k) {
        case kShuffle: if (!V.ready_pending) new_army(); break;
        case kReady:   if (!V.ready_pending) { V.pick = -1; V.ready_pending = !send_army(); changed(); return; } break;
        case kCoverReady:
            V.cover = false;
            V.viewer = B->turn();
            V.pick = -1;
            if (placing()) start_placing();
            break;
        case kPass: V.result = false; V.cover = true; break;
        case kPieces: open_pieces(); return;
    }
    changed();
}

void key_row_cb(lv_event_t* e)
{
    lv_obj_t* k = lv_event_get_target_obj(e);
    if (k == key_a)      do_key(kShuffle);
    else if (k == key_b) do_key(kReady);
    else                 do_key(V.cover ? kCoverReady : V.result ? kPass : kPieces);
}

// ---- The Pieces page: what each side has lost --------------------------------------------------
void pieces_back_cb(lv_event_t*) { close_overlays(); changed(); }

void open_pieces()
{
    overlay_begin("Pieces");
    const int me = viewer() >= 0 ? viewer() : 0;
    Table& t = scratch_table(0);
    table_clear(t);
    static const uint8_t order[kRanks] = {kMarshal, kGeneral, kColonel, kMajor, kCaptain, kLieutenant,
                                          kSergeant, kMiner, kScout, kSpy, kBomb, kFlag};
    for (int i = 0; i < kRanks; ++i) {
        const int r = order[i];
        char a[16], b[8], c[8], d[8];
        snprintf(a, sizeof a, "%s %s", rank_short(r), rank_name(r));
        snprintf(b, sizeof b, "%u", unsigned(kCount[r]));
        snprintf(c, sizeof c, "%u", unsigned(B->lost[me][r]));
        snprintf(d, sizeof d, "%u", unsigned(B->lost[me ^ 1][r]));
        table_add(t, a, b, c, d);
    }
    overlay_text("Pieces lost so far:", true);
    const char* const head[4] = {"Piece", "Each", pnp() ? (me == 0 ? "Red" : "Blue") : "Yours",
                                 pnp() ? (me == 0 ? "Blue" : "Red") : "Theirs"};
    static const int8_t pct[4] = {36, 18, 23, 23};
    table_show(t, head, pct, &lv_font_montserrat_12, metrics().large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    overlay_back(pieces_back_cb, 0);
}

// ---- Drawing ------------------------------------------------------------------------------------
lv_color_t side_color(int s) { return s == 0 ? pal().piece_a : pal().frame; }

void draw_bomb(lv_layer_t* layer, int x, int y, int s, lv_color_t ink)
{
    const int r = s * 22 / 100;
    kit::fill_circle(layer, x + s / 2, y + s / 2 + s / 12, r, ink);
    kit::line(layer, x + s / 2 + r / 2, y + s / 2 - r / 2, x + s / 2 + r, y + s / 2 - r - s / 10, 2, ink);
    kit::fill_circle(layer, x + s / 2 + r + 1, y + s / 2 - r - s / 10 - 1, s / 14 > 1 ? s / 14 : 1, pal().piece_b);
}

void draw_flag(lv_layer_t* layer, int x, int y, int s, lv_color_t ink)
{
    const int px = x + s * 32 / 100, top = y + s * 20 / 100, bot = y + s * 82 / 100;
    kit::line(layer, px, top, px, bot, 2, ink);
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = ink;
    d.opa = LV_OPA_COVER;
    d.p[0].x = px + 1; d.p[0].y = top;
    d.p[1].x = px + 1; d.p[1].y = top + s * 30 / 100;
    d.p[2].x = x + s * 78 / 100; d.p[2].y = top + s * 15 / 100;
    lv_draw_triangle(layer, &d);
}

// One piece in an s x s box. known: its rank shows; hidden_from_them: drawn plain (theirs, not seen)
void draw_piece(lv_layer_t* layer, int x, int y, int s, int side, int rank, bool rank_shows, bool own, bool moved,
                bool picked)
{
    const Palette& P = pal();
    const lv_color_t c = side_color(side);
    const int m = s >= 30 ? 2 : 1;
    if (picked) kit::fill_rect(layer, x, y, x + s - 1, y + s - 1, P.lit, 4);
    if (own || !rank_shows) {
        kit::fill_rect(layer, x + m + (picked ? 2 : 0), y + m + (picked ? 2 : 0), x + s - 1 - m - (picked ? 2 : 0),
                       y + s - 1 - m - (picked ? 2 : 0), c, 4);
    } else {
        // Theirs, seen in battle: a light tile in their colour's outline
        kit::fill_rect(layer, x + m, y + m, x + s - 1 - m, y + s - 1 - m, c, 4);
        kit::fill_rect(layer, x + m + 2, y + m + 2, x + s - 3 - m, y + s - 3 - m, P.cell, 3);
    }
    const lv_color_t ink = own || !rank_shows ? P.stone_light : c;
    if (!rank_shows) {
        // Unseen: a plain tile; a small dot if it has moved (no Bomb or Flag)
        if (moved) kit::fill_circle(layer, x + s / 2, y + s / 2, s / 9 > 1 ? s / 9 : 1, lv_color_mix(P.stone_light, c, 160));
        return;
    }
    if (rank == kBomb) { draw_bomb(layer, x, y, s, ink); return; }
    if (rank == kFlag) { draw_flag(layer, x, y, s, ink); return; }
    const lv_font_t* f = s >= 36 ? &lv_font_montserrat_20 : s >= 26 ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    kit::text(layer, rank_short(rank), f, ink, x, y, s, s);
}

void draw_cb(lv_event_t* e)
{
    if (!B) return;
    lv_obj_t* obj = lv_event_get_target_obj(e);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    const int x0 = a.x1, y0 = a.y1, size = kN * cell;
    const lv_color_t land = lv_color_mix(P.felt, P.screen, 90), land2 = lv_color_mix(P.felt, P.screen, 110);
    const lv_color_t water = lv_color_mix(P.frame, P.screen, 150);
    kit::fill_rect(layer, x0, y0, x0 + size, y0 + size, P.key_border);
    const bool covered = V.cover && !over();
    const int me = viewer();
    // Targets of the picked piece
    bool target[kCells] = {};
    if (!placing() && V.pick >= 0 && !B->setup()) {
        uint8_t t[2 * kN];
        const int n = B->targets(V.pick, t);
        for (int i = 0; i < n; ++i) if (B->can_play(move_key(V.pick, t[i]))) target[t[i]] = true;
    }
    for (int s = 0; s < kCells; ++s) {
        const int c = to_board(s);
        const int x = x0 + (s % kN) * cell, y = y0 + (s / kN) * cell;
        kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, lake(c) ? water : ((s / kN + s % kN) & 1) ? land : land2);
        if (lake(c)) {
            kit::fill_rect(layer, x + cell / 4, y + cell / 3, x + cell * 3 / 4, y + cell / 3 + 1, lv_color_mix(P.stone_light, water, 90));
            continue;
        }
        if (covered) continue;
        // The last move: both squares edged
        if (!B->setup() && (c == B->last_move_from || c == B->last_move_to)) {
            kit::fill_rect(layer, x + 1, y + 1, x + cell - 1, y + cell - 1, lv_color_mix(P.lit, land, 110));
        }
        int side = B->sq[c].side, rank = B->sq[c].rank;
        bool shows = false, own = false, moved = B->sq[c].moved;
        if (placing() && home(me, c)) {                 // my army being set up
            for (int i = 0; i < kArmy; ++i) if (army_cell(me, i) == c) { side = me; rank = V.army.rank_at[i]; }
            const bool picked = V.pick >= 0 && army_cell(me, V.pick) == c;
            draw_piece(layer, x, y, cell, side, rank, true, true, false, picked);
            continue;
        }
        if (side < 0) {
            if (target[c]) kit::fill_circle(layer, x + cell / 2, y + cell / 2, cell / 7 > 2 ? cell / 7 : 2, P.target);
            continue;
        }
        own = side == me;
        shows = own || B->sq[c].shown || over();
        draw_piece(layer, x, y, cell, side, rank, shows, own || (over() && !B->sq[c].shown), moved && !own, c == V.pick);
        if (own && B->sq[c].shown && cell >= 22) {      // they know this one: a small eye-dot in the corner
            kit::fill_circle(layer, x + cell - 5, y + 5, 2, P.piece_b);
        }
        if (target[c]) kit::ring(layer, x + cell / 2, y + cell / 2, cell / 2 - 2, 2, P.target);
    }
    // The battle banner
    if (BS.on) {
        const bool large = metrics().large;
        char l1[32], l2[64];
        battle_text(BS.b, l1, sizeof l1, l2, sizeof l2);
        const lv_font_t* f1 = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const lv_font_t* f2 = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
        const int h1 = lv_font_get_line_height(f1), h2 = lv_font_get_line_height(f2);
        const int ps = cell + cell / 3;
        const int bh = h1 + h2 + ps + (large ? 22 : 14), bw = size - 8;
        const int to_row = (viewer() == 1 ? kCells - 1 - BS.b.to : BS.b.to) / kN;
        const int bx = x0 + 4, by = to_row >= kN / 2 ? y0 + cell / 2 : y0 + size - cell / 2 - bh;
        const bool bomb = BS.b.outcome == kBombHit || BS.b.outcome == kBombDefused;
        kit::fill_rect(layer, bx - 2, by - 2, bx + bw + 1, by + bh + 1, bomb ? P.piece_a : P.stone_dark, 8);
        kit::fill_rect(layer, bx, by, bx + bw - 1, by + bh - 1, P.cell, 7);
        // The two pieces, the attacker on the left
        const int py = by + (large ? 6 : 4);
        const int gap = cell;
        const int px = bx + (bw - 2 * ps - gap) / 2;
        draw_piece(layer, px, py, ps, BS.b.side, BS.b.attacker, true, true, false, false);
        kit::text(layer, "vs", f2, P.muted, px + ps, py, gap, ps);
        draw_piece(layer, px + ps + gap, py, ps, BS.b.side ^ 1, BS.b.defender, true, true, false, false);
        kit::text(layer, l1, f1, BS.b.outcome == kBombHit ? P.piece_a : P.ink, bx, py + ps + 2, bw, h1);
        kit::text(layer, l2, f2, P.ink, bx, py + ps + 2 + h1, bw, h2);
    }
}

lv_point_t press_at{-1, -1};
void pressed_cb(lv_event_t*)
{
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &press_at);
}

void clicked_cb(lv_event_t*)
{
    if (!B || !board_obj || press_at.x < 0 || overlay_open()) return;
    if (V.cover || V.result || BS.on || over()) return;
    lv_area_t a;
    lv_obj_get_coords(board_obj, &a);
    const int x = press_at.x - a.x1, y = press_at.y - a.y1;
    if (x < 0 || y < 0 || x >= kN * cell || y >= kN * cell) return;
    const int c = to_board((y / cell) * kN + x / cell);
    const int me = viewer();
    if (placing()) {
        // Swap two pieces (silent)
        if (V.ready_pending || !home(me, c)) return;
        int i = -1;
        for (int k = 0; k < kArmy; ++k) if (army_cell(me, k) == c) i = k;
        if (i < 0 || B->sq[c].side == me) return;           // already sent
        if (V.pick < 0) V.pick = i;
        else if (V.pick == i) V.pick = -1;
        else {
            const uint8_t t = V.army.rank_at[i]; V.army.rank_at[i] = V.army.rank_at[V.pick]; V.army.rank_at[V.pick] = t;
            V.pick = -1;
        }
        changed();
        return;
    }
    if (B->setup()) return;
    // Pick a piece of mine, then a lit square
    if (B->sq[c].side == me && mobile(B->sq[c].rank) && B->turn() == me) {
        V.pick = V.pick == c ? -1 : c;
        changed();
        return;
    }
    if (V.pick >= 0) {
        const uint32_t key = move_key(V.pick, c);
        if (B->can_play(key) && match::human_may_move()) { match::human_move(int(key)); return; }
        V.pick = -1;
        changed();
    }
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

bool load_image(const uint8_t* buf, size_t n, Board& b, match::State* st)
{
    if (n != kSaveBytes || !b.deserialize(buf, Board::kSaveBytes)) return false;
    return st ? match::read_state(buf + Board::kSaveBytes, match::kStateBytes, *st)
              : match::load_state(buf + Board::kSaveBytes, match::kStateBytes);
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
    const int pad = m.large ? 8 : 4, kh = menu_btn_h();
    const int margin = m.large ? 6 : 3;
    const int W = m.w - 2 * margin, H = bottom - top - 2 * margin;
    cell = (W < H ? W : H) / kN;
    const int bs = kN * cell + 1;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, bs, bs);
    lv_obj_set_pos(board_obj, (m.w - bs) / 2, top + (bottom - top - bs) / 2);
    lv_obj_set_clickable(board_obj, true);
    lv_obj_set_scrollable(board_obj, false);
    lv_obj_add_event_cb(board_obj, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board_obj, pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(board_obj, clicked_cb, LV_EVENT_CLICKED, nullptr);
    cover_l = lv_label_create(scr);
    lv_obj_set_width(cover_l, kN * cell - 16);
    lv_label_set_long_mode(cover_l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_align(cover_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(cover_l, m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(cover_l, pal().ink, 0);
    lv_obj_align_to(cover_l, board_obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_hidden(cover_l, true);
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
    hold_on(false);
    BS = BattleShow{};
    save();
    match::closed();
    board_obj = cover_l = key_a = key_b = key_c = nullptr;
    delete B;
    B = nullptr;
}

void tick(uint32_t now)
{
    now_ms = now;
    match::tick(now);
    if (!B) return;
    if (V.ready_pending && placing() && B->turn() == viewer() && match::human_may_move()) {
        V.ready_pending = !send_army();
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
    const bool ok = H.load_game && load_image(img, H.load_game(kId, img, sizeof img), *b, &st);
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
    const int s = lv_area_get_width(&a), n = 3, c = s / n;
    const int x0 = a.x1 + (s - n * c) / 2, y0 = a.y1 + (s - n * c) / 2;
    kit::fill_rect(layer, x0, y0, x0 + n * c, y0 + n * c, lv_color_mix(P.felt, P.screen, 90), c / 4);
    draw_piece(layer, x0, y0, c, 1, 0, false, false, false, false);
    draw_piece(layer, x0 + 2 * c, y0, c, 1, 0, false, false, true, false);
    draw_piece(layer, x0 + c, y0 + c, c, 0, kMarshal, true, true, false, false);
    draw_piece(layer, x0, y0 + 2 * c, c, 0, kFlag, true, true, false, false);
    draw_piece(layer, x0 + 2 * c, y0 + 2 * c, c, 0, kBomb, true, true, false, false);
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

// Preview staging (tools/preview)
namespace sgo_preview {
sgo::Board* board() { return B; }
void pick(int c) { V.pick = c; changed(); }
void battle(bool on) { BS.on = on; if (on) BS.b = B->last; hold_on(false); changed(); }
void ready() { do_key(kReady); }
void cover_ready() { do_key(kCoverReady); }
void pass() { do_key(kPass); }
void pieces() { open_pieces(); }
}

namespace games {
extern const GameOps strategygo_ops;
const GameOps strategygo_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
