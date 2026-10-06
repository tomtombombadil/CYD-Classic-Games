// Trivial CYD: registry entry, save file and screen. Rules and the
// computers in trivialcyd_core.*, questions from the shared trivia bank
// (Open Trivia DB, CC BY-SA 4.0 - see common/trivia_data.cpp).
//
// The board: the 36-square track round the edge of a square (each square
// in its colour; an HQ shows a wedge, a Roll Again square a die), the
// players' tokens on it, and in the middle each player's wedge pie, the
// die and what just happened. One key at the bottom: Roll / Play Again.
// After a roll the two squares you can reach are framed: tap one. A
// question fills the screen: its colour and category, the question, the
// answers (2 or 4); right turns green, wrong red, then back to the board.
//
// You against Max and Zoe (Easy / Medium / Hard), or 2-4 people pass and
// play (Options key). Sounds: a roll (Move), a right answer (Place, or
// Turn for a computer), a wedge (Trill), your wrong answer ("aww"), the
// end (Win / Lose).
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/trivia_bank.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "trivialcyd_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace tcyd;
using namespace ui;

constexpr const char* kId = "trivialcyd";
const char* const kCpuNames[kMaxPlayers] = {"You", "Max", "Zoe", "Ada"};
const char* const kPlayerColors[kMaxPlayers] = {"Red", "Blue", "Yellow", "Green"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};
constexpr uint32_t kCpuMs = 900, kCpuAnswerMs = 1700, kRevealMs = 1700;

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
    trivia::Question q;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board = nullptr;
lv_obj_t*   page = nullptr;          // the question page
lv_obj_t*   head = nullptr;
lv_obj_t*   qlabel = nullptr;
lv_obj_t*   ans[4] = {};
lv_obj_t*   ans_text[4] = {};
lv_obj_t*   key = nullptr;
int         pad = 4, cell = 22, bx = 0, by = 0, info_y = 0, lh = 14, ans_w = 0, q_w = 0, life_y = 0;
uint32_t    now_ms = 0, wait_until = 0, last_save_ms = 0;
bool        frozen = false;
lv_point_t  press_pt{0, 0};
char        news[64] = "";

void open_menu();
void update();

bool human(int p) { return S && S->g.human(p); }
bool pnp() { return S && S->g.people > 1; }
const char* name(int p) { return pnp() ? kPlayerColors[p] : kCpuNames[p]; }

lv_color_t player_color(int p)
{
    const Palette& P = pal();
    const lv_color_t c[kMaxPlayers] = {P.piece_a, P.frame, P.piece_b, P.win};
    return c[p & 3];
}

