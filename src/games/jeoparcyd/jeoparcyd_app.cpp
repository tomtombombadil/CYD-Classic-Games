// Jeopar-CYD!: registry entry, save file and screen. Rules and the
// computers in jeoparcyd_core.*, questions from the shared trivia bank
// (Open Trivia DB, CC BY-SA 4.0 - see common/trivia_data.cpp).
//
// The board: one row per category (its name on the left, then the five
// values - categories as rows so the names fit a portrait screen), the
// three scores under it, the picker's in gold. Tap a value when it's your
// pick. A Daily Double asks for your wager with four keys.
// The clue: category and value, the question, four answers, a time bar
// and who's buzzing. Tapping an answer IS buzzing in. A computer that
// buzzes first shows its pick in gold, then right or wrong; a wrong
// answer turns red and the clue is open again for the rest. Final: wager
// with four keys, then answer. Nothing asks "are you sure".
//
// Sounds: your right answer (Place), wrong ("aww"), a computer's right
// answer (Turn), a Daily Double (Call), the end (Win / Lose).
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/trivia_bank.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "jeoparcyd_core.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace jcyd;
using namespace ui;

constexpr const char* kId = "jeoparcyd";
const char* const kNames[kPlayers] = {"You", "Max", "Zoe"};
const char* const kLevels[3] = {"Easy", "Medium", "Hard"};
constexpr uint32_t kClueMs = 12000, kBuzzShowMs = 1400, kLockMs = 700, kRevealMs = 2800, kCpuPickMs = 1300;
constexpr uint32_t kReadBaseMs = 3000, kReadPerCharMs = 55, kReadMinMs = 5000, kReadMaxMs = 16000, kReopenReadMs = 2500;

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
lv_obj_t*   board = nullptr;          // custom-drawn board + scores
lv_obj_t*   clue = nullptr;           // the clue page (labels and keys)
lv_obj_t*   head = nullptr;
lv_obj_t*   qlabel = nullptr;
lv_obj_t*   ans[4] = {};
lv_obj_t*   ans_text[4] = {};
lv_obj_t*   timebar = nullptr;
lv_obj_t*   status = nullptr;
lv_obj_t*   keys[4] = {};             // wagers / Play Again
int         pad = 4, cell_w = 30, cell_h = 30, name_w = 80, board_top = 0, scores_y = 0, ans_w = 0, q_w = 0;
int         keys_y = 0, life_y = 0;
uint32_t    now_ms = 0, clue_start = 0, wait_until = 0, last_save_ms = 0, paused_at = 0;
uint32_t    read_ms = kReadMinMs;            // nobody else buzzes while you read the clue
bool        frozen = false;
lv_point_t  press_pt{0, 0};
// the clue's moment-to-moment state (not saved: a reopened clue starts its clock again)
int8_t      locking = -1;             // the player whose answer shows gold
int8_t      lock_slot = -1;
uint8_t     final_step = 0;           // 0 wager, 1 clue

void open_menu();
void update();

