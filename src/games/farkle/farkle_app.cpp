// Farkle: registry entry, save file and screen. Rules and the computer in
// farkle_core.*. Two players: you against the computer (Easy / Medium /
// Hard; who starts alternates each game) or pass-and-play.
//
// Screen: top bar (clock, whose turn and what happened, ☰); the two scores
// (the player whose turn it is has a gold edge); this turn's points; six
// dice in two rows - dice set aside earlier this turn are faded, dice you
// pick from the roll turn gold, a Farkle turns the roll red; keys:
// [Roll 4 Dice] [Bank 350] (or [Pass the Dice] after a Farkle).
// The computer takes its turn step by step at a readable pace.
#include <cstdio>
#include <new>
#include "farkle_core.h"
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace farkle;
using namespace ui;
using twoplayer::Mode;
using twoplayer::Level;

constexpr const char* kId = "farkle";
const twoplayer::Sides kSides{"Player 1", "Player 2"};

struct State {
    Game     g;
    Mode     mode = Mode::Computer;
    Level    level = Level::Medium;
    uint8_t  human = 0;                // vs computer: your side (0 starts)
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 8;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   board = nullptr;
lv_obj_t*   roll_k = nullptr;
lv_obj_t*   roll_l = nullptr;
lv_obj_t*   bank_k = nullptr;
lv_obj_t*   bank_l = nullptr;
int         die = 0, die_gap = 0, dice_y = 0, dice_x0 = 0, score_h = 0;
uint32_t    now_ms = 0, next_at = 0, last_save_ms = 0;

void build();
void open_menu();
void update_status();

bool computer_turn() { return S->mode == Mode::Computer && S->g.turn != S->human && !S->g.over(); }
const char* name_of(int side)
{
    if (S->mode == Mode::Computer) return side == S->human ? "You" : "Computer";
    return side == 0 ? kSides.side1 : kSides.side2;
}

// ---- Save -------------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = uint8_t(S->mode);
    buf[n + 1] = uint8_t(S->level);
    buf[n + 2] = S->human;
    buf[n + 3] = S->recorded;
    for (int k = 0; k < 4; ++k) buf[n + 4 + k] = uint8_t(S->seconds >> (8 * k));
    shell().save_game(kId, buf, sizeof buf);
}

bool load(State& st)
{
    uint8_t buf[kSaveBytes];
    const size_t n = shell().load_game ? shell().load_game(kId, buf, sizeof buf) : 0;
    if (n != kSaveBytes || !st.g.deserialize(buf, n)) return false;
    const size_t b = Game::kSaveBytes;
    st.mode = buf[b] == 1 ? Mode::PassAndPlay : Mode::Computer;
    st.level = buf[b + 1] <= 2 ? Level(buf[b + 1]) : Level::Medium;
    st.human = buf[b + 2] & 1;
    st.recorded = buf[b + 3] ? 1 : 0;
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[b + 4 + k]) << (8 * k);
    return true;
}

void record()
{
    if (S->recorded || !S->g.over()) return;
    S->recorded = 1;
    twoplayer::Record r;
    r.mode = S->mode;
    r.level = S->level;
    const int w = S->g.winner;
    if (S->mode == Mode::Computer) r.result = w == S->human ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
    else r.result = w == 0 ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
    r.moves = S->g.turns;
    r.seconds = S->seconds;
    kit::record_two_player(kId, r, kSides);
}

// ---- Dice and scores --------------------------------------------------------------------------
void draw_die(lv_layer_t* layer, int x, int y, int size, int value, lv_color_t face, lv_color_t edge, lv_color_t pip)
{
    kit::fill_rect(layer, x, y, x + size - 1, y + size - 1, edge, size / 5);
    kit::fill_rect(layer, x + 2, y + 2, x + size - 3, y + size - 3, face, size / 5 - 1);
    static const uint16_t kPips[7] = {0, 0x010, 0x101, 0x111, 0x145, 0x155, 0x16D};
    const int r = size / 11 > 2 ? size / 11 : 2, step = size * 27 / 100;
    for (int k = 0; k < 9; ++k)
        if ((kPips[value] >> k) & 1)
            kit::fill_circle(layer, x + size / 2 + (k % 3 - 1) * step, y + size / 2 + (k / 3 - 1) * step, r, pip);
}