lv_color_t cat_color(int c)
{
    const Palette& P = pal();
    switch (c) {
        case 0:  return P.frame;                                   // Geography: blue
        case 1:  return lv_color_mix(P.piece_a, P.cell, 140);      // Entertainment: pink
        case 2:  return P.piece_b;                                 // History: yellow
        case 3:  return P.sq_dark;                                 // Arts & Literature: brown
        case 4:  return P.win;                                     // Science & Nature: green
        default: return lv_color_mix(P.piece_a, P.lit, 100);       // Sports & Leisure: orange
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
    for (int k = 0; k < 4; ++k) buf[n + 1 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, kSaveBytes);
    delete[] buf;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    const bool ok = n == kSaveBytes && st.g.deserialize(buf, n);
    if (ok) {
        st.recorded = buf[Game::kSaveBytes] ? 1 : 0;
        st.seconds = 0;
        for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[Game::kSaveBytes + 1 + k]) << (8 * k);
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
    const Game& g = S->g;
    if (g.winner == 0) return 1;
    int place = g.winner > 0 ? 2 : 1;
    for (int p = 1; p < g.players; ++p) if (p != g.winner && g.wedge_count(p) > g.wedge_count(0)) ++place;
    return place;
}

bool recordable() { return S && S->g.people == 1; }

void record()
{
    Record r;
    r.place = uint8_t(my_place());
    r.wedges = uint8_t(S->g.wedge_count(0));
    r.level = S->g.level;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

// ---- Geometry: the track round a 10 x 10 square -----------------------------------------------
void square_cell(int sq, int* col, int* row)
{
    if (sq < 9)       { *col = sq; *row = 0; }
    else if (sq < 18) { *col = 9; *row = sq - 9; }
    else if (sq < 27) { *col = 27 - sq; *row = 9; }
    else              { *col = 0; *row = 36 - sq; }
}

void square_xy(const lv_area_t& a, int sq, int* x, int* y)
{
    int c, r;
    square_cell(sq, &c, &r);
    *x = a.x1 + bx + c * cell;
    *y = a.y1 + by + r * cell;
}

// ---- Drawing ----------------------------------------------------------------------------------
void draw_wedge(lv_layer_t* layer, int cx, int cy, int r, int k, lv_color_t col)
{
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = col;
    d.opa = LV_OPA_COVER;
    static const float kCos[7] = {0.0f, 0.866f, 0.866f, 0.0f, -0.866f, -0.866f, 0.0f};
    static const float kSin[7] = {-1.0f, -0.5f, 0.5f, 1.0f, 0.5f, -0.5f, -1.0f};
    d.p[0].x = cx; d.p[0].y = cy;
    d.p[1].x = cx + int(kCos[k] * r); d.p[1].y = cy + int(kSin[k] * r);
    d.p[2].x = cx + int(kCos[k + 1] * r); d.p[2].y = cy + int(kSin[k + 1] * r);
    lv_draw_triangle(layer, &d);
}

void draw_pie(lv_layer_t* layer, int cx, int cy, int r, uint8_t wedges, lv_color_t rim)
{
    const Palette& P = pal();
    kit::fill_circle(layer, cx, cy, r + 2, rim);
    kit::fill_circle(layer, cx, cy, r, P.cell);
    for (int k = 0; k < kColors; ++k) if (wedges >> k & 1) draw_wedge(layer, cx, cy, r, k, cat_color(k));
}

void draw_die(lv_layer_t* layer, int x, int y, int s, int v)
{
    const Palette& P = pal();
    kit::fill_rect(layer, x, y, x + s - 1, y + s - 1, P.stone_dark, s / 5);
    kit::fill_rect(layer, x + 1, y + 1, x + s - 2, y + s - 2, P.stone_light, s / 5);
    const int pr = s / 10 > 1 ? s / 10 : 1;
    const int q1 = x + s / 4, q2 = x + s / 2, q3 = x + 3 * s / 4, r1 = y + s / 4, r2 = y + s / 2, r3 = y + 3 * s / 4;
    auto pip = [&](int px, int py) { kit::fill_circle(layer, px, py, pr, P.stone_dark); };
    if (v % 2) pip(q2, r2);
    if (v >= 2) { pip(q1, r1); pip(q3, r3); }
    if (v >= 4) { pip(q3, r1); pip(q1, r3); }
    if (v == 6) { pip(q1, r2); pip(q3, r2); }
}

void board_draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    const Palette& P = pal();
    const bool large = metrics().large;
    lv_area_t a;
    lv_obj_get_coords(board, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    // the track
    for (int sq = 0; sq < kSquares; ++sq) {
        int x, y;
        square_xy(a, sq, &x, &y);
        const int c = square_color(sq);
        kit::fill_rect(layer, x, y, x + cell - 1, y + cell - 1, P.stone_dark, 3);
        kit::fill_rect(layer, x + 1, y + 1, x + cell - 2, y + cell - 2, c == kRollAgain ? P.cell : cat_color(c), 3);
        if (c == kRollAgain) draw_die(layer, x + cell / 4, y + cell / 4, cell / 2, 3);
        else if (is_hq(sq)) {
            kit::fill_circle(layer, x + cell / 2, y + cell / 2, cell / 3, P.stone_light);
            draw_wedge(layer, x + cell / 2, y + cell / 2, cell / 3 - 1, 0, cat_color(c));
            draw_wedge(layer, x + cell / 2, y + cell / 2, cell / 3 - 1, 1, cat_color(c));
        }
    }
    // where the roll can land
    const bool mine = g.phase == Phase::Move && human(g.turn);
    if (mine)
        for (int k = 0; k < 2; ++k) {
            int x, y;
            square_xy(a, g.dest[k], &x, &y);
            kit::fill_rect(layer, x - 2, y - 2, x + cell + 1, y + cell + 1, P.stone_dark, 4);
            kit::fill_rect(layer, x - 1, y - 1, x + cell, y + cell, P.lit, 4);
            const int c = square_color(g.dest[k]);
            kit::fill_rect(layer, x + 2, y + 2, x + cell - 3, y + cell - 3, c == kRollAgain ? P.cell : cat_color(c), 2);
            if (c == kRollAgain) draw_die(layer, x + cell / 4, y + cell / 4, cell / 2, 3);
        }
    // tokens: up to four in a square, one per corner
    for (int p = 0; p < g.players; ++p) {
        int x, y;
        square_xy(a, g.pos[p], &x, &y);
        const int tr = cell / 5 > 2 ? cell / 5 : 2;
        const int tx = x + ((p & 1) ? cell - tr - 3 : tr + 3), ty = y + ((p & 2) ? cell - tr - 3 : tr + 3);
        kit::fill_circle(layer, tx, ty, tr + 2, P.stone_light);
        kit::fill_circle(layer, tx, ty, tr + 1, P.stone_dark);
        kit::fill_circle(layer, tx, ty, tr, player_color(p));
        if (p == g.turn && g.phase != Phase::Over) kit::ring(layer, tx, ty, tr + 4, 2, P.lit);
    }
    // the middle: pies, die, news
    const int ix = a.x1 + bx + cell + pad, iy = a.y1 + by + cell + pad, iw = 8 * cell - 2 * pad, ih = 8 * cell - 2 * pad;
    const lv_font_t* f = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int fh = lv_font_get_line_height(f);
    const int cols = g.players <= 2 ? 2 : 2;
    const int pw = iw / cols, ph = (ih - fh - cell) / 2;
    const int pr = (pw < ph ? pw : ph) / 2 - fh / 2 - 4;
    for (int p = 0; p < g.players; ++p) {
        const int px = ix + (p % cols) * pw, py = iy + (p / cols) * ph;
        draw_pie(layer, px + pw / 2, py + pr + 4, pr, g.wedges[p], p == g.turn && g.phase != Phase::Over ? P.lit : player_color(p));
        kit::text(layer, name(p), f, P.ink, px, py + 2 * pr + 8, pw, fh);
    }
    if (g.die && g.phase == Phase::Move) draw_die(layer, ix + iw / 2 - cell / 2, iy + ih - fh - cell - 2, cell, g.die);
    kit::text(layer, news, f, P.ink, ix, iy + ih - fh, iw, fh);
    // the line under the board
    char t[80];
    t[0] = 0;
    if (g.phase == Phase::Move && mine) snprintf(t, sizeof t, "You rolled %d: tap a framed square", g.die);
    else if (g.phase == Phase::Roll && human(g.turn)) snprintf(t, sizeof t, g.wedge_count(g.turn) == kColors ? "Six wedges! Roll for the winning question" : "Roll the die");
    kit::text(layer, t, f, P.ink, a.x1, a.y1 + info_y, lv_area_get_width(&a), lh);
}

// ---- The question page ------------------------------------------------------------------------
const lv_font_t* q_font() { return metrics().large ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }

int text_h(const char* s, const lv_font_t* f, int w)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return p.y;
}

bool asking() { const Phase ph = S->g.phase; return ph == Phase::Ask || ph == Phase::Reveal; }

void layout_page()
{
    const Game& g = S->g;
    if (g.q < 0 || !trivia::get(g.q, S->q)) return;
    char t[80];
    const char* what = g.final_q ? "The winning question!" : is_hq(g.pos[g.turn]) && !(g.wedges[g.turn] >> g.q_color & 1) ? "For the wedge!" : "";
    snprintf(t, sizeof t, "%s: %s  %s", name(g.turn), kColorNames[g.q_color], what);
    lv_label_set_text(head, t);
    lv_obj_set_style_bg_color(head, cat_color(g.q_color), 0);
    lv_label_set_text(qlabel, S->q.text);
    const int top = pad + lh + 8 + pad;                       // under the coloured heading
    lv_obj_set_pos(qlabel, pad, top);
    int y = top + text_h(S->q.text, q_font(), q_w) + pad;
    const int min_h = menu_btn_h();
    int hs[4], need = 0;
    for (int s = 0; s < 4; ++s) {
        lv_obj_set_hidden(ans[s], s >= g.answers);
        if (s >= g.answers) continue;
        char a[140];
        snprintf(a, sizeof a, "%c: %s", 'A' + s, S->q.answer[g.order[s]]);
        lv_label_set_text(ans_text[s], a);
        const int h = text_h(a, menu_font(), ans_w - 24) + 10;
        hs[s] = h > min_h ? h : min_h;
        need += hs[s] + pad;
    }
    const int squeeze = need > life_y - pad - y ? (need - (life_y - pad - y) + 3) / g.answers : 0;
    for (int s = 0; s < g.answers; ++s) {
        const int h = hs[s] - squeeze > 24 ? hs[s] - squeeze : 24;
        lv_obj_set_size(ans[s], ans_w, h);
        lv_obj_set_pos(ans[s], pad, y);
        y += h + pad;
    }
}

void paint_page()
{
    const Game& g = S->g;
    const Palette& P = pal();
    for (int s = 0; s < g.answers; ++s) {
        lv_obj_remove_local_style_prop(ans[s], LV_STYLE_BG_COLOR, 0);
        lv_obj_remove_local_style_prop(ans_text[s], LV_STYLE_TEXT_COLOR, 0);
        bool paint = false;
        lv_color_t bg = P.key, ink = P.ink;
        if (g.phase == Phase::Reveal && s == g.right_slot()) { bg = P.win; ink = P.stone_light; paint = true; }
        else if (g.phase == Phase::Reveal && s == g.answered) { bg = P.piece_a; ink = P.stone_light; paint = true; }
        if (paint) { lv_obj_set_style_bg_color(ans[s], bg, 0); lv_obj_set_style_text_color(ans_text[s], ink, 0); }
    }
}

// ---- Flow -------------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    char s[48], sh[24];
    if (g.phase == Phase::Over) {
        const bool you = !pnp() && g.winner == 0;
        snprintf(s, sizeof s, you ? "You win!" : "%s wins", name(g.winner));
        snprintf(sh, sizeof sh, "%.23s", s);
    } else if (human(g.turn) && !pnp()) {
        snprintf(s, sizeof s, "Your turn (Red)");
        snprintf(sh, sizeof sh, "Your turn");
    } else {
        snprintf(s, sizeof s, "%s's turn (%s)", name(g.turn), kPlayerColors[g.turn]);
        snprintf(sh, sizeof sh, "%s's turn", name(g.turn));
    }
    kit::top_bar_status(bar, s, sh);
    const bool ask = asking();
    lv_obj_set_hidden(page, !ask);
    lv_obj_set_hidden(board, ask);
    if (ask) { layout_page(); paint_page(); }
    if (key) {
        const char* label = nullptr;
        if (g.phase == Phase::Over) label = "Play Again";
        else if (g.phase == Phase::Roll && human(g.turn) && int32_t(now_ms - wait_until) >= 0) label = "Roll";
        lv_obj_set_hidden(key, label == nullptr || ask);
        if (label) lv_label_set_text(lv_obj_get_child(key, 0), label);
    }
    if (board) lv_obj_invalidate(board);
}

