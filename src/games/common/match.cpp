#include "match.h"

#include <cstdio>
#include <new>
#include "ai_task.h"
#include "game_kit.h"
#include "nearby.h"
#include "net/wireless.h"
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
bool     start_failed = false;        // the AI task couldn't start (logged once a game)
bool     was_busy = false;            // the game's animation was running last tick
uint32_t now_ms = 0;
constexpr uint32_t kThinkPauseMs = 350;

// Wireless: the link to the other board (on the heap while the game is open)
net::Link* L = nullptr;
bool radio_held = false;
bool link_was_up = false;
bool wl_broken = false;               // a wireless game whose link couldn't be loaded

bool over()             { return G.result && G.result() != -1; }
bool computer_to_move() { return S.mode == Mode::Computer && !over() && G.turn() != S.human_side; }
bool wl()               { return S.mode == Mode::Wireless; }
bool wl_live()          { return wl() && L && !L->ended(); }
const char* peer()      { return L ? L->peer_name() : "Partner"; }

void wl_file(char* buf, size_t cap) { snprintf(buf, cap, "wl_%s", G.id); }

void wl_save()
{
    if (!L || !shell().save_game) return;
    uint8_t buf[net::Link::kSaveBytes];
    char fn[24];
    wl_file(fn, sizeof fn);
    if (L->save(buf, sizeof buf)) shell().save_game(fn, buf, sizeof buf);
}

void hold_radio(bool on)
{
    if (on && !radio_held) radio_held = nearby::radio_start();
    else if (!on && radio_held) { nearby::radio_stop(); radio_held = false; }
}

// Ends the wireless session (another game starts): the other board is told
void wl_end()
{
    if (L) {
        if (!L->ended()) {
            L->leave(now_ms);
            log_event("Wireless: %s ended", G.id);
        }
        delete L;
        L = nullptr;
    }
    hold_radio(false);
    wl_broken = false;
}

// Opening (or restyling) a game saved as wireless: get the link back
void wl_resume()
{
    if (!wl()) return;
    if (!L && !wl_broken) {
        uint8_t buf[net::Link::kSaveBytes];
        char fn[24];
        wl_file(fn, sizeof fn);
        const Shell& H = shell();
        L = new (std::nothrow) net::Link();
        const size_t n = H.load_game ? H.load_game(fn, buf, sizeof buf) : 0;
        if (!L || n != sizeof buf || !L->load(buf, n, nearby::air(), now_ms)) {
            delete L;
            L = nullptr;
            wl_broken = true;
        }
    }
    if (L) {
        S.human_side = uint8_t(L->my_side());
        if (!L->ended()) hold_radio(true);
    }
}

void ai_job(void* ctx, volatile bool* stop)
{
    Think* t = static_cast<Think*>(ctx);
    t->move = G.think(t->level, t->seed, stop);
    t->done = true;
}

const char* side_name(int s) { return s == 0 ? G.sides.side1 : G.sides.side2; }

// The info line's wireless news, if any
void wireless_note(char* buf, size_t cap)
{
    buf[0] = 0;
    if (!L) {
        if (wl_broken) snprintf(buf, cap, "This wireless game can't go on");
        return;
    }
    switch (L->end_reason()) {
        case net::Link::End::PeerLeft:  snprintf(buf, cap, "%s ended the game", peer()); return;
        case net::Link::End::OutOfStep: snprintf(buf, cap, "The boards' games differ: it ended"); return;
        case net::Link::End::YouLeft:   snprintf(buf, cap, "You ended the game"); return;
        case net::Link::End::None:      break;
    }
    if (!radio_held) { snprintf(buf, cap, "The radio couldn't start (low on memory)"); return; }
    if (!L->up(now_ms)) {
        if (L->peer_away()) snprintf(buf, cap, "%s left %s for now", peer(), G.title);
        else                snprintf(buf, cap, "%s needs %s open too", peer(), G.title);
        return;
    }
    if (over()) {
        if (L->again() && !L->peer_again()) snprintf(buf, cap, "Waiting for %s to play again", peer());
        else if (L->peer_again())          snprintf(buf, cap, "%s wants to play again", peer());
    }
}

