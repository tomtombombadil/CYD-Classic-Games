// Wheel of CYD: registry entry, save file and screen. Rules and the
// computer players in wheel_core.*, the puzzles in wheel_phrases.cpp.
// You against two computer players (Max and Zoe: Easy / Medium / Hard),
// or two players passing the board.
//
// Screen, top to bottom: the puzzle board (4 rows of 12 tiles), its
// category, the players' money (round money big, banked money small; the
// player to go has a gold edge), a line saying what just happened, the
// letter keyboard (called letters greyed) and the action keys:
// [Spin] [Vowel $250] [Solve]. A spin fills the screen with the wheel
// until it stops. Solving: tap letters into the blanks, then Solve.
// Computer players take their turns step by step at a readable pace.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "wheel_core.h"

namespace {

using namespace wheel;
using namespace ui;
using twoplayer::Level;
using twoplayer::Mode;

constexpr const char* kId = "wheel";
const twoplayer::Sides kSides{"Player 1", "Player 2"};
const char* const kComputers[2] = {"Max", "Zoe"};
constexpr uint32_t kSpinMs = 2600, kHoldMs = 1100, kRevealMs = 280, kStepMs = 750;

struct State {
    Game     g;
    Mode     mode = Mode::Computer;
    Level    level = Level::Medium;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 8;

// The screen's own state (not saved)
enum class Ui : uint8_t { Idle, Spinning, Revealing, Buying, Solving };
Ui         ui_mode = Ui::Idle;
uint32_t   anim_start = 0, next_at = 0, now_ms = 0, last_save_ms = 0;
float      rot_from = 0, rot_to = 0, rot = 0;           // the wheel's angle (degrees)
char       reveal_letter = 0;
char       guess[kCols * kRows + 1] = "";               // Solving: letters typed for the blanks
int        guess_n = 0;
char       message[64] = "";
bool       pending_call = false;                        // a computer spun money: call a letter next

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*  board_obj = nullptr;
lv_obj_t*  keys_obj = nullptr;
lv_obj_t*  wheel_obj = nullptr;
lv_obj_t*  act[3] = {};
lv_timer_t* anim_timer = nullptr;
int tile_w = 0, tile_h = 0, tile_gap = 0, board_h = 0;
int key_w = 0, key_h = 0, key_gap = 0;

void open_menu();
void update();

bool vs_computer() { return S->mode == Mode::Computer; }
bool computer(int p) { return vs_computer() && p != 0; }
bool human_turn() { return !computer(S->g.turn) && !S->g.over(); }
const char* name_of(int p) { return vs_computer() ? (p == 0 ? "You" : kComputers[p - 1]) : (p == 0 ? kSides.side1 : kSides.side2); }

void money_text(char* buf, size_t cap, long v)
{
    if (v >= 1000) snprintf(buf, cap, "$%ld,%03ld", v / 1000, v % 1000);
    else snprintf(buf, cap, "$%ld", v);
}

uint32_t fresh_seed() { return shell().random_seed ? shell().random_seed() : now_ms * 2654435761u + 1; }

// ---- Save ---------------------------------------------------------------------------------------
void save()
{
    if (!S || !shell().save_game) return;
    uint8_t buf[kSaveBytes];
    const size_t n = S->g.serialize(buf, sizeof buf);
    buf[n] = uint8_t(S->mode);
    buf[n + 1] = uint8_t(S->level);
    buf[n + 2] = S->recorded;
    buf[n + 3] = 0;
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
    st.recorded = buf[b + 2] ? 1 : 0;
    st.seconds = 0;
    for (int k = 0; k < 4; ++k) st.seconds |= uint32_t(buf[b + 4 + k]) << (8 * k);
    if ((st.mode == Mode::Computer) != (st.g.players == 3)) return false;
    return true;
}

void record()
{
    if (S->recorded || !S->g.over()) return;
    S->recorded = 1;
    twoplayer::Record r;
    r.mode = S->mode;
    r.level = S->level;
    const int w = S->g.leader();
    r.result = w < 0 ? twoplayer::Result::Draw : w == 0 ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
    r.moves = S->g.turns;
    r.seconds = S->seconds;
    kit::record_two_player(kId, r, kSides);
}

// ---- What happened -----------------------------------------------------------------------------
void after_change()
{
    Game& g = S->g;
    if (g.over() && !S->recorded) {
        record();
        const int w = g.leader();
        const bool you_won = !vs_computer() ? w >= 0 : w == 0;
        sound(you_won ? Sound::Win : Sound::Lose);
        if (you_won) kit::flash();
    }
    save();
    update();
}

// The wedge under the pointer for wheel angle `r`
int wedge_at(float r)
{
    // Wedge i's middle sits at r + 22.5 i + 11.25 degrees; the pointer at 270
    float a = 270.0f - r;
    a = fmodf(a, 360.0f);
    if (a < 0) a += 360.0f;
    return int(a / (360.0f / kWedges)) % kWedges;
}

void anim_cb(lv_timer_t*)
{
    if (wheel_obj) lv_obj_invalidate(wheel_obj);
    if (board_obj && ui_mode == Ui::Revealing) lv_obj_invalidate(board_obj);
}

void anim_on(bool on)
{
    if (on && !anim_timer) anim_timer = lv_timer_create(anim_cb, 33, nullptr);
    if (!on && anim_timer) { lv_timer_delete(anim_timer); anim_timer = nullptr; }
}

// The player to go spins: the core decides where it lands, the wheel shows it
void do_spin()
{
    Game& g = S->g;
    const int p = g.turn;
    Rng rng(fresh_seed());
    const int w = g.spin(rng);
    if (w < 0) return;
    const float step = 360.0f / kWedges;
    rot_from = fmodf(rot, 360.0f);
    float target = 270.0f - step * w - step / 2;
    while (target < rot_from + 3 * 360.0f) target += 360.0f;
    // Not dead centre: somewhere inside the wedge
    target += (float(int((fresh_seed() >> 8) % 61)) - 30.0f) / 100.0f * step;
    rot_to = target;
    anim_start = now_ms;
    ui_mode = Ui::Spinning;
    pending_call = false;
    const int16_t v = kWheel[w];
    char m[16];
    money_text(m, sizeof m, v);
    if (v == kBust)      snprintf(message, sizeof message, "BUST! %s %s the round's money", name_of(p), p == 0 && vs_computer() ? "lose" : "loses");
    else if (v == kSkip) snprintf(message, sizeof message, "SKIP: %s %s a turn", name_of(p), p == 0 && vs_computer() ? "lose" : "loses");
    else                 snprintf(message, sizeof message, "%s a letter", m);
    anim_on(true);
    after_change();
}

void show_letter_result(int p, char c, int n, bool vowel)
{
    const bool you = p == 0 && vs_computer();
    if (n > 0) {
        snprintf(message, sizeof message, "%s %s %s %c: %d %c%s", name_of(p),
                 vowel ? (you ? "buy" : "buys") : (you ? "call" : "calls"), vowel ? "an" : "", c, n, c, n == 1 ? "" : "'s");
        // "calls  T" -> one space
        char* d = strstr(message, "  ");
        if (d) memmove(d, d + 1, strlen(d));
        reveal_letter = c;
        anim_start = now_ms;
        ui_mode = Ui::Revealing;
        anim_on(true);
        if (!computer(p)) sound(Sound::Place);
        else sound(Sound::Turn);
    } else {
        snprintf(message, sizeof message, "No %c. %s's turn", c, name_of(S->g.turn));
        if (!computer(p) || !vs_computer()) sound(Sound::Error);
    }
    if (S->g.phase == Phase::RoundOver) {
        snprintf(message, sizeof message, "%s solved it!", name_of(p));
        sound(computer(p) ? Sound::Turn : Sound::Trill);
    }
    next_at = now_ms + kStepMs + (n > 0 ? uint32_t(n) * kRevealMs : 0);
}

void call_letter(char c)
{
    Game& g = S->g;
    const int p = g.turn;
    const int n = g.call(c);
    if (n < 0) return;
    show_letter_result(p, c, n, false);
    after_change();
}

void buy_letter(char c)
{
    Game& g = S->g;
    const int p = g.turn;
    const int n = g.buy(c);
    if (n < 0) return;
    ui_mode = Ui::Idle;
    show_letter_result(p, c, n, true);
    after_change();
}

void try_solve(const char* letters)
{
    Game& g = S->g;
    const int p = g.turn;
    const bool right = g.solve(letters);
    ui_mode = Ui::Idle;
    if (right) {
        char m[16];
        money_text(m, sizeof m, g.bank[p]);
        snprintf(message, sizeof message, "%s solved it! Bank %s", name_of(p), m);
        sound(computer(p) ? Sound::Turn : Sound::Trill);
    } else {
        snprintf(message, sizeof message, "%s: not quite. %s's turn", computer(p) ? name_of(p) : "Sorry", name_of(g.turn));
        if (!computer(p)) sound(Sound::Error);
    }
    next_at = now_ms + 1400;
    after_change();
}

void next_round()
{
    Game& g = S->g;
    Rng rng(fresh_seed());
    g.next_round(rng);
    message[0] = 0;
    if (!g.over()) snprintf(message, sizeof message, "Round %d: %s first", g.round + 1, name_of(g.turn));
    else if (g.leader() < 0) snprintf(message, sizeof message, "Three rounds done: a tie at the top!");
    else snprintf(message, sizeof message, "Three rounds done: %s banked the most", name_of(g.leader()));
    next_at = now_ms + 900;
    after_change();
}

void new_game(Mode mode, Level level)
{
    // Leaving a started game against the computer for a new one counts as a loss
    if (S->mode == Mode::Computer && !S->g.over() && !S->recorded && S->g.turns > 0) {
        S->recorded = 1;
        twoplayer::Record r;
        r.mode = S->mode;
        r.level = S->level;
        r.result = twoplayer::Result::Side2;
        r.moves = S->g.turns;
        r.seconds = S->seconds;
        kit::record_two_player(kId, r, kSides);
    }
    kit::flash_stop();
    Rng rng(fresh_seed());
    S->mode = mode;
    S->level = level;
    S->recorded = 0;
    S->seconds = 0;
    S->g.start(mode == Mode::Computer ? 3 : 2, rng);
    ui_mode = Ui::Idle;
    anim_on(false);
    snprintf(message, sizeof message, "Round 1: %s first", name_of(0));
    next_at = now_ms + 900;
    after_change();
}

// ---- The computer -------------------------------------------------------------------------------
void computer_step()
{
    Game& g = S->g;
    if (!S || g.over() || overlay_open() || ui_mode != Ui::Idle || int32_t(now_ms - next_at) < 0) return;
    if (g.phase == Phase::RoundOver) return;                     // Next Round is a player's tap
    if (!computer(g.turn)) return;
    const int level = int(S->level);
    const uint32_t seed = fresh_seed();
    if (g.phase == Phase::Consonant) {
        const char c = pick_consonant(g, level, seed);
        call_letter(c);
        return;
    }
    switch (decide(g, level, seed)) {
        case Act::Spin: do_spin(); return;
        case Act::Buy:  buy_letter(pick_vowel(g, level, seed)); return;
        case Act::Solve: {
            char letters[kCols * kRows + 1];
            guess_letters(g, seed, letters, sizeof letters);
            snprintf(message, sizeof message, "%s tries to solve...", name_of(g.turn));
            try_solve(letters);
            return;
        }
    }
}

// ---- Drawing --------------------------------------------------------------------------------------
// Tiles of the current puzzle: position -> character index
void tile_chars(int8_t* at)       // at[kCols * kRows]: index into the text, -1 none
{
    int8_t pos[kCols * kRows + 1];
    const char* t = S->g.text();
    for (int i = 0; i < kCols * kRows; ++i) at[i] = -1;
    if (!wrap(t, pos, int(sizeof pos))) return;
    for (int k = 0; t[k]; ++k) if (pos[k] >= 0) at[pos[k]] = int8_t(k);
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
    const int w = lv_area_get_width(&a);
    // The puzzle board
    const lv_color_t frame = lv_color_darken(P.felt, 40), empty = lv_color_darken(P.felt, 10);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y1 + board_h - 1, frame, 6);
    const int bw = kCols * tile_w + (kCols - 1) * tile_gap;
    const int x0 = a.x1 + (w - bw) / 2, y0 = a.y1 + (board_h - (kRows * tile_h + (kRows - 1) * tile_gap)) / 2;
    int8_t at[kCols * kRows];
    tile_chars(at);
    const char* t = g.text();
    const lv_font_t* tf = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    // Revealing: the letter's tiles light up one after another
    int reveal_left = ui_mode == Ui::Revealing ? int((now_ms - anim_start) / kRevealMs) + 1 : 0;
    // Solving: the typed letters go into the hidden tiles in reading order
    int typed = 0;
    for (int i = 0; i < kCols * kRows; ++i) {
        const int x = x0 + (i % kCols) * (tile_w + tile_gap), y = y0 + (i / kCols) * (tile_h + tile_gap);
        if (at[i] < 0) { kit::fill_rect(layer, x, y, x + tile_w - 1, y + tile_h - 1, empty, 2); continue; }
        const char c = t[at[i]];
        bool show = g.shown(c);
        lv_color_t bg = P.stone_light, ink = P.stone_dark;
        char s[2] = {c, 0};
        if (ui_mode == Ui::Revealing && c == reveal_letter) {
            if (reveal_left > 0) { --reveal_left; bg = P.piece_b; }
            else show = false;
        }
        if (!show && ui_mode == Ui::Solving && is_letter(c) && !g.called_letter(c)) {
            if (typed < guess_n) { s[0] = guess[typed]; show = true; bg = lv_color_mix(P.frame, P.stone_light, 70); }
            else if (typed == guess_n) bg = lv_color_mix(P.piece_b, P.stone_light, 120);   // the next blank
            ++typed;
        }
        kit::fill_rect(layer, x, y, x + tile_w - 1, y + tile_h - 1, bg, 2);
        if (show) kit::text(layer, s, tf, ink, x, y, tile_w, tile_h);
    }
    // Category
    const lv_font_t* cf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int ch = lv_font_get_line_height(cf) + 2;
    int y = a.y1 + board_h + 1;
    kit::text(layer, g.category(), cf, P.muted, a.x1, y, w, ch);
    y += ch + (M.large ? 4 : 1);
    // Players: round money big, banked money small; the one to go edged in gold
    const lv_font_t* nf = M.large ? &lv_font_montserrat_12 : &lv_font_montserrat_10;
    const lv_font_t* mf = M.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const int nh = lv_font_get_line_height(nf), mh = lv_font_get_line_height(mf);
    const int sh = nh + mh + 6, gap = M.large ? 6 : 4;
    const int n = g.players, boxw = (w - (n - 1) * gap) / n;
    const int lead = g.over() ? g.leader() : -1;
    for (int p = 0; p < n; ++p) {
        const int x = a.x1 + p * (boxw + gap);
        const bool on = (!g.over() && g.turn == p && g.phase != Phase::RoundOver) || lead == p
                        || (g.phase == Phase::RoundOver && g.round_winner == p);
        kit::fill_rect(layer, x, y, x + boxw - 1, y + sh - 1, on ? P.lit : P.key_border, 5);
        kit::fill_rect(layer, x + 2, y + 2, x + boxw - 3, y + sh - 3, P.key, 4);
        char line[40], m[16];
        money_text(m, sizeof m, g.bank[p]);
        snprintf(line, sizeof line, "%s  %s", name_of(p), m);
        kit::text(layer, line, nf, P.muted, x, y + 3, boxw, nh);
        money_text(m, sizeof m, g.over() ? g.bank[p] : g.money[p]);
        kit::text(layer, m, mf, P.ink, x, y + 3 + nh, boxw, mh);
    }
    y += sh + (M.large ? 4 : 2);
    // What just happened
    const lv_font_t* lf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    kit::text(layer, message, lf, P.ink, a.x1, y, w, lv_font_get_line_height(lf));
}

// ---- The keyboard ---------------------------------------------------------------------------------
const char* const kKeyRows[3] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};

