// Who Wants To Be A CYD?: registry entry, save file and screen. Rules in
// whowants_core.*, questions from the shared trivia bank (Open Trivia DB,
// CC BY-SA 4.0 - see common/trivia_data.cpp).
//
// Screen, top to bottom: the question's money and a Walk Away key (top
// right), the question, four answer keys (A-D, wrapped text; Ask the
// Audience adds each answer's percent on its right), and the three
// lifelines along the bottom (Play Again there once the game is over).
// A tapped answer turns gold for a moment, then green (right) or red
// (wrong, and the right one turns green) - no "final answer?" question:
// keys act on the first tap. A right answer moves up after a moment.
// Questions too long for this screen are skipped.
//
// Sounds: right (Place), a safe amount reached (Trill), wrong ("aww"), a
// lifeline (Hint), the million (Fanfare + flash).
#include <cstdio>
#include <cstring>
#include <new>
#include "games/common/game_kit.h"
#include "games/common/trivia_bank.h"
#include "games/common/two_player.h"
#include "games/registry.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "whowants_core.h"

namespace {

using namespace whowants;
using namespace ui;

constexpr const char* kId = "whowants";
constexpr uint32_t kLockMs = 1500, kRightMs = 1500;
const char* const kFriends[3] = {"Max", "Zoe", "Ada"};

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
    trivia::Question q;           // the question showing
    trivia::Question probe;       // for measuring others
};
State* S = nullptr;
constexpr size_t kSaveBytes = Game::kSaveBytes + 5;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   info = nullptr;
lv_obj_t*   walk = nullptr;
lv_obj_t*   qlabel = nullptr;
lv_obj_t*   ans[4] = {};
lv_obj_t*   ans_text[4] = {};
lv_obj_t*   ans_pct[4] = {};
lv_obj_t*   life[3] = {};
lv_obj_t*   again = nullptr;
int         pad = 4, top_h = 30, life_y = 0, ans_w = 0, q_w = 0;
uint32_t    now_ms = 0, step_at = 0, last_save_ms = 0;
bool        frozen = false;                // the preview holds the screen still

void open_menu();
void update();

const lv_font_t* q_font() { return metrics().large ? &lv_font_montserrat_20 : &lv_font_montserrat_14; }
const lv_font_t* a_font() { return menu_font(); }

void money(char* buf, size_t cap, int32_t v)
{
    if (v >= 1000000) snprintf(buf, cap, "$%ld,%03ld,%03ld", long(v / 1000000), long(v / 1000 % 1000), long(v % 1000));
    else if (v >= 1000) snprintf(buf, cap, "$%ld,%03ld", long(v / 1000), long(v % 1000));
    else snprintf(buf, cap, "$%ld", long(v));
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
    r.won = S->g.won;
    r.reached = uint8_t(S->g.step + (S->g.won >= 1000000 ? 1 : 0));
    r.seconds = S->seconds;
    char body[64];
    if (shell().stats_append && format_body(body, sizeof body, r)) shell().stats_append(kId, kCsvHeader, body);
    S->recorded = 1;
}

// ---- Fitting a question on the screen ---------------------------------------------------------
int text_h(const char* s, const lv_font_t* f, int w)
{
    lv_point_t p;
    lv_text_get_size(&p, s, f, 0, 0, w, LV_TEXT_FLAG_NONE);
    return p.y;
}

int key_h_for(const char* s)
{
    const int min_h = menu_btn_h();
    const int h = text_h(s, a_font(), ans_w - 24 - (metrics().large ? 52 : 40)) + 10;
    return h > min_h ? h : min_h;
}

// Whether question q fits between the top row and the lifelines
bool fits(int q)
{
    if (!S || ans_w <= 0 || !trivia::get(q, S->probe)) return false;
    int h = text_h(S->probe.text, q_font(), q_w) + pad;
    for (int a = 0; a < S->probe.answers; ++a) {
        char t[140];
        snprintf(t, sizeof t, "A: %s", S->probe.answer[a]);
        h += key_h_for(t) + pad;
    }
    return top_h + pad + h <= life_y - pad;
}