void update_status()
{
    if (!bar.center) return;
    char t[16];
    twoplayer::format_time(t, sizeof t, S.seconds);
    lv_label_set_text(bar.left, t);
    char st[40];
    const int r = G.result();
    if (wl()) {
        const uint32_t now = now_ms;
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               snprintf(st, sizeof st, "%s wins", peer());
        else if (!L)                   snprintf(st, sizeof st, "Game over");
        else if (L->ended())           snprintf(st, sizeof st, "Game ended");
        else if (!radio_held)          snprintf(st, sizeof st, "Radio off");
        else if (!L->up(now))          snprintf(st, sizeof st, "Waiting for %s...", peer());
        else if (G.turn() == S.human_side) snprintf(st, sizeof st, "Your turn (%s)", side_name(S.human_side));
        else                           snprintf(st, sizeof st, "%s's turn", peer());
    } else if (S.mode == Mode::Computer) {
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               snprintf(st, sizeof st, "Computer wins");
        else if (clock_.paused)        snprintf(st, sizeof st, "Paused");
        else if (computer_to_move())   snprintf(st, sizeof st, start_failed ? "Low on memory..." : "Thinking...");
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
        char wl_note[64] = "";
        if (wl()) wireless_note(wl_note, sizeof wl_note);
        if (wl_note[0]) {
            snprintf(in, sizeof in, "%s", wl_note);       // the link's news first
        } else if (note[0]) {
            snprintf(in, sizeof in, "%s", note);          // a note is news: on its own
        } else if (extra[0]) {
            snprintf(in, sizeof in, "%s. %s", extra,
                     wl() ? peer() : S.mode == Mode::Computer ? twoplayer::level_name(S.level) : "Pass and play");
            // The level goes when the line would be cut short
            if (text_width(in, lv_obj_get_style_text_font(info_l, LV_PART_MAIN)) > lv_obj_get_style_width(info_l, LV_PART_MAIN))
                snprintf(in, sizeof in, "%s", extra);
        }
        else if (wl())
            snprintf(in, sizeof in, "Wireless with %s. You: %s", peer(), side_name(S.human_side));
        else if (S.mode == Mode::Computer)
            snprintf(in, sizeof in, "Computer: %s. You: %s",
                     twoplayer::level_name(S.level), side_name(S.human_side));
        else
            snprintf(in, sizeof in, "Pass and play. %s moves first", G.sides.side1);
        lv_label_set_text(info_l, in);
    }
    if (again_k) {
        // Wireless: Play Again only while the partner can still answer; lit
        // until tapped
        const bool show = over() && (!wl() || wl_live());
        lv_obj_set_hidden(again_k, !show);
        set_checked(again_k, !(wl() && L && L->again()));
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

// A move was made (by anyone): sounds, end of game, the computer's turn.
// by_other = the computer's or the other board's move.
void after_move(bool by_other)
{
    if (G.redraw) G.redraw();
    const int r = G.result();
    const bool vs = S.mode == Mode::Computer || wl();     // one player at this board
    if (r == -1) {
        sound(by_other ? Sound::Turn : Sound::Place);
        if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    } else if (!S.recorded) {
        S.recorded = 1;
        twoplayer::Result res;
        if (r == 2) res = twoplayer::Result::Draw;
        else if (vs)
            res = r == S.human_side ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
        else res = r == 0 ? twoplayer::Result::Side1 : twoplayer::Result::Side2;
        record(res);
        if (r == 2) sound(Sound::Draw);
        else if (vs && r != S.human_side) sound(Sound::Lose);
        else { sound(Sound::Win); kit::flash(); }
    }
    update_status();
}

void start_new(Mode mode, twoplayer::Level level)
{
    ai_stop();
    thinking = false;
    if (wl()) wl_end();                   // the other board is told: no record here or there
    // Leaving a vs-computer game the player has moved in counts as a loss
    if (!over() && !S.recorded && S.mode == Mode::Computer && S.human_moved)
        record(twoplayer::Result::Side2);
    kit::flash_stop();
    const bool was_computer = S.mode == Mode::Computer;
    S.mode = mode;
    S.level = level;
    // Alternate who moves first vs the computer
    S.human_side = (mode == Mode::Computer && was_computer) ? uint8_t(S.human_side ^ 1) : 0;
    S.recorded = 0;
    S.human_moved = 0;
    start_failed = false;
    S.seconds = 0;
    G.reset();
    if (G.redraw) G.redraw();
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    update_status();
}

// The next game of a wireless session (both players asked for it)
void wl_next_game()
{
    kit::flash_stop();
    S.human_side = uint8_t(L->my_side());
    S.recorded = 0;
    S.human_moved = 0;
    S.seconds = 0;
    G.reset();
    if (G.redraw) G.redraw();
    wl_save();
    update_status();
}

void again_cb(lv_event_t*)
{
    if (wl()) {
        if (wl_live() && !L->again()) L->want_again(now_ms);
        update_status();
        return;
    }
    start_new(S.mode, S.level);
}

// Play Nearby agreed on a game: it starts here at once
void wl_started(const net::Mac& peer_mac, const char* name, uint32_t session, bool inviter)
{
    start_new(Mode::Wireless, S.level);   // ends whatever was on (a wireless game too)
    L = new (std::nothrow) net::Link();
    if (!L) { wl_broken = true; update_status(); return; }
    L->begin(nearby::my_mac(), peer_mac, name, session, inviter, nearby::air(), now_ms);
    hold_radio(true);
    S.human_side = uint8_t(L->my_side());
    link_was_up = false;
    wl_save();
    update_status();
}

void menu_pick(int id)
{
    if (id <= kit::kLevel2) start_new(Mode::Computer, static_cast<twoplayer::Level>(id));
    else if (id == kit::kPassAndPlay) start_new(Mode::PassAndPlay, S.level);
    else if (id == kit::kWireless) nearby::lobby_open(G.id, G.title, wl_started, open_menu);
}

// Wireless, every tick: packets in, status out, the other board's moves
void wl_tick(uint32_t now)
{
    if (!L) return;
    if (radio_held) {
        net::Mac from;
        uint8_t buf[net::kPacketMax];
        size_t n;
        for (int k = 0; k < 16 && nearby::receive(&from, buf, &n); ++k) L->receive(from, buf, n, now);
        L->tick(now);
    }
    if (L->started_next()) wl_next_game();
    // The other board's moves, one at a time (a game may animate each),
    // each checked against this board's rules first
    // (held while a menu is open, like the computer's: it would land unseen)
    const bool busy = (G.busy && G.busy()) || overlay_open();
    const int m = busy ? -1 : L->next_move();
    if (m >= 0) {
        if (over() || G.turn() == S.human_side || (G.legal && !G.legal(m))) {
            log_event("Wireless: %s got a move it can't play (%d)", G.id, m);
            L->disagree(now);
        } else {
            G.play(m);
            L->played(m, now);
            after_move(true);
        }
    }
    if (L->ended() && radio_held) {
        hold_radio(false);
        wl_save();
        update_status();
    }
    const bool up = L->up(now);
    if (up != link_was_up) { link_was_up = up; update_status(); }
}
void menu_stats() { kit::stats_two_player(G.id, G.sides, open_menu); }
void menu_back()  { if (G.redraw) G.redraw(); update_status(); }

} // namespace

State& state() { return S; }

int my_side() { return S.mode == Mode::PassAndPlay ? -1 : S.human_side; }

const char* opponent_name()
{
    if (S.mode == Mode::Computer) return "Computer";
    if (wl()) return peer();
    return nullptr;
}

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
    start_failed = false;
    was_busy = false;
    wl_resume();
    link_was_up = false;
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    update_status();
}