template <class Fn>
void each_key(int width, Fn fn)
{
    for (int r = 0; r < 3; ++r) {
        const int n = int(strlen(kKeyRows[r]));
        const int row_w = n * key_w + (n - 1) * key_gap;
        int x = (width - row_w) / 2;
        const int y = r * (key_h + key_gap);
        for (int k = 0; k < n; ++k) { fn(kKeyRows[r][k], x, y, key_w, key_h); x += key_w + key_gap; }
    }
}

// May the player tap this letter now?
bool letter_live(char c)
{
    const Game& g = S->g;
    if (!human_turn() || overlay_open()) return false;
    switch (ui_mode) {
        case Ui::Idle:    return g.phase == Phase::Consonant && g.can_call(c);
        case Ui::Buying:  return g.can_buy_letter(c);
        case Ui::Solving: return !g.called_letter(c) && guess_n < g.hidden();
        default:          return false;
    }
}

void keys_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const lv_font_t* f = key_h >= 40 ? &lv_font_montserrat_20 : &lv_font_montserrat_14;
    const Game& g = S->g;
    each_key(lv_area_get_width(&a), [&](char c, int x, int y, int w, int h) {
        const bool used = g.called_letter(c);
        const bool live = letter_live(c);
        const lv_color_t bg = used ? P.absent : live ? P.key_on : P.key;
        const lv_color_t ink = used ? contrast_text(P.absent) : live ? P.key_on_text : P.key_dim_text;
        kit::fill_rect(layer, a.x1 + x, a.y1 + y, a.x1 + x + w - 1, a.y1 + y + h - 1, live ? bg : P.key_border, 4);
        kit::fill_rect(layer, a.x1 + x + 1, a.y1 + y + 1, a.x1 + x + w - 2, a.y1 + y + h - 2, bg, 3);
        char s[2] = {c, 0};
        kit::text(layer, s, f, ink, a.x1 + x, a.y1 + y, w, h);
    });
}