// ---- Layout -----------------------------------------------------------------------------------
void layout()
{
    if (!S || !qlabel) return;
    const Game& g = S->g;
    if (!trivia::get(g.q, S->q)) { lv_label_set_text(qlabel, "(question missing)"); return; }
    lv_label_set_text(qlabel, S->q.text);
    int y = top_h + pad;
    lv_obj_set_pos(qlabel, pad, y);
    y += text_h(S->q.text, q_font(), q_w) + pad;
    const bool poll = g.shown & kAudience;
    for (int s = 0; s < 4; ++s) {
        char t[140];
        snprintf(t, sizeof t, "%c: %s", 'A' + s, S->q.answer[g.order[s]]);
        lv_label_set_text(ans_text[s], (g.hidden >> s & 1) ? "" : t);
        const int h = key_h_for(t);
        lv_obj_set_size(ans[s], ans_w, h);
        lv_obj_set_pos(ans[s], pad, y);
        lv_obj_set_width(ans_text[s], ans_w - 24 - (poll ? (metrics().large ? 52 : 40) : 0));
        y += h + pad;
        char pc[8];
        snprintf(pc, sizeof pc, "%u%%", unsigned(g.poll[s]));
        lv_label_set_text(ans_pct[s], pc);
        lv_obj_set_hidden(ans_pct[s], !poll || (g.hidden >> s & 1));
    }
}

void paint_answers()
{
    const Game& g = S->g;
    const Palette& P = pal();
    const int right = g.right_slot();
    for (int s = 0; s < 4; ++s) {
        lv_obj_t* k = ans[s];
        lv_obj_remove_local_style_prop(k, LV_STYLE_BG_COLOR, 0);
        lv_obj_remove_local_style_prop(ans_text[s], LV_STYLE_TEXT_COLOR, 0);
        lv_obj_remove_local_style_prop(ans_pct[s], LV_STYLE_TEXT_COLOR, 0);
        bool painted = false;
        lv_color_t bg = P.key, ink = P.ink;
        if (g.phase == Phase::Locked && s == g.chosen) { bg = P.lit; ink = P.stone_dark; painted = true; }
        else if ((g.phase == Phase::Right || g.phase == Phase::Over) && !g.walked) {
            if (s == right) { bg = P.win; ink = P.stone_light; painted = true; }
            else if (s == g.chosen) { bg = P.piece_a; ink = P.stone_light; painted = true; }
        } else if (g.phase == Phase::Over && g.walked && s == right) { bg = P.win; ink = P.stone_light; painted = true; }
        if (painted) {
            lv_obj_set_style_bg_color(k, bg, 0);
            lv_obj_set_style_text_color(ans_text[s], ink, 0);
            lv_obj_set_style_text_color(ans_pct[s], ink, 0);
        }
        set_dim(k, (g.hidden >> s & 1) != 0);
    }
}

void update()
{
    if (!S || !bar.center) return;
    const Game& g = S->g;
    char t[96], m[20];
    if (g.phase == Phase::Over) {
        money(m, sizeof m, g.won);
        snprintf(t, sizeof t, g.won >= 1000000 ? "A MILLION DOLLARS!" : g.walked ? "You walked away" : "Game over");
        kit::top_bar_status(bar, t);
        snprintf(t, sizeof t, "You take home %s", m);
    } else {
        money(m, sizeof m, kPrize[g.step]);
        snprintf(t, sizeof t, "Question %d of %d", g.step + 1, kSteps);
        kit::top_bar_status(bar, t, t);
        if (g.shown & kPhone) {
            static const char* const kSure[3] = {"Maybe %c?", "I think it's %c", "I'm sure it's %c!"};
            char said[32];
            snprintf(said, sizeof said, kSure[g.friend_sure < 3 ? g.friend_sure : 0], 'A' + (g.friend_pick >= 0 ? g.friend_pick : 0));
            snprintf(t, sizeof t, "%s: %s", kFriends[g.step % 3], said);
        } else if (g.phase == Phase::Right) {
            snprintf(t, sizeof t, "Right! %s", m);
        } else {
            snprintf(t, sizeof t, "For %s", m);
        }
    }
    lv_label_set_text(info, t);
    lv_obj_set_hidden(walk, g.phase != Phase::Asking || g.step == 0);
    for (int k = 0; k < 3; ++k) {
        lv_obj_set_hidden(life[k], g.phase == Phase::Over);
        set_dim(life[k], (g.used >> k & 1) || g.phase != Phase::Asking);
    }
    lv_obj_set_hidden(again, g.phase != Phase::Over);
    paint_answers();
}

// ---- Flow -------------------------------------------------------------------------------------
uint32_t seed_now() { return shell().random_seed ? shell().random_seed() : lv_tick_get() * 2654435761u + 1; }

void game_over()
{
    if (!S->recorded) record();
    if (S->g.won >= 1000000) { sound(Sound::Fanfare); kit::flash(); }
    else if (!S->g.walked) sound(Sound::Error);
    save();
}

void new_game()
{
    kit::flash_stop();
    if (S->g.phase != Phase::Over && S->g.step > 0 && !S->recorded) { S->g.walk_away(); record(); }   // left part-way: what you had
    S->g.start(seed_now(), fits);                // played questions carry over
    S->recorded = 0;
    S->seconds = 0;
    layout();
    save();
    update();
}