void game_over()
{
    if (!S->recorded && recordable()) record();
    const bool won = S->g.winner >= 0 && human(S->g.winner);
    sound(won ? Sound::Win : Sound::Lose);
    if (won && !pnp()) kit::flash();
}

void after_answer()
{
    Game& g = S->g;
    const int p = g.turn;
    if (g.right) {
        if (g.won_wedge) { sound(Sound::Trill); snprintf(news, sizeof news, "%s won the %s wedge!", name(p), kColorNames[g.q_color]); }
        else { sound(human(p) ? Sound::Place : Sound::Turn); snprintf(news, sizeof news, "%s %s right", name(p), !pnp() && p == 0 ? "were" : "was"); }
    } else {
        if (human(p)) sound(Sound::Error);
        snprintf(news, sizeof news, "%s %s wrong", name(p), !pnp() && p == 0 ? "were" : "was");
    }
    wait_until = now_ms + kRevealMs;
    save();
    update();
}

void do_roll()
{
    Game& g = S->g;
    if (!g.roll()) return;
    sound(Sound::Move);
    if (g.phase == Phase::Ask) snprintf(news, sizeof news, "%s: the winning question!", name(g.turn));
    else snprintf(news, sizeof news, "%s rolled %d", name(g.turn), g.die);
    save();
    update();
}