void money(char* buf, size_t cap, int32_t v)
{
    const char* sign = v < 0 ? "-" : "";
    const int32_t a = v < 0 ? -v : v;
    if (a >= 1000) snprintf(buf, cap, "%s$%ld,%03ld", sign, long(a / 1000), long(a % 1000));
    else snprintf(buf, cap, "%s$%ld", sign, long(a));
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

void record()
{
    Record r;
    r.place = uint8_t(S->g.place(0));
    r.score = S->g.score[0];
    r.level = S->g.level;
    r.seconds = S->seconds;
    char body[96];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

// ---- Board drawing ----------------------------------------------------------------------------
void draw_wrapped(lv_layer_t* layer, const char* s, const lv_font_t* f, lv_color_t c, int x1, int y1, int w, int h)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.text = s;
    d.text_local = 1;
    d.font = f;
    d.color = c;
    d.align = LV_TEXT_ALIGN_LEFT;
    lv_point_t sz;
    lv_text_get_size(&sz, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    const int top = y1 + (h - sz.y) / 2;
    lv_area_t a{x1, top > y1 ? top : y1, x1 + w - 1, y1 + h - 1};
    lv_draw_label(layer, &d, &a);
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
    const lv_font_t* nf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_10;
    const lv_font_t* vf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const lv_color_t blue = lv_color_mix(P.frame, P.stone_dark, 200);
    char t[24];
    if (g.phase != Phase::Over && g.round < 2) {
        for (int c = 0; c < kCats; ++c) {
            const int y = a.y1 + board_top + c * (cell_h + 2);
            kit::fill_rect(layer, a.x1 + pad, y, a.x1 + pad + name_w - 3, y + cell_h - 1, blue, 3);
            draw_wrapped(layer, trivia::category_short(g.cat[c]), nf, P.stone_light, a.x1 + pad + 3, y + 1, name_w - 6, cell_h - 2);
            for (int r = 0; r < kRows; ++r) {
                const int x = a.x1 + pad + name_w + r * (cell_w + 2);
                const Cell& cl = g.cell[c][r];
                const bool now = g.cur_c == c && g.cur_r == r && g.phase != Phase::Board;
                kit::fill_rect(layer, x, y, x + cell_w - 1, y + cell_h - 1, now ? P.lit : cl.used ? lv_color_mix(blue, P.screen, 90) : blue, 3);
                if (!cl.used) {
                    snprintf(t, sizeof t, "%ld", long(g.value(r)));
                    kit::text(layer, t, vf, P.lit, x, y, cell_w, cell_h);
                }
            }
        }
    } else if (g.phase == Phase::Over) {
        // the results, best first
        const lv_font_t* bf = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const int lh = lv_font_get_line_height(bf);
        int order[kPlayers] = {0, 1, 2};
        for (int i = 0; i < kPlayers; ++i)
            for (int j = i + 1; j < kPlayers; ++j)
                if (g.score[order[j]] > g.score[order[i]]) { const int tt = order[i]; order[i] = order[j]; order[j] = tt; }
        int y = a.y1 + board_top + lh;
        for (int i = 0; i < kPlayers; ++i) {
            const int p = order[i];
            char m[20];
            money(m, sizeof m, g.score[p]);
            snprintf(t, sizeof t, "%s  %s", kNames[p], m);
            kit::text(layer, t, bf, i == 0 ? P.ink : P.muted, a.x1, y, lv_area_get_width(&a), lh);
            y += lh + 8;
        }
    } else {
        // Final: the category
        const lv_font_t* bf = large ? &lv_font_montserrat_28 : &lv_font_montserrat_20;
        const int lh = lv_font_get_line_height(bf);
        kit::text(layer, "Final", bf, P.muted, a.x1, a.y1 + board_top + lh, lv_area_get_width(&a), lh);
        kit::text(layer, trivia::category_name(g.final_cat), bf, P.ink, a.x1, a.y1 + board_top + 2 * lh + 6, lv_area_get_width(&a), lh);
    }
    // scores
    const lv_font_t* sf = large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int sw = (lv_area_get_width(&a) - 2 * pad) / kPlayers;
    const int sh = lv_font_get_line_height(sf) * 2 + 6;
    for (int p = 0; p < kPlayers; ++p) {
        const int x = a.x1 + pad + p * sw;
        const int y = a.y1 + scores_y;
        const bool pick = p == g.chooser && g.phase != Phase::Over && g.round < 2;
        kit::fill_rect(layer, x + 1, y, x + sw - 2, y + sh, pick ? P.lit : P.cell, 5);
        char m[20];
        money(m, sizeof m, g.score[p]);
        kit::text(layer, kNames[p], sf, pick ? P.stone_dark : P.muted, x, y + 2, sw, lv_font_get_line_height(sf));
        kit::text(layer, m, sf, pick ? P.stone_dark : (g.score[p] < 0 ? P.piece_a : P.ink), x, y + 2 + lv_font_get_line_height(sf), sw, lv_font_get_line_height(sf));
    }
}

// ---- Clue page --------------------------------------------------------------------------------
const lv_font_t* q_font() { return metrics().large ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }

int text_h(const char* s, const lv_font_t* f, int w)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return p.y;
}

bool clue_showing()
{
    const Phase ph = S->g.phase;
    return ph == Phase::Clue || ph == Phase::Reveal || (ph == Phase::FinalWager && final_step == 1) || ph == Phase::FinalClue ||
           (ph == Phase::Over && S->g.round == 2 && int32_t(now_ms - wait_until) < 0);   // the Final's answer stays a moment
}

void layout_clue()
{
    const Game& g = S->g;
    if (g.q < 0 || !trivia::get(g.q, S->q)) return;
    char t[64], m[20];
    if (g.round == 2) snprintf(t, sizeof t, "Final: %s", trivia::category_name(g.final_cat));
    else {
        money(m, sizeof m, g.wager ? g.wager : g.value(g.cur_r));
        snprintf(t, sizeof t, "%s%s - %s", g.wager ? "Daily Double! " : "", trivia::category_name(g.cat[g.cur_c]), m);
    }
    lv_label_set_text(head, t);
    lv_label_set_text(qlabel, S->q.text);
    const int top = lv_obj_get_y(head) + lv_font_get_line_height(metrics().large ? &lv_font_montserrat_14 : &lv_font_montserrat_12) + pad;
    lv_obj_set_pos(qlabel, pad, top);
    int y = top + text_h(S->q.text, q_font(), q_w) + pad;
    const int min_h = menu_btn_h();
    const int avail = life_y - pad - y;
    int need = 0, hs[4];
    for (int s = 0; s < 4; ++s) {
        char a[140];
        snprintf(a, sizeof a, "%c: %s", 'A' + s, S->q.answer[g.order[s]]);
        lv_label_set_text(ans_text[s], a);
        const int h = text_h(a, menu_font(), ans_w - 24) + 10;
        hs[s] = h > min_h ? h : min_h;
        need += hs[s] + pad;
    }
    // squeeze the keys a little if the question is long (text stays the same size)
    const int squeeze = need > avail ? (need - avail + 3) / 4 : 0;
    for (int s = 0; s < 4; ++s) {
        const int h = hs[s] - squeeze > 24 ? hs[s] - squeeze : 24;
        lv_obj_set_size(ans[s], ans_w, h);
        lv_obj_set_pos(ans[s], pad, y);
        y += h + pad;
    }
}

void paint_clue()
{
    const Game& g = S->g;
    const Palette& P = pal();
    const bool reveal = g.phase == Phase::Reveal || g.phase == Phase::Over;
    const int right = g.right_slot();
    for (int s = 0; s < 4; ++s) {
        lv_obj_remove_local_style_prop(ans[s], LV_STYLE_BG_COLOR, 0);
        lv_obj_remove_local_style_prop(ans_text[s], LV_STYLE_TEXT_COLOR, 0);
        lv_color_t bg = P.key, ink = P.ink;
        bool paint = false;
        if (reveal && s == right) { bg = P.win; ink = P.stone_light; paint = true; }
        else if (g.wrong_slots >> s & 1) { bg = P.piece_a; ink = P.stone_light; paint = true; }
        else if (lock_slot == s) { bg = P.lit; ink = P.stone_dark; paint = true; }
        else if (g.round == 2 && g.final_slot[0] == s) { bg = P.lit; ink = P.stone_dark; paint = true; }
        if (paint) { lv_obj_set_style_bg_color(ans[s], bg, 0); lv_obj_set_style_text_color(ans_text[s], ink, 0); }
    }
}

// ---- Keys under the board (wagers, Play Again) ------------------------------------------------
int32_t wager_for(int k)
{
    const Game& g = S->g;
    if (g.phase == Phase::Wager) {
        const int32_t top = g.max_wager(0);
        const int32_t w[4] = {5, top / 2, top, g.value(g.cur_r)};
        return w[k];
    }
    const int32_t s = g.score[0];
    const int32_t w[4] = {0, s / 4, s / 2, s};
    return w[k];
}

void set_keys()
{
    const Game& g = S->g;
    const bool wager = g.phase == Phase::Wager && g.chooser == 0;
    const bool final_bet = g.phase == Phase::FinalWager && final_step == 0 && g.in_final(0);
    const bool over = g.phase == Phase::Over;
    for (int k = 0; k < 4; ++k) {
        bool show = false;
        char t[24], m[20];
        if (wager || final_bet) {
            show = true;
            money(m, sizeof m, wager_for(k));
            if (wager) { static const char* const n[4] = {"Least", "Half", "All", "Its Value"}; snprintf(t, sizeof t, "%s %s", n[k], m); }
            else { static const char* const n[4] = {"Nothing", "Quarter", "Half", "All"}; snprintf(t, sizeof t, "%s %s", n[k], k ? m : ""); }
        } else if (over && k == 0) { show = true; snprintf(t, sizeof t, "Play Again"); }
        lv_obj_set_hidden(keys[k], !show);
        if (show) lv_label_set_text(lv_obj_get_child(keys[k], 0), t);
        set_checked(keys[k], over || k == 2);
    }
    // Play Again is one wide key
    if (over) { lv_obj_set_width(keys[0], metrics().w - 2 * pad); lv_obj_set_y(keys[0], life_y); }
    else {
        const int kw = (metrics().w - 3 * pad) / 2;
        for (int k = 0; k < 4; ++k) { lv_obj_set_width(keys[k], kw); lv_obj_set_pos(keys[k], pad + (k & 1) * (kw + pad), keys_y + (k >> 1) * (menu_btn_h() + pad)); }
    }
}

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    char s[48], sh[24];
    switch (g.phase) {
        case Phase::Board:
            if (g.chooser == 0) { snprintf(s, sizeof s, "Your pick: tap a value"); snprintf(sh, sizeof sh, "Your pick"); }
            else { snprintf(s, sizeof s, "%s is picking...", kNames[g.chooser]); snprintf(sh, sizeof sh, "%s picks", kNames[g.chooser]); }
            break;
        case Phase::Wager:
            snprintf(s, sizeof s, g.chooser == 0 ? "Daily Double! Your wager" : "Daily Double for %s", kNames[g.chooser]);
            snprintf(sh, sizeof sh, "Daily Double!");
            break;
        case Phase::Over: {
            const int p = g.place(0);
            snprintf(s, sizeof s, p == 1 ? "You win!" : "Game over");
            snprintf(sh, sizeof sh, "%.23s", s);
            break;
        }
        case Phase::FinalWager:
        case Phase::FinalClue:
            snprintf(s, sizeof s, final_step == 0 && g.in_final(0) ? "Final: your wager" : "Final");
            snprintf(sh, sizeof sh, "Final");
            break;
        default:
            snprintf(s, sizeof s, g.round == 0 ? "Round 1" : "Double Round");
            snprintf(sh, sizeof sh, "%.23s", s);
            break;
    }
    kit::top_bar_status(bar, s, sh);
    const bool cl = clue_showing();
    lv_obj_set_hidden(clue, !cl);
    lv_obj_set_hidden(board, cl);
    if (cl) { layout_clue(); paint_clue(); }
    if (timebar) lv_obj_set_hidden(timebar, g.round == 2 || g.phase != Phase::Clue);
    set_keys();
    if (board) lv_obj_invalidate(board);
}