void board_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Metrics& M = metrics();
    const Game& g = S->g;
    // Scores: two boxes, the one to play edged in gold
    const int w = lv_area_get_width(&a), gap = M.large ? 10 : 6, bw = (w - gap) / 2;
    const lv_font_t* nf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const lv_font_t* sf = M.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    for (int s = 0; s < 2; ++s) {
        const int x = a.x1 + s * (bw + gap);
        const bool on = !g.over() && g.turn == s;
        const bool won = g.over() && g.winner == s;
        kit::fill_rect(layer, x, a.y1, x + bw - 1, a.y1 + score_h - 1, on || won ? P.lit : P.key_border, 6);
        kit::fill_rect(layer, x + 2, a.y1 + 2, x + bw - 3, a.y1 + score_h - 3, P.key, 5);
        const int nh = lv_font_get_line_height(nf), sh = lv_font_get_line_height(sf);
        kit::text(layer, name_of(s), nf, P.muted, x, a.y1 + 4, bw, nh);
        char t[16];
        snprintf(t, sizeof t, "%ld", long(g.score[s]));
        kit::text(layer, t, sf, P.ink, x, a.y1 + 4 + nh, bw, sh);
    }
    // This turn's points
    const lv_font_t* tf = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int ty = a.y1 + score_h + (M.large ? 10 : 6), th = lv_font_get_line_height(tf);
    char t[64];
    const int ps = g.picked_score();
    if (g.phase == Phase::Farkle) snprintf(t, sizeof t, "Farkle! %ld points lost", long(g.turn_score));
    else if (g.over()) snprintf(t, sizeof t, "%s %s", name_of(g.winner), g.winner == S->human || S->mode != Mode::Computer ? "win!" : "wins");
    else if (g.phase == Phase::Start) snprintf(t, sizeof t, "%s: roll the dice", name_of(g.turn));
    else if (ps > 0) snprintf(t, sizeof t, "This turn: %ld + %d picked", long(g.turn_score), ps);
    else if (g.picked) snprintf(t, sizeof t, "Those dice don't all score");
    else snprintf(t, sizeof t, "This turn: %ld - pick scoring dice", long(g.turn_score));
    kit::text(layer, t, tf, g.phase == Phase::Farkle || (g.picked && ps < 0) ? P.conflict : P.ink, a.x1, ty, w, th);
    // Dice: two rows of three
    for (int i = 0; i < kDice; ++i) {
        const int x = dice_x0 + (i % 3) * (die + die_gap), y = dice_y + (i / 3) * (die + die_gap);
        const bool live = (g.live >> i) & 1, kept = (g.kept >> i) & 1, picked = (g.picked >> i) & 1;
        if (g.phase == Phase::Start && !g.turns && !g.score[0] && !g.score[1]) {
            draw_die(layer, x, y, die, i + 1, P.cell, P.key_border, P.muted);   // before the first roll
            continue;
        }
        if (kept)        draw_die(layer, x, y, die, g.dice[i], P.screen, P.key_border, P.muted);
        else if (picked) draw_die(layer, x, y, die, g.dice[i], P.lit, P.selected, P.ink);
        else if (live && g.phase == Phase::Farkle) draw_die(layer, x, y, die, g.dice[i], P.conflict_bg, P.conflict, P.ink);
        else             draw_die(layer, x, y, die, g.dice[i], P.cell, P.key_border, P.ink);
    }
}

void board_press_cb(lv_event_t* e)
{
    if (!S || overlay_open() || computer_turn() || S->g.phase != Phase::Rolled) return;
    lv_indev_t* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const int c = (p.x - dice_x0) / (die + die_gap), r = (p.y - dice_y) / (die + die_gap);
    if (p.x < dice_x0 || p.y < dice_y || c > 2 || r > 1) return;
    S->g.toggle(r * 3 + c);                               // picking is silent
    save();
    lv_obj_invalidate(board);
    update_status();
}

// ---- Turn flow ------------------------------------------------------------------------------------
Rng rng_now() { return Rng(shell().random_seed ? shell().random_seed() : now_ms * 2654435761u); }

void after_change()
{
    Game& g = S->g;
    if (g.over() && !S->recorded) {
        record();
        const bool you_won = S->mode != Mode::Computer || g.winner == S->human;
        sound(you_won ? Sound::Win : Sound::Lose);
        if (you_won) kit::flash();
    }
    save();
    if (board) lv_obj_invalidate(board);
    update_status();
}

