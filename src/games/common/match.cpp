#include "match.h"

#include <cstdio>
#include "ai_task.h"
#include "game_kit.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace match {

using namespace ui;
using twoplayer::Mode;

namespace {

Game         G{};
State        S{};
bool         attached = false;
kit::TopBar  bar;
kit::Clock   clock_;
lv_obj_t*    info_l  = nullptr;
lv_obj_t*    again_k = nullptr;

// The computer's move travels from the AI task to the UI through these
struct Think {
    int           level = 0;
    uint32_t      seed = 0;
    volatile int  move = -1;
    volatile bool done = false;
} think;
bool     thinking = false;
uint32_t think_after_ms = 0;          // short pause so the reply isn't instant
uint32_t now_ms = 0;
constexpr uint32_t kThinkPauseMs = 350;

bool over()             { return G.result && G.result() != -1; }
bool computer_to_move() { return S.mode == Mode::Computer && !over() && G.turn() != S.human_side; }

void ai_job(void* ctx, volatile bool* stop)
{
    Think* t = static_cast<Think*>(ctx);
    t->move = G.think(t->level, t->seed, stop);
    t->done = true;
}

const char* side_name(int s) { return s == 0 ? G.sides.side1 : G.sides.side2; }

void update_status()
{
    if (!bar.center) return;
    char t[16];
    twoplayer::format_time(t, sizeof t, S.seconds);
    lv_label_set_text(bar.left, t);
    char st[40];
    const int r = G.result();
    if (S.mode == Mode::Computer) {
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               snprintf(st, sizeof st, "Computer wins");
        else if (clock_.paused)        snprintf(st, sizeof st, "Paused");
        else if (computer_to_move())   snprintf(st, sizeof st, "Thinking...");
        else                           snprintf(st, sizeof st, "Your turn (%s)", side_name(S.human_side));
    } else {
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r >= 0)               snprintf(st, sizeof st, "%s wins!", side_name(r));
        else if (clock_.paused)        snprintf(st, sizeof st, "Paused");
        else                           snprintf(st, sizeof st, "%s's turn", side_name(G.turn()));
    }
    kit::top_bar_status(bar, st);

    if (info_l) {
        char in[80], extra[48] = "", note[48] = "";
        if (G.note) G.note(note, sizeof note);
        if (!note[0] && G.score) G.score(extra, sizeof extra);
        if (note[0]) {
            snprintf(in, sizeof in, "%s", note);          // a note is news: on its own
        } else if (extra[0]) {
            snprintf(in, sizeof in, "%s. %s", extra,
                     S.mode == Mode::Computer ? twoplayer::level_name(S.level) : "Pass and play");
            // The level goes when the line would be cut short
            if (text_width(in, lv_obj_get_style_text_font(info_l, LV_PART_MAIN)) > lv_obj_get_style_width(info_l, LV_PART_MAIN))
                snprintf(in, sizeof in, "%s", extra);
        }
        else if (S.mode == Mode::Computer)
            snprintf(in, sizeof in, "Computer: %s. You: %s",
                     twoplayer::level_name(S.level), side_name(S.human_side));
        else
            snprintf(in, sizeof in, "Pass and play. %s moves first", G.sides.side1);
        lv_label_set_text(info_l, in);
    }
    if (again_k) {
        if (over()) lv_obj_set_hidden(again_k, false);
        else        lv_obj_set_hidden(again_k, true);
    }
}

void record(twoplayer::Result res)
{
    twoplayer::Record r;
    r.mode = S.mode;
    r.level = S.level;
    r.result = res;
    r.moves = static_cast<uint16_t>(G.moves());
    r.seconds = S.seconds;
    kit::record_two_player(G.id, r, G.sides);
}

// A move was made (by anyone): sounds, end of game, the computer's turn
void after_move(bool by_computer)
{
    if (G.redraw) G.redraw();
    const int r = G.result();
    if (r == -1) {
        sound(by_computer ? Sound::Turn : Sound::Place);
        if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    } else if (!S.recorded) {
        S.recorded = 1;
        twoplayer::Result res;
        if (r == 2) res = twoplayer::Result::Draw;
        else if (S.mode == Mode::Computer)
            res = r == S.human_side ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
        else res = r == 0 ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
        record(res);
        if (r == 2) sound(Sound::Draw);
        else if (S.mode == Mode::Computer && r != S.human_side) sound(Sound::Lose);
        else { sound(Sound::Win); kit::flash(); }
    }
    update_status();
}

void start_new(Mode mode, twoplayer::Level level)
{
    ai_stop();
    thinking = false;
    // Leaving a started vs-computer game counts as a loss
    if (!over() && !S.recorded && S.mode == Mode::Computer && G.moves() > 0)
        record(twoplayer::Result::Side2);
    kit::flash_stop();
    const bool was_computer = S.mode == Mode::Computer;
    S.mode = mode;
    S.level = level;
    // Alternate who moves first vs the computer
    S.human_side = (mode == Mode::Computer && was_computer) ? uint8_t(S.human_side ^ 1) : 0;
    S.recorded = 0;
    S.seconds = 0;
    G.reset();
    if (G.redraw) G.redraw();
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    update_status();
}