void refresh() { update_status(); }

void detach()
{
    ai_stop();
    thinking = false;
    kit::flash_stop();
    nearby::lobby_close();
    bar = kit::TopBar{};
    info_l = again_k = nullptr;
    attached = false;
}

void closed()
{
    nearby::lobby_close();
    if (L) {
        if (!L->ended()) L->away(now_ms);          // "Bob left Chess for now"
        wl_save();
        delete L;
        L = nullptr;
    }
    hold_radio(false);
    wl_broken = false;
}

bool human_may_move()
{
    if (!attached || over() || overlay_open()) return false;
    if (wl()) return wl_live() && radio_held && L->up(now_ms) && L->next_move() < 0
                     && G.turn() == S.human_side && !(G.busy && G.busy());
    return S.mode == Mode::PassAndPlay || G.turn() == S.human_side;
}

void human_move(int move)
{
    if (!human_may_move()) return;
    S.human_moved = 1;
    G.play(move);
    if (wl() && L) L->played(move, now_ms);
    after_move(false);
}

void tick(uint32_t now)
{
    now_ms = now;
    if (!attached) return;
    if (nearby::lobby_active()) nearby::lobby_tick(now);
    else if (wl()) wl_tick(now);
    if (clock_.tick(now, !over(), S.seconds)) update_status();
    // Computer: start thinking after the pause, pick up the move when done
    // A game still animating its last move holds the computer back; the
    // pause starts when the animation ends
    const bool busy = G.busy && G.busy();
    if (was_busy && !busy && computer_to_move()) think_after_ms = now + kThinkPauseMs;
    was_busy = busy;
    if (busy) return;
    if (!thinking && computer_to_move() && !overlay_open() && int32_t(now - think_after_ms) >= 0) {
        think.level = static_cast<int>(S.level);
        think.seed = shell().random_seed ? shell().random_seed() : now;
        think.done = false;
        think.move = -1;
        thinking = ai_start(ai_job, &think, G.ai_stack);
        if (!thinking) {
            // No task (out of memory): try again in a while, log it once
            think_after_ms = now + 3000;
            if (!start_failed) log_event("%s: the computer could not start thinking", G.id);
            start_failed = true;
        }
        update_status();
    }
    // A finished move waits while a menu is open (it would land unseen)
    if (thinking && think.done && !overlay_open()) {
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
    kit::menu_two_player(G.title, h, G.legal && nearby::available());
}

void describe(const State& st, int r, int moves, const twoplayer::Sides& sides, char* buf, size_t cap)
{
    const char* how = st.mode == Mode::Computer ? twoplayer::level_name(st.level)
                    : st.mode == Mode::Wireless ? "Wireless" : "Pass and play";
    const char* won = r == 0 ? sides.side1 : sides.side2;
    if (r == -1) snprintf(buf, cap, "%s, move %d", how, moves + 1);
    else if (r == 2) snprintf(buf, cap, "%s, a draw", how);
    else if (st.mode != Mode::PassAndPlay) snprintf(buf, cap, "%s, you %s", how, r == st.human_side ? "won" : "lost");
    else snprintf(buf, cap, "%s, %s won", how, won);
}

void summary(char* buf, size_t cap) { describe(S, G.result(), G.moves(), G.sides, buf, cap); }

size_t save_state(uint8_t* buf, size_t cap)
{
    if (cap < kStateBytes) return 0;
    buf[0] = static_cast<uint8_t>(S.mode);
    buf[1] = static_cast<uint8_t>(S.level);
    buf[2] = S.human_side;
    buf[3] = uint8_t((S.recorded ? 1 : 0) | (S.human_moved ? 2 : 0));
    for (int k = 0; k < 4; ++k) buf[4 + k] = uint8_t(S.seconds >> (8 * k));
    if (wl()) wl_save();                  // the link's state goes with every save
    return kStateBytes;
}

bool read_state(const uint8_t* buf, size_t len, State& out)
{
    if (len < kStateBytes || buf[0] > 2 || buf[1] > 2 || buf[2] > 1) return false;
    out.mode = static_cast<Mode>(buf[0]);
    out.level = static_cast<twoplayer::Level>(buf[1]);
    out.human_side = buf[2];
    out.recorded = buf[3] & 1;
    out.human_moved = (buf[3] >> 1) & 1;
    out.seconds = 0;
    for (int k = 0; k < 4; ++k) out.seconds |= uint32_t(buf[4 + k]) << (8 * k);
    return true;
}

bool load_state(const uint8_t* buf, size_t len) { return read_state(buf, len, S); }

} // namespace match