void do_roll()
{
    Game& g = S->g;
    Rng rng = rng_now();
    if (!g.roll(rng)) { sound(Sound::Error); return; }
    sound(g.phase == Phase::Farkle ? Sound::Error : Sound::Move);
    next_at = now_ms + (g.phase == Phase::Farkle ? 1600 : 900);
    after_change();
}

void do_bank()
{
    if (!S->g.bank()) { sound(Sound::Error); return; }
    if (!S->g.over()) sound(Sound::Place);
    next_at = now_ms + 900;
    after_change();
}

void roll_cb(lv_event_t*)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    if (g.over()) {                                       // Play Again
        const Mode m = S->mode;
        const Level l = S->level;
        const uint8_t h = S->human ^ 1;                   // who starts alternates
        *S = State{};
        S->mode = m; S->level = l; S->human = m == Mode::Computer ? h : 0;
        next_at = now_ms + 600;
        after_change();
        return;
    }
    if (computer_turn()) return;
    if (g.phase == Phase::Farkle) { g.next_turn(); next_at = now_ms + 600; after_change(); return; }
    do_roll();
}

void bank_cb(lv_event_t*)
{
    if (!S || overlay_open() || computer_turn() || S->g.over()) return;
    do_bank();
}

void computer_step()
{
    Game& g = S->g;
    if (!computer_turn() || overlay_open() || int32_t(now_ms - next_at) < 0) return;
    switch (g.phase) {
        case Phase::Start:  do_roll(); return;
        case Phase::Farkle: g.next_turn(); next_at = now_ms + 700; after_change(); return;
        case Phase::Rolled: {
            if (!g.picked) {                                // first show the pick
                g.picked = plan(g, int(S->level)).pick;
                next_at = now_ms + 900;
                after_change();
                return;
            }
            const Plan p = plan(g, int(S->level));
            if (p.roll_on) do_roll(); else do_bank();
            return;
        }
        default: return;
    }
}

void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char t[16], s[48];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (g.over()) snprintf(s, sizeof s, "%s %ld to %ld", name_of(g.winner), long(g.score[g.winner]), long(g.score[g.winner ^ 1]));
    else if (g.last_turn_for >= 0) snprintf(s, sizeof s, "%s: last turn!", name_of(g.turn));
    else if (computer_turn()) snprintf(s, sizeof s, "Computer's turn");
    else if (S->mode == Mode::PassAndPlay) snprintf(s, sizeof s, "%s's turn", name_of(g.turn));
    else snprintf(s, sizeof s, "Your turn, to %ld", long(kTarget));
    kit::top_bar_status(bar, s);
    // Keys
    char r[24], b[24];
    const bool mine = !computer_turn();
    if (g.over()) snprintf(r, sizeof r, "Play Again");
    else if (g.phase == Phase::Farkle) snprintf(r, sizeof r, "Pass the Dice");
    else if (g.dice_to_roll() == kDice && g.phase == Phase::Rolled) snprintf(r, sizeof r, "Roll All 6");
    else snprintf(r, sizeof r, "Roll %d %s", g.dice_to_roll(), g.dice_to_roll() == 1 ? "Die" : "Dice");
    lv_label_set_text(roll_l, r);
    const int ps = g.picked_score();
    if (g.phase == Phase::Rolled && ps > 0) snprintf(b, sizeof b, "Bank %ld", long(g.turn_score + ps));
    else snprintf(b, sizeof b, "Bank");
    lv_label_set_text(bank_l, b);
    set_dim(roll_k, !g.over() && !(mine && (g.can_roll() || g.phase == Phase::Farkle)));
    set_checked(roll_k, g.over() || (mine && g.phase != Phase::Rolled));
    set_dim(bank_k, g.over() || !mine || !g.can_bank());
    set_checked(bank_k, mine && g.can_bank());
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, gap = m.large ? 8 : 6;
    const int kh = menu_btn_h();
    const int keys_y = m.h - pad - kh;
    const int top = bar.h + (m.large ? 6 : 3);
    board = lv_obj_create(scr);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, m.w - 2 * pad, keys_y - gap - top);
    lv_obj_set_pos(board, pad, top);
    lv_obj_set_clickable(board, true);
    lv_obj_add_event_cb(board, board_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board, board_press_cb, LV_EVENT_PRESSED, nullptr);
    // Sizes: scores, the turn line, then two rows of three dice as big as fit
    score_h = lv_font_get_line_height(m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12)
            + lv_font_get_line_height(m.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20) + 10;
    const int turn_h = lv_font_get_line_height(m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14) + (m.large ? 20 : 12);
    const int room_h = keys_y - gap - top - score_h - turn_h;
    die_gap = m.large ? 14 : 10;
    die = (m.w - 2 * pad - 2 * die_gap) / 3;
    if (2 * die + die_gap > room_h) die = (room_h - die_gap) / 2;
    const int cap = m.large ? 88 : 64;
    if (die > cap) die = cap;
    dice_x0 = (m.w - (3 * die + 2 * die_gap)) / 2;
    dice_y = top + score_h + turn_h + (room_h - (2 * die + die_gap)) / 2;
    const int half = (m.w - 2 * pad - gap) / 2;
    roll_k = make_key(scr, half, kh, roll_cb, 0);
    lv_obj_set_pos(roll_k, pad, keys_y);
    roll_l = key_label(roll_k, "Roll", menu_font());
    bank_k = make_key(scr, half, kh, bank_cb, 0);
    lv_obj_set_pos(bank_k, pad + half + gap, keys_y);
    bank_l = key_label(bank_k, "Bank", menu_font());
    clock_ = kit::Clock{};
    update_status();
}