void key_letter(char c)
{
    if (!letter_live(c)) return;
    Game& g = S->g;
    if (ui_mode == Ui::Solving) {
        guess[guess_n++] = c;
        guess[guess_n] = 0;
        update();
        return;
    }
    if (ui_mode == Ui::Buying) { buy_letter(c); return; }
    if (g.phase == Phase::Consonant) call_letter(c);
}

// Taps act on release at the point where the stylus came down
lv_point_t key_down{-1, -1};
void keys_pressed_cb(lv_event_t*)
{
    lv_indev_t* indev = lv_indev_active();
    if (indev) lv_indev_get_point(indev, &key_down);
}

void keys_clicked_cb(lv_event_t* e)
{
    if (!S || key_down.x < 0) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    const int px = key_down.x - a.x1, py = key_down.y - a.y1;
    char hit = 0;
    int best = 1 << 30;
    each_key(lv_area_get_width(&a), [&](char c, int x, int y, int w, int h) {
        const int dx = px < x ? x - px : px > x + w ? px - x - w : 0;
        const int dy = py < y ? y - py : py > y + h ? py - y - h : 0;
        const int d = dx * dx + dy * dy;
        if (d < best) { best = d; hit = c; }
    });
    if (hit) key_letter(hit);
}

// ---- The wheel ------------------------------------------------------------------------------------
lv_color_t wedge_color(int i, lv_color_t* ink)
{
    const Palette& P = pal();
    const int16_t v = kWheel[i];
    if (v == kBust) { *ink = P.stone_light; return P.stone_dark; }
    if (v == kSkip) { *ink = P.stone_dark; return P.stone_light; }
    switch (i % 4) {
        case 0:  *ink = P.stone_light; return P.piece_a;
        case 1:  *ink = P.stone_dark;  return P.piece_b;
        case 2:  *ink = P.stone_light; return P.frame;
        default: *ink = P.stone_light; return P.felt;
    }
}