void again_cb(lv_event_t*) { start_new(S.mode, S.level); }

void menu_pick(int id)
{
    if (id <= kit::kLevel2) start_new(Mode::Computer, static_cast<twoplayer::Level>(id));
    else if (id == kit::kPassAndPlay) start_new(Mode::PassAndPlay, S.level);
}
void menu_stats() { kit::stats_two_player(G.id, G.sides, open_menu); }
void menu_back()  { update_status(); }

} // namespace

State& state() { return S; }

void attach(const Game& g)
{
    G = g;
    attached = true;
}

void build_chrome(int* top, int* bottom)
{
    const Metrics& m = metrics();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4;
    const int kh = menu_btn_h();
    // Bottom: "Play again" key (only when the game is over) above the info line
    info_l = lv_label_create(scr);
    lv_obj_set_style_text_font(info_l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(info_l, pal().muted, 0);
    lv_obj_set_width(info_l, m.w - 2 * pad);
    lv_obj_set_style_text_align(info_l, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(info_l, LV_LABEL_LONG_MODE_DOTS);
    const int ih = lv_font_get_line_height(&lv_font_montserrat_14);
    lv_obj_set_height(info_l, ih);
    lv_obj_set_pos(info_l, pad, m.h - pad - ih);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, m.h - pad - ih - pad - kh);
    *top = bar.h;
    *bottom = m.h - pad - ih - pad - kh - pad;
}

void restart_view()
{
    clock_ = kit::Clock{};
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    update_status();
}

void refresh() { update_status(); }

void detach()
{
    ai_stop();
    thinking = false;
    kit::flash_stop();
    bar = kit::TopBar{};
    info_l = again_k = nullptr;
    attached = false;
}

bool human_may_move()
{
    if (!attached || over() || overlay_open()) return false;
    return S.mode == Mode::PassAndPlay || G.turn() == S.human_side;
}

void human_move(int move)
{
    if (!human_may_move()) return;
    G.play(move);
    after_move(false);
}

void tick(uint32_t now)
{
    now_ms = now;
    if (!attached) return;
    if (clock_.tick(now, !over(), S.seconds)) update_status();
    // Computer: start thinking after the pause, pick up the move when done
    if (!thinking && computer_to_move() && !overlay_open() && int32_t(now - think_after_ms) >= 0) {
        think.level = static_cast<int>(S.level);
        think.seed = shell().random_seed ? shell().random_seed() : now;
        think.done = false;
        think.move = -1;
        thinking = ai_start(ai_job, &think, G.ai_stack);
        update_status();
    }
    if (thinking && think.done) {
        thinking = false;
        if (think.move >= 0 && computer_to_move()) {
            G.play(think.move);
            after_move(true);
        }
    }
}

void open_menu()
{
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_two_player(G.title, h);
}

void describe(const State& st, int r, int moves, const twoplayer::Sides& sides, char* buf, size_t cap)
{
    const char* how = st.mode == Mode::Computer ? twoplayer::level_name(st.level) : "Pass and play";
    const char* won = r == 0 ? sides.side1 : sides.side2;
    if (r == -1) snprintf(buf, cap, "%s, move %d", how, moves + 1);
    else if (r == 2) snprintf(buf, cap, "%s, a draw", how);
    else if (st.mode == Mode::Computer) snprintf(buf, cap, "%s, you %s", how, r == st.human_side ? "won" : "lost");
    else snprintf(buf, cap, "%s, %s won", how, won);
}

void summary(char* buf, size_t cap) { describe(S, G.result(), G.moves(), G.sides, buf, cap); }

size_t save_state(uint8_t* buf, size_t cap)
{
    if (cap < kStateBytes) return 0;
    buf[0] = static_cast<uint8_t>(S.mode);
    buf[1] = static_cast<uint8_t>(S.level);
    buf[2] = S.human_side;
    buf[3] = S.recorded;
    for (int k = 0; k < 4; ++k) buf[4 + k] = uint8_t(S.seconds >> (8 * k));
    return kStateBytes;
}

bool read_state(const uint8_t* buf, size_t len, State& out)
{
    if (len < kStateBytes || buf[0] > 1 || buf[1] > 2 || buf[2] > 1) return false;
    out.mode = static_cast<Mode>(buf[0]);
    out.level = static_cast<twoplayer::Level>(buf[1]);
    out.human_side = buf[2];
    out.recorded = buf[3] ? 1 : 0;
    out.seconds = 0;
    for (int k = 0; k < 4; ++k) out.seconds |= uint32_t(buf[4 + k]) << (8 * k);
    return true;
}

bool load_state(const uint8_t* buf, size_t len) { return read_state(buf, len, S); }

} // namespace match