// ---- Menu ---------------------------------------------------------------------------------
void stats_back() { open_menu(); }
void open_stats() { kit::stats_two_player(kId, kSides, stats_back); }

void menu_pick(int id)
{
    // Leaving a started vs-computer game for a new one counts as a loss
    if (!S->g.over() && !S->recorded && S->mode == Mode::Computer && S->g.turns > 0) {
        S->g.winner = uint8_t(S->human ^ 1);
        S->g.phase = Phase::Over;
        record();
    }
    const uint8_t h = S->human ^ 1;
    *S = State{};
    if (id == kit::kPassAndPlay) { S->mode = Mode::PassAndPlay; S->human = 0; }
    else { S->mode = Mode::Computer; S->level = Level(id); S->human = h; }
    next_at = now_ms + 600;
    kit::flash_stop();
    after_change();
}
void menu_back() { update_status(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_two_player("Farkle", h);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) *S = State{};
    next_at = 0;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    board = roll_k = roll_l = bank_k = bank_l = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    now_ms = now;
    if (!S) return;
    if (clock_.tick(now, !S->g.over(), S->seconds)) {
        char t[16];
        twoplayer::format_time(t, sizeof t, S->seconds);
        if (bar.left) lv_label_set_text(bar.left, t);       // only the clock: no board redraw
    }
    computer_step();
    if (now - last_save_ms > 30000) { last_save_ms = now; save(); }
}

void restyle() { if (S) build(); }

bool summary(char* buf, size_t cap)
{
    State tmp;
    const State* st = S;
    if (!st) { if (!load(tmp)) return false; st = &tmp; }
    if (st->mode == Mode::Computer)
        snprintf(buf, cap, "%s, you %ld, computer %ld", twoplayer::level_name(st->level),
                 long(st->g.score[st->human]), long(st->g.score[st->human ^ 1]));
    else snprintf(buf, cap, "Pass and play, %ld to %ld", long(st->g.score[0]), long(st->g.score[1]));
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int size = lv_area_get_width(&a), d = size * 46 / 100;
    draw_die(layer, a.x1, a.y1 + size / 10, d, 1, P.lit, P.selected, P.ink);
    draw_die(layer, a.x1 + size - d, a.y1, d, 5, P.lit, P.selected, P.ink);
    draw_die(layer, a.x1 + (size - d) / 2, a.y1 + size - d, d, 3, P.cell, P.key_border, P.ink);
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
extern const GameOps farkle_ops;
const GameOps farkle_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace farkle_preview {
farkle::Game* game() { return S ? &S->g : nullptr; }
void refresh() { after_change(); }
void step(uint32_t ms) { now_ms += ms; computer_step(); }
} // namespace farkle_preview
#endif