void wedge_label(int i, char* buf, size_t cap)
{
    const int16_t v = kWheel[i];
    if (v == kBust) snprintf(buf, cap, "BUST");
    else if (v == kSkip) snprintf(buf, cap, "SKIP");
    else snprintf(buf, cap, "%d", v);
}

void wheel_draw_cb(lv_event_t* e)
{
    if (!S) return;
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const Metrics& M = metrics();
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, P.screen);
    // Where the wheel is: ease out over kSpinMs
    float t = float(now_ms - anim_start) / float(kSpinMs);
    if (t > 1) t = 1;
    const float k = 1 - (1 - t) * (1 - t) * (1 - t);
    rot = rot_from + (rot_to - rot_from) * k;
    const int w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    const int R = (w < h ? w : h) / 2 - (M.large ? 10 : 6);
    const int cx = a.x1 + w / 2, cy = a.y1 + h / 2 + (M.large ? 8 : 5);
    const float step = 360.0f / kWedges;
    kit::fill_circle(layer, cx, cy, R + 3, P.stone_dark);
    for (int i = 0; i < kWedges; ++i) {
        lv_color_t ink;
        const lv_color_t c = wedge_color(i, &ink);
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = uint16_t(R);
        d.width = R;
        d.color = c;
        const float s0 = rot + step * i;
        d.start_angle = int(floorf(s0));
        d.end_angle = int(ceilf(s0 + step)) + 1;
        lv_draw_arc(layer, &d);
    }
    // Labels: upright, near the rim
    const lv_font_t* lf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    const int lh = lv_font_get_line_height(lf), lw = M.large ? 44 : 30;
    for (int i = 0; i < kWedges; ++i) {
        lv_color_t ink;
        wedge_color(i, &ink);
        const float mid = (rot + step * i + step / 2) * 3.14159265f / 180.0f;
        const int lx = cx + int(cosf(mid) * R * 0.74f), ly = cy + int(sinf(mid) * R * 0.74f);
        char s[8];
        wedge_label(i, s, sizeof s);
        kit::text(layer, s, lf, ink, lx - lw / 2, ly - lh / 2, lw, lh);
    }
    // Hub: the wedge under the pointer
    const int hub = R * 34 / 100;
    kit::fill_circle(layer, cx, cy, hub + 2, P.stone_dark);
    kit::fill_circle(layer, cx, cy, hub, P.cell);
    const int under = wedge_at(rot);
    char s[16];
    if (kWheel[under] == kBust) snprintf(s, sizeof s, "BUST");
    else if (kWheel[under] == kSkip) snprintf(s, sizeof s, "SKIP");
    else money_text(s, sizeof s, kWheel[under]);
    const lv_font_t* hf = M.large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
    kit::text(layer, s, hf, P.ink, cx - hub, cy - hub, 2 * hub, 2 * hub);
    // The pointer, at the top
    const int pw = M.large ? 14 : 10, ph = M.large ? 24 : 16;
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    td.color = P.ink;
    td.opa = LV_OPA_COVER;
    td.p[0].x = cx - pw; td.p[0].y = cy - R - 8;
    td.p[1].x = cx + pw; td.p[1].y = cy - R - 8;
    td.p[2].x = cx;      td.p[2].y = cy - R - 8 + ph;
    lv_draw_triangle(layer, &td);
    const lv_font_t* nf = M.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    if (t >= 1) {
        const char* res = kWheel[S->g.wedge] == kBust ? "BUST! The round's money is gone"
                        : kWheel[S->g.wedge] == kSkip ? "SKIP: lose a turn" : "";
        if (res[0]) kit::text(layer, res, nf, P.ink, a.x1, a.y2 - lv_font_get_line_height(nf) - 2, w, lv_font_get_line_height(nf));
    }
}