void set_status(const char* t) { if (status) lv_label_set_text(status, t); }

// ---- Flow -------------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void game_over()
{
    if (!S->recorded) record();
    const bool won = S->g.place(0) == 1;
    sound(won ? Sound::Win : Sound::Lose);
    if (won) kit::flash();
}

// Reading time from the clue's length (question and answers): ~18 characters a second
uint32_t reading_time()
{
    const Game& g = S->g;
    if (g.q < 0 || !trivia::get(g.q, S->q)) return kReadMinMs;
    size_t chars = strlen(S->q.text);
    for (int a = 0; a < S->q.answers; ++a) chars += strlen(S->q.answer[a]);
    uint32_t t = kReadBaseMs + uint32_t(chars) * kReadPerCharMs;
    return t < kReadMinMs ? kReadMinMs : t > kReadMaxMs ? kReadMaxMs : t;
}

void start_clue_clock() { clue_start = now_ms; read_ms = reading_time(); locking = lock_slot = -1; set_status(""); }

void after_answer(int p)
{
    Game& g = S->g;
    if (g.phase == Phase::Reveal) {
        if (g.last_right) { sound(p == 0 ? Sound::Place : Sound::Turn); char t[40]; snprintf(t, sizeof t, "%s got it!", kNames[p]); set_status(t); }
        else { sound(p == 0 ? Sound::Error : Sound::Turn); set_status(p == 0 ? "Wrong - no one got it" : "No one got it"); }
        wait_until = now_ms + kRevealMs;
    } else {
        // wrong: open again for the others
        if (p == 0) sound(Sound::Error);
        char t[40];
        snprintf(t, sizeof t, "%s was wrong", kNames[p]);
        set_status(t);
        g.plan_clue();
        clue_start = now_ms;
        read_ms = kReopenReadMs;
    }
    locking = lock_slot = -1;
    save();
    update();
}