void answer_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    const int s = int(intptr_t(lv_event_get_user_data(e)));
    if (!S->g.lock(s)) return;
    step_at = now_ms + kLockMs;
    save();
    update();
}

void life_cb(lv_event_t* e)
{
    if (!S || overlay_open()) return;
    const int k = int(intptr_t(lv_event_get_user_data(e)));
    if (!S->g.use(Lifeline(1 << k))) return;
    sound(Sound::Hint);
    layout();
    save();
    update();
}

void walk_cb(lv_event_t*)
{
    if (!S || overlay_open() || !S->g.walk_away()) return;
    game_over();
    update();
}

void again_cb(lv_event_t*) { if (S && !overlay_open()) new_game(); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    pad = m.large ? 8 : 4;
    const int kh = menu_btn_h();
    const int top = bar.h;
    top_h = top + kh;
    // top row: what this question is worth, Walk Away on the right
    const int ww = m.large ? 120 : 92;
    info = lv_label_create(scr);
    lv_obj_set_style_text_font(info, m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(info, pal().ink, 0);
    lv_label_set_long_mode(info, LV_LABEL_LONG_DOT);
    lv_obj_set_width(info, m.w - ww - 3 * pad);
    lv_obj_set_pos(info, pad, top + pad + (kh - lv_font_get_line_height(m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12)) / 2);
    walk = make_key(scr, ww, kh - 2 * pad, walk_cb, 0);
    key_label(walk, "Walk Away", m.large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    lv_obj_set_pos(walk, m.w - ww - pad, top + pad);
    // the question
    q_w = m.w - 2 * pad;
    qlabel = lv_label_create(scr);
    lv_obj_set_style_text_font(qlabel, q_font(), 0);
    lv_obj_set_style_text_color(qlabel, pal().ink, 0);
    lv_label_set_long_mode(qlabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(qlabel, q_w);
    // the answers
    ans_w = m.w - 2 * pad;
    for (int s = 0; s < 4; ++s) {
        ans[s] = make_key(scr, ans_w, kh, answer_cb, s);
        ans_text[s] = lv_label_create(ans[s]);
        lv_obj_set_style_text_font(ans_text[s], a_font(), 0);
        lv_label_set_long_mode(ans_text[s], LV_LABEL_LONG_WRAP);
        lv_obj_set_width(ans_text[s], ans_w - 24);
        lv_obj_align(ans_text[s], LV_ALIGN_LEFT_MID, 10, 0);
        ans_pct[s] = lv_label_create(ans[s]);
        lv_obj_set_style_text_font(ans_pct[s], a_font(), 0);
        lv_obj_align(ans_pct[s], LV_ALIGN_RIGHT_MID, -8, 0);
    }
    // lifelines along the bottom
    life_y = m.h - pad - kh;
    const int lw = (m.w - 4 * pad) / 3;
    const char* const names[3] = {"50:50", "Audience", "Phone"};
    for (int k = 0; k < 3; ++k) {
        life[k] = make_key(scr, lw, kh, life_cb, k);
        key_label(life[k], names[k], menu_font());
        lv_obj_set_pos(life[k], pad + k * (lw + pad), life_y);
    }
    again = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    key_label(again, "Play Again", menu_font());
    set_checked(again, true);
    lv_obj_set_pos(again, pad, life_y);
    clock_ = kit::Clock{};
    layout();
    update();
}

// ---- Stats and the ladder ---------------------------------------------------------------------
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
        char a[12], b[20], c[20], d[12];
        snprintf(a, sizeof a, "%lu", (unsigned long)sum.games);
        money(b, sizeof b, sum.best);
        money(c, sizeof c, int32_t(sum.total / int64_t(sum.games)));
        snprintf(d, sizeof d, "%lu", (unsigned long)sum.millions);
        table_add(t, a, b, c, d);
        const char* const head[4] = {"Games", "Best", "Average", "$1M"};
        static const int8_t pct[4] = {20, 32, 32, 16};
        table_show(t, head, pct, hf, bf);
        Table& rt = scratch_table(1);
        table_clear(rt);
        for (int i = 0; i < sum.recent_n && i < (large ? 6 : 4); ++i) {
            const Record& r = sum.newest(i);
            char s1[20], s2[8], tm[16];
            money(s1, sizeof s1, r.won);
            snprintf(s2, sizeof s2, "%u", unsigned(r.reached));
            twoplayer::format_time(tm, sizeof tm, r.seconds);
            table_add(rt, s1, s2, tm, "");
        }
        const char* const head2[4] = {"Won", "Right", "Time", ""};
        static const int8_t pct2[4] = {44, 22, 34, 0};
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

void ladder_back_cb(lv_event_t*) { open_menu(); }

// The money ladder: top prize first, the question you're on marked, safe amounts starred
void open_ladder()
{
    overlay_begin("Money Ladder");
    Table& t = scratch_table(0);
    table_clear(t);
    for (int s = kSteps - 1; s >= 0; --s) {
        char a[12], b[20], c[16];
        snprintf(a, sizeof a, "%d", s + 1);
        money(b, sizeof b, kPrize[s]);
        snprintf(c, sizeof c, "%s", S && S->g.phase != Phase::Over && S->g.step == s ? "< Now" : (s == 4 || s == 9) ? "Safe" : "");
        table_add(t, a, b, c, "");
    }
    const char* const head[4] = {"Question", "Prize", "", ""};
    static const int8_t pct[4] = {34, 40, 26, 0};
    table_show(t, head, pct, &lv_font_montserrat_12, metrics().large ? &lv_font_montserrat_14 : &lv_font_montserrat_12);
    overlay_back(ladder_back_cb, 0);
}

void menu_pick(int id)
{
    if (id == kit::kLevel0) new_game();
    else if (id == kit::kOptions) open_ladder();
}
void menu_back() { update(); }

void open_menu()
{
    kit::MenuHandlers h{menu_pick, open_stats, menu_back, open_menu};
    kit::menu_solo("Who Wants To Be A CYD?", nullptr, h, false, "Money Ladder");
}

// ---- Registry entry ---------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    const bool loaded = load(*S);
    step_at = 0;
    build();                                   // the layout sizes are needed to choose a question
    if (!loaded) { S->g.start(seed_now(), fits); layout(); update(); }
    else if (S->g.phase == Phase::Locked) step_at = 1;
}

void close()
{
    if (!S) return;
    kit::flash_stop();
    save();
    trivia::release();
    bar = kit::TopBar{};
    info = walk = qlabel = again = nullptr;
    for (int s = 0; s < 4; ++s) ans[s] = ans_text[s] = ans_pct[s] = nullptr;
    for (auto& l : life) l = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S || frozen) return;
    now_ms = now;
    Game& g = S->g;
    if (step_at == 1) step_at = now + 300;
    if (step_at && !overlay_open() && int32_t(now - step_at) >= 0) {
        step_at = 0;
        if (g.phase == Phase::Locked) {
            g.reveal();
            if (g.phase == Phase::Right) {
                sound(g.step == 4 || g.step == 9 ? Sound::Trill : Sound::Place);
                step_at = now + kRightMs;
            } else game_over();
            save();
            update();
        } else if (g.phase == Phase::Right) {
            g.next_question(fits);
            layout();
            save();
            update();
        }
    }
    if (g.phase == Phase::Right && !step_at) step_at = now + kRightMs;
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
    if (st->g.phase == Phase::Over) { money(m, sizeof m, st->g.won); snprintf(buf, cap, "Game over: you won %s", m); }
    else { money(m, sizeof m, kPrize[st->g.step]); snprintf(buf, cap, "Question %d for %s", st->g.step + 1, m); }
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a gold coin with a dollar sign over the four answer bars
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const Palette& P = pal();
    const int s = lv_area_get_width(&a);
    for (int k = 0; k < 4; ++k) {
        const int x = a.x1 + (k & 1) * s / 2 + s / 30, y = a.y1 + s * 56 / 100 + (k >> 1) * s * 22 / 100;
        kit::fill_rect(layer, x, y, x + s / 2 - s / 15, y + s * 16 / 100, k == 2 ? P.lit : P.frame, s / 12);
    }
    const int cx = a.x1 + s / 2, cy = a.y1 + s * 27 / 100, r = s * 24 / 100;
    kit::fill_circle(layer, cx, cy, r, P.stone_dark);
    kit::fill_circle(layer, cx, cy, r - 2, P.lit);
    kit::text(layer, "$", &lv_font_montserrat_20, P.stone_dark, cx - r, cy - r, 2 * r, 2 * r);
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

namespace whowants_preview {
whowants::Game* game() { return S ? &S->g : nullptr; }
void refresh() { if (S) { layout(); update(); } }
void hold(bool on) { frozen = on; step_at = 0; }
void advance(int n)
{
    if (!S) return;
    for (int k = 0; k < n; ++k) { S->g.lock(S->g.right_slot()); S->g.reveal(); S->g.next_question(fits); }
    layout();
    update();
}
void ladder() { open_ladder(); }
}

namespace games {
extern const GameOps whowants_ops;
const GameOps whowants_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