// ---- Keys and the turn's flow -------------------------------------------------------------------
enum ActKey { kActA, kActB, kActC };

void act_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    const int k = int(intptr_t(lv_event_get_user_data(e)));
    Game& g = S->g;
    if (g.over()) { if (k == kActC) new_game(S->mode, S->level); return; }
    if (g.phase == Phase::RoundOver) { if (k == kActC) next_round(); return; }
    if (!human_turn()) return;
    switch (ui_mode) {
        case Ui::Idle:
            if (g.phase != Phase::Choose) return;
            if (k == kActA && g.can_spin()) do_spin();
            else if (k == kActB && g.can_buy()) { ui_mode = Ui::Buying; snprintf(message, sizeof message, "Pick a vowel ($%d)", kVowelCost); update(); }
            else if (k == kActC) { ui_mode = Ui::Solving; guess_n = 0; guess[0] = 0; snprintf(message, sizeof message, "Fill in the blanks, then Solve"); update(); }
            return;
        case Ui::Buying:
            if (k == kActA) { ui_mode = Ui::Idle; message[0] = 0; update(); }
            return;
        case Ui::Solving:
            if (k == kActA) { if (guess_n) guess[--guess_n] = 0; update(); }
            else if (k == kActB) { ui_mode = Ui::Idle; message[0] = 0; update(); }
            else if (k == kActC && guess_n == g.hidden()) try_solve(guess);
            return;
        default:
            return;
    }
}