void new_game(int level)
{
    kit::flash_stop();
    if (S->g.phase != Phase::Over && !S->recorded && (S->g.round > 0 || S->g.clues_left() < kCats * kRows - 3)) record();
    S->g.start(seed_now(), level);
    S->recorded = 0;
    S->seconds = 0;
    final_step = 0;
    locking = lock_slot = -1;
    wait_until = now_ms + 600;
    save();
    update();
}

void answer_cb(lv_event_t* e)
{
    if (!S || overlay_open() || locking >= 0) return;
    Game& g = S->g;
    const int s = int(intptr_t(lv_event_get_user_data(e)));
    if (g.round == 2) {
        if (!g.in_final(0) || g.final_slot[0] >= 0) return;
        g.final_answer(0, s);
        for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_answer(p, g.cpu_final_slot(p));
        if (g.phase == Phase::Over) wait_until = now_ms + kRevealMs + 800;
        save();
        update();
        return;
    }
    if (g.phase != Phase::Clue || (g.tried & 1) || (g.wrong_slots >> s & 1)) return;
    if (g.wager && g.chooser != 0) return;
    locking = 0;
    lock_slot = int8_t(s);
    wait_until = now_ms + kLockMs;
    update();
}

void key_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    Game& g = S->g;
    const int k = int(intptr_t(lv_event_get_user_data(e)));
    if (g.phase == Phase::Over) { new_game(g.level); return; }
    if (g.phase == Phase::Wager && g.chooser == 0) {
        g.set_wager(wager_for(k));
        start_clue_clock();
        save();
        update();
    } else if (g.phase == Phase::FinalWager && final_step == 0 && g.in_final(0)) {
        g.final_bet(0, wager_for(k));
        for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_bet(p, g.cpu_final_wager(p));
        final_step = 1;
        set_status("");
        save();
        update();
    }
}