void do_move(int sq)
{
    Game& g = S->g;
    const int p = g.turn;
    if (!g.move(sq)) return;
    if (g.phase == Phase::Roll) snprintf(news, sizeof news, "%s: roll again!", name(p));
    wait_until = now_ms + (human(p) ? 200 : kCpuMs);
    save();
    update();
}

void new_game(int level, int people)
{
    kit::flash_stop();
    if (S->g.phase != Phase::Over && S->g.turns > 3 && !S->recorded && recordable()) record();
    const int players = people > 1 ? people : 3;
    S->g.start(seed_now(), players, people, level);
    S->recorded = 0;
    S->seconds = 0;
    news[0] = 0;
    wait_until = now_ms + 400;
    save();
    update();
}

void key_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.phase == Phase::Over) { new_game(g.level, g.people); return; }
    if (g.phase == Phase::Roll && human(g.turn) && int32_t(now_ms - wait_until) >= 0) do_roll();
}

void answer_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.phase != Phase::Ask || !human(g.turn)) return;
    const int s = int(intptr_t(lv_event_get_user_data(e)));
    if (g.answer(s)) after_answer();
}

void press_cb(lv_event_t*) { lv_indev_get_point(lv_indev_active(), &press_pt); }

// A tap acts on release, where the stylus came down
void board_cb(lv_event_t*)
{
    if (!S || overlay_open() || !board) return;
    Game& g = S->g;
    if (g.phase != Phase::Move || !human(g.turn)) return;
    lv_area_t a;
    lv_obj_get_coords(board, &a);
    for (int k = 0; k < 2; ++k) {
        int x, y;
        square_xy(a, g.dest[k], &x, &y);
        if (press_pt.x >= x - 4 && press_pt.x < x + cell + 4 && press_pt.y >= y - 4 && press_pt.y < y + cell + 4) { do_move(g.dest[k]); return; }
    }
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    pad = m.large ? 8 : 4;
    const int kh = menu_btn_h();
    const lv_font_t* f = m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    lh = lv_font_get_line_height(f);
    const int top = bar.h;
    const int room = m.h - kh - 2 * pad - lh - 6;
    int side = m.w - 2 * pad;
    if (side > room) side = room;
    cell = side / 10;
    bx = (m.w - 10 * cell) / 2;
    by = 2;
    info_y = by + 10 * cell + 4;
    board = lv_obj_create(scr);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, m.w, info_y + lh + 2);
    lv_obj_set_pos(board, 0, top + pad / 2);
    lv_obj_set_clickable(board, true);
    lv_obj_add_event_cb(board, board_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(board, board_cb, LV_EVENT_CLICKED, nullptr);
    // the question page
    page = lv_obj_create(scr);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, m.w, m.h - top);
    lv_obj_set_pos(page, 0, top);
    head = lv_label_create(page);
    lv_obj_set_style_text_font(head, f, 0);
    lv_obj_set_style_text_color(head, pal().stone_light, 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(head, 5, 0);
    lv_obj_set_style_pad_all(head, 4, 0);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_width(head, m.w - 2 * pad);
    lv_obj_set_pos(head, pad, pad);
    lv_obj_set_height(head, lh + 8);
    q_w = m.w - 2 * pad;
    qlabel = lv_label_create(page);
    lv_obj_set_style_text_font(qlabel, q_font(), 0);
    lv_obj_set_style_text_color(qlabel, pal().ink, 0);
    lv_label_set_long_mode(qlabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(qlabel, q_w);
    ans_w = m.w - 2 * pad;
    for (int s = 0; s < 4; ++s) {
        ans[s] = make_key(page, ans_w, kh, answer_cb, s);
        ans_text[s] = lv_label_create(ans[s]);
        lv_obj_set_style_text_font(ans_text[s], menu_font(), 0);
        lv_label_set_long_mode(ans_text[s], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ans_text[s], ans_w - 24);
        lv_obj_align(ans_text[s], LV_ALIGN_LEFT_MID, 10, 0);
    }
    life_y = m.h - top - pad;
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
        const char* const h1[4] = {"Games", "Wins", "Won", ""};
        static const int8_t pct[4] = {34, 33, 33, 0};
        table_show(t, h1, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[8], s2[8], tm[16];
            snprintf(s1, sizeof s1, "%u", unsigned(r.place));
            snprintf(s2, sizeof s2, "%u", unsigned(r.wedges));
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, kLevels[r.level < 3 ? r.level : 0], tm);
        }
        const char* const h2[4] = {"Place", "Wedges", "Level", "Time"};
        static const int8_t pct2[4] = {20, 26, 26, 28};
        table_show(rt, h2, pct2, hf, hf);
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
    overlay_text("Two to four people take turns on one board: Red, then Blue, Yellow, Green.", false);
    for (int n = 2; n <= kMaxPlayers; ++n) {
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
    kit::menu_solo("Trivial CYD", kLevels, h, false, "Pass and Play");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now(), 3, 1, 1); }
    news[0] = 0;
    wait_until = 0;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    trivia::release();
    bar = kit::TopBar{};
    board = page = head = qlabel = key = nullptr;
    for (int s = 0; s < 4; ++s) ans[s] = ans_text[s] = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S || frozen) return;
    const bool was_waiting = int32_t(now_ms - wait_until) < 0;
    now_ms = now;
    Game& g = S->g;
    if (overlay_open()) { if (int32_t(now - wait_until) > -300) wait_until = now + 300; clock_.tick(now, false, S->seconds); return; }
    const bool waiting = int32_t(now - wait_until) < 0;
    if (was_waiting && !waiting) update();                      // e.g. the Roll key comes back
    if (!waiting) {
        if (g.phase == Phase::Reveal) {
            g.next();
            if (g.phase == Phase::Over) game_over();
            else if (!human(g.turn)) wait_until = now + kCpuMs;
            save();
            update();
        } else if (!human(g.turn)) {
            switch (g.phase) {
                case Phase::Roll: do_roll(); wait_until = now + (g.phase == Phase::Ask ? kCpuAnswerMs : kCpuMs); break;
                case Phase::Move: do_move(g.cpu_move()); if (g.phase == Phase::Ask) wait_until = now + kCpuAnswerMs; break;
                case Phase::Ask: if (g.answer(g.cpu_answer())) after_answer(); break;   // after its reading time
                default: break;
            }
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
    if (g.phase == Phase::Over) snprintf(buf, cap, "Game over: %d wedges", g.wedge_count(0));
    else if (g.people > 1) snprintf(buf, cap, "Pass and play, %d players", g.players);
    else snprintf(buf, cap, "%s, you have %d of 6 wedges", kLevels[g.level], g.wedge_count(0));
    delete tmp;
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int s = lv_area_get_width(&a);
    draw_pie(layer, a.x1 + s / 2, a.y1 + s / 2, s * 40 / 100, 0x3F, pal().stone_dark);
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

namespace trivialcyd_preview {
tcyd::Game* game() { return S ? &S->g : nullptr; }
void hold(bool on) { frozen = on; wait_until = 0; update(); }
void news_line(const char* t) { snprintf(news, sizeof news, "%s", t); update(); }
void people() { open_people(); }
}

namespace games {
extern const GameOps trivialcyd_ops;
const GameOps trivialcyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