void set_act(int i, bool show, const char* text, bool on, bool dim, int x, int width)
{
    lv_obj_t* k = act[i];
    if (!k) return;
    lv_obj_set_hidden(k, !show);
    if (!show) return;
    lv_label_set_text(lv_obj_get_child(k, 0), text);
    lv_obj_set_x(k, x);
    lv_obj_set_width(k, width);
    set_checked(k, on);
    set_dim(k, dim);
}

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    const Metrics& m = metrics();
    // Header
    char st[40], sh[24] = "";
    if (g.over()) {
        const int w = g.leader();
        if (w < 0) snprintf(st, sizeof st, "A tie!");
        else if (w == 0 && vs_computer()) snprintf(st, sizeof st, "You win!");
        else snprintf(st, sizeof st, "%s %s", name_of(w), vs_computer() ? "wins" : "wins!");
    } else if (g.phase == Phase::RoundOver) {
        snprintf(st, sizeof st, "Round %d of %d done", g.round + 1, kRounds);
        snprintf(sh, sizeof sh, "Round %d done", g.round + 1);
    } else if (human_turn()) {
        if (vs_computer()) snprintf(st, sizeof st, "Round %d: your turn", g.round + 1);
        else snprintf(st, sizeof st, "Round %d: %s", g.round + 1, name_of(g.turn));
        snprintf(sh, sizeof sh, vs_computer() ? "Your turn" : "%s", name_of(g.turn));
    } else {
        snprintf(st, sizeof st, "Round %d: %s's turn", g.round + 1, name_of(g.turn));
        snprintf(sh, sizeof sh, "%s's turn", name_of(g.turn));
    }
    kit::top_bar_status(bar, st, sh[0] ? sh : nullptr);
    // Action keys
    const int pad = m.large ? 6 : 3, gap = m.large ? 6 : 4;
    const int full = m.w - 2 * pad, third = (full - 2 * gap) / 3;
    const bool busy = ui_mode == Ui::Spinning || ui_mode == Ui::Revealing;
    for (int i = 0; i < 3; ++i) set_act(i, false, "", false, false, pad, third);
    char vowel[24];
    if (m.large) snprintf(vowel, sizeof vowel, "Vowel $%d", kVowelCost);
    else snprintf(vowel, sizeof vowel, "Vowel");                      // the price doesn't fit a third
    if (g.over()) set_act(kActC, true, "Play Again", true, false, pad, full);
    else if (g.phase == Phase::RoundOver) set_act(kActC, true, g.round + 1 >= kRounds ? "See Who Won" : "Next Round", true, false, pad, full);
    else if (!human_turn() || busy) { /* the computer's turn or a spin / reveal: no keys */ }
    else if (ui_mode == Ui::Buying) set_act(kActA, true, "Cancel", false, false, pad, full);
    else if (ui_mode == Ui::Solving) {
        set_act(kActA, true, "Delete", false, guess_n == 0, pad, third);
        set_act(kActB, true, "Cancel", false, false, pad + third + gap, third);
        set_act(kActC, true, "Solve", guess_n == g.hidden(), guess_n != g.hidden(), pad + 2 * (third + gap), third);
    } else if (g.phase == Phase::Choose) {
        set_act(kActA, true, "Spin", g.can_spin(), !g.can_spin(), pad, third);
        set_act(kActB, true, vowel, false, !g.can_buy(), pad + third + gap, third);
        set_act(kActC, true, "Solve", false, false, pad + 2 * (third + gap), third);
    }
    if (wheel_obj) lv_obj_set_hidden(wheel_obj, ui_mode != Ui::Spinning);
    if (board_obj) lv_obj_invalidate(board_obj);
    if (keys_obj) lv_obj_invalidate(keys_obj);
}