void press_cb(lv_event_t*) { lv_indev_get_point(lv_indev_active(), &press_pt); }

// A tap acts on release, where the stylus came down
void board_cb(lv_event_t*)
{
    if (!S || overlay_open() || !board) return;
    Game& g = S->g;
    if (g.phase != Phase::Board || g.chooser != 0 || int32_t(now_ms - wait_until) < 0) return;
    const lv_point_t pt = press_pt;
    lv_area_t a;
    lv_obj_get_coords(board, &a);
    const int c = (pt.y - a.y1 - board_top) / (cell_h + 2);
    const int r = (pt.x - a.x1 - pad - name_w) / (cell_w + 2);
    if (c < 0 || c >= kCats || r < 0 || r >= kRows || pt.x < a.x1 + pad + name_w) return;
    if (!g.pick(c, r)) return;
    if (g.phase == Phase::Wager) sound(Sound::Call);
    else start_clue_clock();
    save();
    update();
}

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    pad = m.large ? 8 : 4;
    const int kh = menu_btn_h();
    const int top = bar.h;
    life_y = m.h - pad - kh;
    // the board: 6 rows, and the scores; wager keys (two rows) under it
    const lv_font_t* sf = m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12;
    const int sh = lv_font_get_line_height(sf) * 2 + 6;
    keys_y = m.h - 2 * (kh + pad);
    name_w = m.w * 30 / 100;
    cell_w = (m.w - 2 * pad - name_w - 8) / kRows;
    board_top = pad;
    const int rows_room = keys_y - top - pad - sh - 3 * pad;
    cell_h = rows_room / kCats - 2;
    if (cell_h > (m.large ? 50 : 34)) cell_h = m.large ? 50 : 34;
    scores_y = board_top + kCats * (cell_h + 2) + pad;
    board = lv_obj_create(scr);
    lv_obj_remove_style_all(board);
    lv_obj_set_size(board, m.w, scores_y + sh + 2);
    lv_obj_set_pos(board, 0, top);
    lv_obj_set_clickable(board, true);
    lv_obj_add_event_cb(board, board_draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(board, press_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(board, board_cb, LV_EVENT_CLICKED, nullptr);
    // the clue page
    clue = lv_obj_create(scr);
    lv_obj_remove_style_all(clue);
    lv_obj_set_size(clue, m.w, m.h - top);
    lv_obj_set_pos(clue, 0, top);
    head = lv_label_create(clue);
    lv_obj_set_style_text_font(head, sf, 0);
    lv_obj_set_style_text_color(head, pal().muted, 0);
    lv_label_set_long_mode(head, LV_LABEL_LONG_DOT);
    lv_obj_set_width(head, m.w - 2 * pad);
    lv_obj_set_pos(head, pad, pad);
    q_w = m.w - 2 * pad;
    qlabel = lv_label_create(clue);
    lv_obj_set_style_text_font(qlabel, q_font(), 0);
    lv_obj_set_style_text_color(qlabel, pal().ink, 0);
    lv_label_set_long_mode(qlabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(qlabel, q_w);
    ans_w = m.w - 2 * pad;
    for (int s = 0; s < 4; ++s) {
        ans[s] = make_key(clue, ans_w, kh, answer_cb, s);
        ans_text[s] = lv_label_create(ans[s]);
        lv_obj_set_style_text_font(ans_text[s], menu_font(), 0);
        lv_label_set_long_mode(ans_text[s], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ans_text[s], ans_w - 24);
        lv_obj_align(ans_text[s], LV_ALIGN_LEFT_MID, 10, 0);
    }
    life_y = m.h - top - pad - kh;
    status = lv_label_create(clue);
    lv_obj_set_style_text_font(status, sf, 0);
    lv_obj_set_style_text_color(status, pal().ink, 0);
    lv_label_set_text(status, "");
    lv_obj_set_width(status, m.w - 2 * pad);
    lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(status, pad, life_y + 2);
    timebar = lv_obj_create(clue);
    lv_obj_remove_style_all(timebar);
    lv_obj_set_style_bg_opa(timebar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(timebar, pal().lit, 0);
    lv_obj_set_style_radius(timebar, 3, 0);
    lv_obj_set_size(timebar, m.w - 2 * pad, 6);
    lv_obj_set_pos(timebar, pad, life_y + kh - 8);
    // keys
    for (int k = 0; k < 4; ++k) {
        keys[k] = make_key(scr, 10, kh, key_cb, k);
        key_label(keys[k], "", menu_font());
    }
    life_y = m.h - pad - kh;
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
        char a[12], b[12], c[12], d[20];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        snprintf(b, sizeof b, "%lu", (unsigned long)sum.wins);
        snprintf(c, sizeof c, "%lu%%", (unsigned long)(sum.wins * 100 / sum.games));
        money(d, sizeof d, sum.best);
        table_add(t, a, b, c, d);
        const char* const head2[4] = {"Games", "Wins", "Won", "Best"};
        static const int8_t pct[4] = {22, 20, 22, 36};
        table_show(t, head2, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[8], s2[20], tm[16];
            snprintf(s1, sizeof s1, "%u", unsigned(r.place));
            money(s2, sizeof s2, r.score);
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, kLevels[r.level < 3 ? r.level : 0], tm);
        }
        const char* const head3[4] = {"Place", "Score", "Level", "Time"};
        static const int8_t pct2[4] = {18, 32, 24, 26};
        table_show(rt, head3, pct2, hf, hf);
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

void menu_pick(int id) { if (id >= kit::kLevel0 && id <= kit::kLevel2) new_game(id - kit::kLevel0); }
void menu_back() { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Jeopar-CYD!", kLevels, h, false);
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    if (!load(*S)) { *S = State{}; S->g.start(seed_now(), 1); }
    final_step = S->g.phase == Phase::FinalClue ? 1 : 0;
    locking = lock_slot = -1;
    wait_until = 0;
    build();
    clue_start = 0;                     // a clue showing starts its clock again
    read_ms = S->g.phase == Phase::Clue ? reading_time() : kReadMinMs;
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    trivia::release();
    bar = kit::TopBar{};
    board = clue = head = qlabel = timebar = status = nullptr;
    for (int s = 0; s < 4; ++s) ans[s] = ans_text[s] = keys[s] = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S || frozen) return;
    now_ms = now;
    Game& g = S->g;
    if (clue_start == 0) clue_start = now;
    if (overlay_open()) {                               // nothing runs behind a menu
        if (!paused_at) paused_at = now;
        clock_.tick(now, false, S->seconds);
        return;
    }
    if (paused_at) {
        const uint32_t d = now - paused_at;
        clue_start += d; wait_until += d;
        paused_at = 0;
    }
    const bool waiting = int32_t(now - wait_until) < 0;
    switch (g.phase) {
        case Phase::Board:
            if (!waiting && g.chooser != 0) {
                int c, r;
                g.pick_cell(g.level, &c, &r);
                if (g.pick(c, r)) {
                    if (g.phase == Phase::Wager) { sound(Sound::Call); wait_until = now + kCpuPickMs; }
                    else start_clue_clock();
                    save();
                    update();
                }
            }
            break;
        case Phase::Wager:
            if (!waiting && g.chooser != 0) { g.set_wager(g.cpu_wager(g.chooser)); start_clue_clock(); save(); update(); }
            break;
        case Phase::Clue: {
            if (locking >= 0) {                              // an answer showing gold
                if (!waiting) { const int p = locking; g.answer(p, lock_slot); after_answer(p); }
                break;
            }
            // reading first: the bar and the others start after it
            const uint32_t since = now - clue_start;
            if (since < read_ms) {
                if (timebar && lv_obj_get_width(timebar) != metrics().w - 2 * pad) lv_obj_set_width(timebar, metrics().w - 2 * pad);
                break;
            }
            const uint32_t t = since - read_ms;
            if (timebar) {
                const int full = metrics().w - 2 * pad;
                const int w = t >= kClueMs ? 0 : int(int64_t(full) * (kClueMs - t) / kClueMs);
                if (lv_obj_get_width(timebar) != w) lv_obj_set_width(timebar, w > 0 ? w : 1);
            }
            // a computer buzzing in
            int first = -1;
            for (int p = 1; p < kPlayers; ++p)
                if (g.plan[p].buzz_ms && t >= g.plan[p].buzz_ms && (first < 0 || g.plan[p].buzz_ms < g.plan[first].buzz_ms)) first = p;
            if (first > 0) {
                char s[40];
                snprintf(s, sizeof s, "%s buzzes in: %c", kNames[first], 'A' + g.plan[first].slot);
                set_status(s);
                locking = int8_t(first);
                lock_slot = g.plan[first].slot;
                wait_until = now + kBuzzShowMs;
                update();
            } else if (t >= kClueMs) {
                g.time_up();
                sound(g.wager && g.chooser == 0 ? Sound::Error : Sound::Turn);
                set_status("Time's up");
                wait_until = now + kRevealMs;
                save();
                update();
            }
            break;
        }
        case Phase::Reveal:
            if (!waiting) {
                const uint8_t round = g.round;
                g.done_revealing();
                if (g.round != round && g.round == 1) sound(Sound::Trill);
                final_step = 0;
                if (g.phase == Phase::FinalWager && !g.in_final(0)) {
                    for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_bet(p, g.cpu_final_wager(p));
                    final_step = 1;
                    wait_until = now + 2500;
                } else if (g.phase == Phase::Over) game_over();
                else wait_until = now + 500;
                save();
                update();
            }
            break;
        case Phase::FinalWager:
        case Phase::FinalClue:
            if (!g.in_final(0) && final_step == 1 && !waiting) {     // you sit out: the others answer
                for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_answer(p, g.cpu_final_slot(p));
                if (g.phase == Phase::Over) { wait_until = now + kRevealMs + 800; game_over(); }
                save();
                update();
            }
            break;
        case Phase::Over:
            if (lv_obj_is_hidden(board) && !waiting) update();   // the Final's answer has been seen
            break;
    }
    if (g.phase == Phase::Over && !S->recorded) game_over();
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
    char m[20];
    money(m, sizeof m, st->g.score[0]);
    if (st->g.phase == Phase::Over) snprintf(buf, cap, "Game over: you have %s", m);
    else snprintf(buf, cap, "%s, %s: you have %s", kLevels[st->g.level], st->g.round == 0 ? "Round 1" : st->g.round == 1 ? "Double Round" : "Final", m);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a little board of blue value tiles, one gold
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    const int cw = s / 3, ch = s / 3;
    const lv_color_t blue = lv_color_mix(P.frame, P.stone_dark, 200);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            const int x = a.x1 + c * cw, y = a.y1 + r * ch;
            kit::fill_rect(layer, x + 1, y + 1, x + cw - 2, y + ch - 2, r == 1 && c == 1 ? P.lit : blue, 3);
            if (!(r == 1 && c == 1)) kit::text(layer, "$", &lv_font_montserrat_12, P.lit, x, y, cw, ch);
        }
    kit::text(layer, "?", &lv_font_montserrat_20, P.stone_dark, a.x1 + cw, a.y1 + ch, cw, ch);
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

namespace jeoparcyd_preview {
// You: pick when it's your pick, bet the least, answer (A) only the Final; the computers do the rest
int robot()
{
    if (!S) return -1;
    Game& g = S->g;
    if (g.phase == Phase::Over) return 2;
    if (int32_t(now_ms - wait_until) < 0) return 0;
    if (g.phase == Phase::Board && g.chooser == 0) {
        int c, r;
        g.pick_cell(1, &c, &r);
        if (g.pick(c, r)) { if (g.phase == Phase::Clue) start_clue_clock(); save(); update(); }
        return 1;
    }
    if (g.phase == Phase::Wager && g.chooser == 0) { g.set_wager(5); start_clue_clock(); update(); return 1; }
    if (g.phase == Phase::FinalWager && final_step == 0 && g.in_final(0)) {
        g.final_bet(0, 0);
        for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_bet(p, g.cpu_final_wager(p));
        final_step = 1;
        update();
        return 1;
    }
    if ((g.phase == Phase::FinalWager || g.phase == Phase::FinalClue) && final_step == 1 && g.in_final(0) && g.final_slot[0] < 0) {
        g.final_answer(0, 0);
        for (int p = 1; p < kPlayers; ++p) if (g.in_final(p)) g.final_answer(p, g.cpu_final_slot(p));
        if (g.phase == Phase::Over) wait_until = now_ms + kRevealMs;
        update();
        return 1;
    }
    return 0;
}
int clues() { return S ? kCats * kRows * (S->g.round + 1) - S->g.clues_left() : 0; }
uint32_t reading() { return read_ms; }
jcyd::Game* game() { return S ? &S->g : nullptr; }
void hold(bool on) { frozen = on; }
void refresh() { update(); }
void status_line(const char* t) { set_status(t); }
void final_clue() { final_step = 1; update(); }
}

namespace games {
extern const GameOps jeoparcyd_ops;
const GameOps jeoparcyd_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