// Animations end, the turn goes on
void ui_tick()
{
    if (ui_mode == Ui::Spinning && now_ms - anim_start >= kSpinMs + kHoldMs) {
        ui_mode = Ui::Idle;
        anim_on(false);
        rot = fmodf(rot_to, 360.0f);
        const Game& g = S->g;
        const int16_t v = kWheel[g.wedge];
        if (v == kBust || v == kSkip) {
            if (!computer((g.turn + g.players - 1) % g.players)) sound(Sound::Error);   // the spinner's turn passed
        } else if (!computer(g.turn)) {
            char m[16];
            money_text(m, sizeof m, v);
            snprintf(message, sizeof message, "%s a letter: pick a consonant", m);
        }
        next_at = now_ms + kStepMs;
        update();
    }
    if (ui_mode == Ui::Revealing) {
        const int n = S->g.count(reveal_letter);
        if (now_ms - anim_start >= uint32_t(n) * kRevealMs + 250) {
            ui_mode = Ui::Idle;
            anim_on(false);
            update();
        }
    }
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 6 : 3, gap = m.large ? 6 : 3;
    const int kh = m.large ? 48 : 30;
    // Bottom up: the action keys, the keyboard, then the board and the rest
    const int ky = m.h - pad - kh;
    key_gap = 2;
    key_w = (m.w - 2 * pad - 9 * key_gap) / 10;
    key_h = m.large ? 42 : 27;
    const int kb_h = 3 * key_h + 2 * key_gap;
    const int kb_y = ky - gap - kb_h;
    keys_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(keys_obj);
    lv_obj_set_size(keys_obj, m.w - 2 * pad, kb_h);
    lv_obj_set_pos(keys_obj, pad, kb_y);
    lv_obj_set_clickable(keys_obj, true);
    lv_obj_add_event_cb(keys_obj, keys_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(keys_obj, keys_pressed_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(keys_obj, keys_clicked_cb, LV_EVENT_CLICKED, nullptr);
    for (int i = 0; i < 3; ++i) {
        act[i] = make_key(scr, 40, kh, act_cb, i);
        key_label(act[i], "", menu_font());
        lv_obj_set_pos(act[i], pad, ky);
    }
    const int top = bar.h + (m.large ? 2 : 1);
    const int area_h = kb_y - gap - top;
    // The rows under the board: category, players, message
    const int ch = lv_font_get_line_height(m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12) + 2;
    const int sh = lv_font_get_line_height(m.large ? &lv_font_montserrat_12 : &lv_font_montserrat_10)
                 + lv_font_get_line_height(m.large ? &lv_font_montserrat_20 : &lv_font_montserrat_14) + 6;
    const int lh = lv_font_get_line_height(m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    const int under = ch + (m.large ? 4 : 1) + sh + (m.large ? 4 : 2) + lh + 1;
    board_h = area_h - under;
    tile_gap = m.large ? 3 : 2;
    tile_w = (m.w - 2 * pad - 8 - (kCols - 1) * tile_gap) / kCols;
    tile_h = (board_h - 6 - (kRows - 1) * tile_gap) / kRows;
    if (tile_h > tile_w * 3 / 2) tile_h = tile_w * 3 / 2;
    board_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(board_obj);
    lv_obj_set_size(board_obj, m.w - 2 * pad, area_h);
    lv_obj_set_pos(board_obj, pad, top);
    lv_obj_add_event_cb(board_obj, board_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    // The wheel: over everything while it turns
    wheel_obj = lv_obj_create(scr);
    lv_obj_remove_style_all(wheel_obj);
    lv_obj_set_size(wheel_obj, m.w, m.h - top);
    lv_obj_set_pos(wheel_obj, 0, top);
    lv_obj_add_event_cb(wheel_obj, wheel_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_set_hidden(wheel_obj, true);
    clock_ = kit::Clock{};
    update();
}

// ---- Menu ---------------------------------------------------------------------------------
void stats_back() { open_menu(); }
void open_stats() { kit::stats_two_player(kId, kSides, stats_back); }

void menu_pick(int id)
{
    if (id == kit::kPassAndPlay) new_game(Mode::PassAndPlay, S->level);
    else if (id >= kit::kLevel0 && id <= kit::kLevel2) new_game(Mode::Computer, Level(id));
}
void menu_back() { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_two_player("Wheel of CYD", h);
}

// ---- Registry entry --------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) {
        *S = State{};
        Rng rng(fresh_seed());
        S->g.start(3, rng);
        snprintf(message, sizeof message, "Round 1: %s first", name_of(0));
    } else {
        message[0] = 0;
    }
    ui_mode = Ui::Idle;
    guess_n = 0;
    next_at = 0;
    build();
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    anim_on(false);
    ui_mode = Ui::Idle;
    save();
    bar = kit::TopBar{};
    board_obj = keys_obj = wheel_obj = nullptr;
    act[0] = act[1] = act[2] = nullptr;
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
    ui_tick();
    computer_step();
    if (kit::save_due(now, last_save_ms, S->seconds)) { last_save_ms = now; save(); }
}

void restyle() { if (S) { anim_on(false); if (ui_mode == Ui::Spinning || ui_mode == Ui::Revealing) ui_mode = Ui::Idle; build(); } }

bool summary(char* buf, size_t cap)
{
    State* tmp = nullptr;
    const State* st = S;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    char m[16];
    money_text(m, sizeof m, st->g.bank[0]);
    if (st->g.over()) snprintf(buf, cap, "Game over, you banked %s", m);
    else snprintf(buf, cap, "Round %d of %d, %s banked %s", st->g.round + 1, kRounds,
                  st->mode == Mode::Computer ? "you" : "Player 1", m);
    delete tmp;
    return true;
}

void save_now() { save(); }

void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int cx = a.x1 + s / 2, cy = a.y1 + s / 2 + s / 20, R = s * 44 / 100;
    kit::fill_circle(layer, cx, cy, R + 2, P.stone_dark);
    const lv_color_t cols[4] = {P.piece_a, P.piece_b, P.frame, P.felt};
    for (int i = 0; i < 8; ++i) {
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.center.x = cx;
        d.center.y = cy;
        d.radius = uint16_t(R);
        d.width = R;
        d.color = cols[i % 4];
        d.start_angle = i * 45;
        d.end_angle = i * 45 + 46;
        lv_draw_arc(layer, &d);
    }
    kit::fill_circle(layer, cx, cy, R / 3, P.cell);
    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    td.color = P.ink;
    td.opa = LV_OPA_COVER;
    td.p[0].x = cx - s / 12; td.p[0].y = a.y1;
    td.p[1].x = cx + s / 12; td.p[1].y = a.y1;
    td.p[2].x = cx;          td.p[2].y = a.y1 + s / 6;
    lv_draw_triangle(layer, &td);
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
extern const GameOps wheel_ops;
const GameOps wheel_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games

#ifdef CYD_PREVIEW
namespace wheel_preview {
wheel::Game* game() { return S ? &S->g : nullptr; }
void key(char c) { key_letter(c); }
void action(int k) { if (S && act[k]) lv_obj_send_event(act[k], LV_EVENT_CLICKED, nullptr); }   // a tap on action key k
void set_message(const char* m) { snprintf(message, sizeof message, "%s", m); update(); }
} // namespace wheel_preview
#endif
