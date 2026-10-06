#include "match.h"

#include <climits>
#include <cstdio>
#include <cstring>
#include <new>
#include "ai_task.h"
#include "game_kit.h"
#include "net/wireless.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"
#include "wplay.h"

namespace match {

using namespace ui;
using twoplayer::Mode;
using End = net::Link::End;

namespace {

Game         G{};
State        S{};
bool         attached = false;
kit::TopBar  bar;
kit::Clock   clock_;
lv_obj_t*    info_l  = nullptr;
lv_obj_t*    again_k = nullptr;        // Play Again (full width; a third in a wireless session)
lv_obj_t*    new_k   = nullptr;        // wireless: New Game (with the same player)
lv_obj_t*    done_k  = nullptr;        // wireless: Goodbye / Done (the session ended)
int          key_full_w = 0, key_half_w = 0, key_pad = 0;

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
uint32_t think_pause() { return G.no_pause && G.no_pause() ? 0 : kThinkPauseMs; }

// Wireless: the session's link lives in the Wireless Play service (wplay.*)
int      link_look = -1;              // the link's state the screen shows
uint32_t handled_end = 0;             // the session whose end was dealt with here
uint32_t turn_start_ms = 0;           // the move timer counts from here
int      timer_ply = -1, timer_game = -1;
bool     was_up = false;              // the link was up last tick
bool     grace_up = false;            // "You haven't responded in time" was shown this turn
int      comm = 0;                    // 0 fine, 1 "No reply" asked, 2 keep waiting
uint32_t comm_deadline = 0;
uint32_t start_note_until = 0;        // "Move Timer: ..." after a game starts
uint32_t resumed_note_until = 0;      // "Resuming your previous one player game."
uint32_t behind_since = 0;            // the partner is ahead of this board (log a stuck game)
int      behind_logged = -1;
int      last_secs = -1;              // the countdown shown
constexpr uint32_t kGraceMs = 10000;  // after the move timer ran out
constexpr uint32_t kNoTimerCommMs = 60000;   // Move Timer Off: how long "no reply" waits

bool over()             { return G.result && G.result() != -1; }
bool computer_to_move() { return S.mode == Mode::Computer && !over() && G.turn() != S.human_side; }
bool wl()               { return S.mode == Mode::Wireless; }
net::Link* link()       { return wl() ? wplay::session_for(G.id) : nullptr; }
bool wl_live()          { net::Link* l = link(); return l && !l->ended(); }
// A game of the session is finished (or void): Play Again / New Game / Goodbye
bool game_done()        { net::Link* l = link(); return over() || (l && l->voided()); }
bool wl_going()         { return wl_live() && !game_done(); }
const char* peer()      { return link() ? wplay::partner_name() : "Partner"; }
uint32_t timer_ms()     { net::Link* l = link(); return l ? uint32_t(l->timer()) * 1000u : 0; }
uint32_t comm_ms()      { return timer_ms() ? timer_ms() : kNoTimerCommMs; }

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
    net::Link* L = link();
    if (!L) { snprintf(buf, cap, "This wireless game has ended. New Game is in the menu"); return; }
    switch (L->end_reason()) {
        case End::PeerForfeited:
            if (L->by_time()) snprintf(buf, cap, "%s ran out of time and forfeited the game.", peer());
            else              snprintf(buf, cap, "%s left and forfeited the game.", peer());
            return;
        case End::YouForfeited:
            snprintf(buf, cap, L->by_time() ? "You ran out of time and forfeited." : "You forfeited the game.");
            return;
        case End::PeerDone:      snprintf(buf, cap, "%s said goodbye. Thanks for the game!", peer()); return;
        case End::YouDone:       snprintf(buf, cap, "Thanks for playing!"); return;
        case End::PeerGone:      snprintf(buf, cap, "%s's board ended this game. Not counted.", peer()); return;
        case End::YouLeft:       snprintf(buf, cap, "This wireless game was cleared."); return;
        case End::Closed:
        case End::PeerClosed:    snprintf(buf, cap, "Communications failed. Game not counted."); return;
        case End::None:          break;
    }
    if (L->voided()) { snprintf(buf, cap, "The boards' games differ. Not counted."); return; }
    if (!L->up(now_ms)) { snprintf(buf, cap, "%s is out of range.", peer()); return; }
    if (game_done()) {
        if (L->again() && !L->peer_again()) snprintf(buf, cap, "Waiting for %s to play again", peer());
        else if (L->peer_again())          snprintf(buf, cap, "%s wants to play again!", peer());
        else                               snprintf(buf, cap, "Play again with %s?", peer());
        return;
    }
    // The Move Timer, when it isn't this player's own setting
    if (int32_t(start_note_until - now_ms) > 0 && L->timer() != wplay::move_timer()) {
        if (L->timer() == 0) snprintf(buf, cap, "Move Timer: Off");
        else if (L->timer() < 60) snprintf(buf, cap, "Move Timer: %d seconds", L->timer());
        else snprintf(buf, cap, "Move Timer: %d minute%s", L->timer() / 60, L->timer() >= 120 ? "s" : "");
    }
}

void place_keys()
{
    if (!again_k || !done_k || !new_k) return;
    // Wireless: [Play Again | New Game | Goodbye] while the session goes
    // on; just "Done" once it ended. Elsewhere: Play Again.
    net::Link* L = link();
    const bool rematch = wl() && game_done() && L && !L->ended();
    const bool ended = wl() && (!L || L->ended());
    lv_obj_set_hidden(again_k, !((over() && !wl()) || rematch));
    lv_obj_set_hidden(new_k, !rematch);
    lv_obj_set_hidden(done_k, !(rematch || ended));
    const int third = (key_full_w - 2 * key_pad) / 3;
    lv_obj_set_width(again_k, rematch ? third : key_full_w);
    lv_obj_set_width(new_k, third);
    lv_obj_set_x(new_k, key_pad + third + key_pad);
    lv_obj_set_width(done_k, rematch ? third : key_full_w);
    lv_obj_set_x(done_k, rematch ? key_pad + 2 * (third + key_pad) : key_pad);
    lv_label_set_text(lv_obj_get_child(again_k, 0), rematch ? "Again" : "Play Again");
    lv_label_set_text(lv_obj_get_child(done_k, 0), rematch ? "Goodbye" : "Done");
    set_checked(again_k, !(L && L->again()));
    set_checked(done_k, ended);
}

// Seconds left on the move timer (negative: into the 10 s grace), or INT32_MIN when none counts
int32_t timer_left_ms()
{
    net::Link* L = link();
    if (!L || !L->timer() || !wl_going() || !L->up(now_ms)) return INT32_MIN;
    return int32_t(timer_ms()) - int32_t(now_ms - turn_start_ms);
}

void update_status()
{
    if (!bar.center) return;
    char t[16];
    twoplayer::format_time(t, sizeof t, S.seconds);
    lv_label_set_text(bar.left, t);
    char st[48];
    const char* short_st = nullptr;     // when the header has no room for it
    const int r = G.result();
    if (wl()) {
        net::Link* L = link();
        const int32_t left = timer_left_ms();
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               { snprintf(st, sizeof st, "%s wins", peer()); short_st = "You lost"; }
        else if (!L)                   snprintf(st, sizeof st, "Game ended");
        else if (L->end_reason() == End::PeerForfeited) snprintf(st, sizeof st, "You win!");
        else if (L->end_reason() == End::YouForfeited)  snprintf(st, sizeof st, "Forfeited");
        else if (L->ended() || L->voided()) snprintf(st, sizeof st, "Not counted");
        else if (!L->up(now_ms))       { snprintf(st, sizeof st, "Waiting for %s...", peer()); short_st = "Out of range"; }
        else if (G.turn() == S.human_side) {
            if (left == INT32_MIN) { snprintf(st, sizeof st, "Your turn (%s)", side_name(S.human_side)); short_st = "Your turn"; }
            else {
                const int32_t ms = left > 0 ? left : left + int32_t(kGraceMs);
                snprintf(st, sizeof st, "Respond in %lds", long(ms > 0 ? (ms + 999) / 1000 : 0));
            }
        } else {
            if (left == INT32_MIN) { snprintf(st, sizeof st, "%s's turn", peer()); short_st = "Their turn"; }
            else snprintf(st, sizeof st, "Waiting... %lds", long(left > 0 ? (left + 999) / 1000 : 0));
        }
    } else if (S.mode == Mode::Computer) {
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               snprintf(st, sizeof st, "Computer wins");
        else if (clock_.paused)        snprintf(st, sizeof st, "Paused");
        else if (computer_to_move())   snprintf(st, sizeof st, start_failed ? "Low on memory..." : "Thinking...");
        else                           { snprintf(st, sizeof st, "Your turn (%s)", side_name(S.human_side)); short_st = "Your turn"; }
    } else {
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r >= 0)               snprintf(st, sizeof st, "%s wins!", side_name(r));
        else if (clock_.paused)        snprintf(st, sizeof st, "Paused");
        else                           snprintf(st, sizeof st, "%s's turn", side_name(G.turn()));
    }
    kit::top_bar_status(bar, st, short_st);

    if (info_l) {
        char in[96], extra[48] = "", note[48] = "";
        if (G.note) G.note(note, sizeof note);
        if (!note[0] && G.score) G.score(extra, sizeof extra);
        char wl_note[96] = "";
        if (wl()) wireless_note(wl_note, sizeof wl_note);
        else if (int32_t(resumed_note_until - now_ms) > 0) snprintf(wl_note, sizeof wl_note, "Resuming your previous one player game.");
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
            snprintf(in, sizeof in, "Playing %s. You: %s", peer(), side_name(S.human_side));
        else if (S.mode == Mode::Computer)
            snprintf(in, sizeof in, "Computer: %s. You: %s",
                     twoplayer::level_name(S.level), side_name(S.human_side));
        else
            snprintf(in, sizeof in, "Pass and play. %s moves first", G.sides.side1);
        lv_label_set_text(info_l, in);
    }
    place_keys();
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

void start_turn()
{
    turn_start_ms = now_ms;
    grace_up = false;
    if (net::Link* L = link()) { timer_ply = L->ply(); timer_game = L->game_no(); }
}

// A move was made (by anyone): sounds, end of game, the computer's turn.
// by_other = the computer's or the other board's move.
void after_move(bool by_other)
{
    if (G.redraw) G.redraw();
    const int r = G.result();
    const bool vs = S.mode == Mode::Computer || wl();     // one player at this board
    if (wl()) start_turn();
    if (r == -1) {
        if (G.move_sound) G.move_sound(by_other);
        else sound(by_other ? Sound::Turn : Sound::Place);
        if (computer_to_move()) think_after_ms = now_ms + think_pause();
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
        if (wl()) wplay::session_over(true);           // free to be asked again (a rematch may follow)
    }
    update_status();
}

void start_new(Mode mode, twoplayer::Level level)
{
    ai_stop();
    thinking = false;
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
    if (computer_to_move()) think_after_ms = now_ms + think_pause();
    update_status();
}

// A game of the wireless session starts (its first, or the next after Play Again)
void wl_game_starts()
{
    net::Link* L = link();
    if (!L) return;
    ai_stop();
    thinking = false;
    kit::flash_stop();
    if (overlay_open()) close_overlays();
    S.mode = Mode::Wireless;
    S.human_side = uint8_t(L->my_side());
    S.recorded = 0;
    S.human_moved = 0;
    S.seconds = 0;
    clock_ = kit::Clock{};
    comm = 0;
    G.reset();
    if (G.redraw) G.redraw();
    start_turn();
    start_note_until = now_ms + 8000;
    behind_since = 0;
    behind_logged = -1;
    log_event("Wireless: %s game %d, this board %s (%s), timer %d s", G.id, L->game_no(), side_name(S.human_side),
              S.human_side == 0 ? "first" : "second", L->timer());
    wplay::session_over(false);
    wplay::session_save();
    update_status();
}

// ---- Questions over the game -------------------------------------------------------------------
enum Ask : intptr_t { kKeepPlaying = 1, kLeave, kGraceOk, kKeepWaiting, kCloseGame };
void ask_cb(lv_event_t* e);

void end_notice(const char* text)
{
    overlay_begin(G.title);
    overlay_text(text, false);
    overlay_button(overlay(), "OK", ask_cb, kGraceOk, true);
    overlay_back(ask_cb, kGraceOk);
}

// The session ended: record what it means here (once), then let it go.
// navigate: it ended while this game was on screen (then Goodbye from the
// other board takes this one back to the Play page too).
void wl_ended(bool navigate, bool quiet = false)
{
    net::Link* L = link();
    if (!L || !L->ended()) return;
    handled_end = L->session();
    const End why = L->end_reason();
    if (!S.recorded && !game_done()) {
        if (why == End::PeerForfeited) {
            S.recorded = 1;
            record(twoplayer::Result::Side1);              // they left: a win here
            if (!quiet) { sound(Sound::Win); kit::flash(); }
        } else if (why == End::YouForfeited) {
            S.recorded = 1;
            record(twoplayer::Result::Side2);
        }
    }
    if (overlay_open() && !quiet) close_overlays();       // a timer or "no reply" question is moot
    comm = 0;
    wplay::session_finished();
    update_status();
    // News the player must not miss gets its own little page (the info
    // line is one line: a long name would cut it short)
    if (navigate && !quiet && why != End::PeerDone) {
        char t[112];
        wireless_note(t, sizeof t);
        if (why == End::PeerForfeited && !game_done()) snprintf(t + strlen(t), sizeof t - strlen(t), " You win!");
        if (t[0]) end_notice(t);
    }
    if (why == End::PeerDone && navigate) {
        char t[96];
        snprintf(t, sizeof t, "%s said goodbye. Thanks for the game!", peer());
        wplay::back_after_game(t);
    }
}

void forfeit(bool by_time = false)
{
    net::Link* L = link();
    if (!L || L->ended()) return;
    char t[96];
    snprintf(t, sizeof t, "You forfeited %s with %s.", G.title, peer());
    L->forfeit(now_ms, by_time);
    log_event("Wireless: %s forfeited%s", G.id, by_time ? " (move timer)" : "");
    wl_ended(false);
    sound(Sound::Lose);
    if (!by_time) wplay::back_after_game(t);
    else end_notice("You ran out of time and forfeited the game.");   // it shows here, over the board
}

void ask_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    close_overlays();
    switch (id) {
        case kLeave: forfeit(); break;
        case kKeepWaiting:
            comm = 2;
            comm_deadline = now_ms + comm_ms();
            break;
        case kCloseGame:
            if (net::Link* L = link()) {
                L->close(now_ms);
                log_event("Wireless: %s closed after losing touch", G.id);
                wl_ended(false);
                wplay::back_after_game("Communications failed. Game not counted.");
            }
            break;
        default: break;
    }
    if (G.redraw) G.redraw();
    update_status();
}

// The one "are you sure" (Tom): leaving a 2P game forfeits it
void confirm_leave()
{
    char t[48];
    snprintf(t, sizeof t, "Leave %s?", G.title);
    overlay_begin(t);
    overlay_text("Leaving will forfeit this game.", false);
    overlay_button(overlay(), "Keep Playing", ask_cb, kKeepPlaying, true);
    overlay_button(overlay(), "Leave Game", ask_cb, kLeave);
    overlay_back(ask_cb, kKeepPlaying);
}

void grace_question()
{
    overlay_begin("Move Timer");
    overlay_text("You haven't responded in time. You will forfeit if you do not respond in 10 seconds.", false);
    overlay_button(overlay(), "OK", ask_cb, kGraceOk, true);
    overlay_back(ask_cb, kGraceOk);
}

void no_reply_question()
{
    char t[128];
    overlay_begin("No Reply");
    snprintf(t, sizeof t, "No reply from %s. Do you want to close the game, or keep waiting?", peer());
    overlay_text(t, false);
    overlay_button(overlay(), "Keep Waiting", ask_cb, kKeepWaiting, true);
    overlay_button(overlay(), "Close Game", ask_cb, kCloseGame);
    overlay_back(ask_cb, kKeepWaiting);
}

void again_cb(lv_event_t*)
{
    if (wl()) {
        net::Link* L = link();
        if (L && !L->ended() && !L->again()) L->want_again(now_ms);
        update_status();
        return;
    }
    start_new(S.mode, S.level);
}

void new_cb(lv_event_t*)
{
    if (wl() && wl_live()) wplay::new_game_with_partner();
}

void done_cb(lv_event_t*)
{
    if (!wl()) return;
    net::Link* L = link();
    if (L && !L->ended()) {
        L->done(now_ms);                                   // Goodbye
        wl_ended(false);
        wplay::back_after_game("Thanks for playing!");
    } else {
        wplay::back_after_game(nullptr);
    }
}

void menu_pick(int id)
{
    if (id == kit::kForfeit) { forfeit(); return; }
    if (id == kit::kOptions) { if (G.options) G.options(); return; }
    if (id <= kit::kLevel2) start_new(Mode::Computer, static_cast<twoplayer::Level>(id));
    else if (id == kit::kPassAndPlay) start_new(Mode::PassAndPlay, S.level);
    else if (id == kit::kWireless) wplay::open_menu(open_menu);
}
void menu_stats() { kit::stats_two_player(G.id, G.sides, open_menu); }
void menu_back()  { if (G.redraw) G.redraw(); update_status(); }

// Wireless, every tick: a new game, the other board's moves, the move
// timer, losing touch, the end
void wl_tick(uint32_t now)
{
    net::Link* L = link();
    if (!L) return;
    if (L->started_next()) wl_game_starts();
    if (L->resumed()) { start_turn(); comm = 0; }
    if (L->ended()) {
        if (handled_end != L->session()) wl_ended(true);
        return;
    }
    // The other board's moves, one at a time (a game may animate each),
    // each checked against this board's rules first; held while a menu is
    // open, like the computer's (it would land unseen)
    const bool busy = (G.busy && G.busy()) || overlay_open();
    uint32_t m = 0;
    if (!busy && L->next_move(&m)) {
        if (over() || G.turn() == S.human_side || (G.legal && !G.legal(int(m)))) {
            log_event("Wireless: %s got a move it can't play (%lu)", G.id, (unsigned long)m);
            L->disagree(now);
            wplay::session_over(true);
            update_status();
        } else {
            if (L->ply() < 4) log_event("Wireless: %s ply %d played here from the partner (%lu)", G.id, L->ply() + 1,
                                        (unsigned long)m);
            G.play(int(m));
            L->played(m, now);
            after_move(true);
        }
    }
    // The partner is ahead and this board hasn't played its move: say why, once
    if (L->peer_game() == L->game_no() && L->peer_ply() > L->ply() && !L->voided()) {
        if (!behind_since) behind_since = now;
        if (now - behind_since > 3000 && behind_logged != L->peer_ply()) {
            behind_logged = L->peer_ply();
            char r[112];
            wplay::radio_report(r, sizeof r);
            uint32_t mm = 0;
            log_event("Wireless: %s behind: ply %d, partner %d, move %s, overlay %d, busy %d, turn %d/%d; %s", G.id,
                      L->ply(), L->peer_ply(), L->next_move(&mm) ? "waiting" : "missing", overlay_open() ? 1 : 0,
                      (G.busy && G.busy()) ? 1 : 0, G.turn(), S.human_side, r);
        }
    } else {
        behind_since = 0;
    }
    if (L->voided()) wplay::session_over(true);
    const bool up = L->up(now);
    if (up != was_up && wl_going()) {
        char r[112];
        wplay::radio_report(r, sizeof r);
        log_event("Wireless: %s link %s at ply %d; %s", G.id, up ? "back" : "down", L->ply(), r);
    }
    // Losing touch: after a whole move time with nothing heard, ask; kept
    // waiting another move time with nothing = put the session away
    if (up) {
        if (!was_up) start_turn();                           // back: a fresh move time
        if (comm) {
            if (comm == 1 && overlay_open()) close_overlays();
            comm = 0;
        }
    } else if (wl_going()) {
        const uint32_t since = L->heard() ? L->heard_ms() : turn_start_ms;
        if (comm == 0 && now - since > comm_ms()) {
            comm = 1;
            comm_deadline = now + comm_ms();
            sound(Sound::Call);
            no_reply_question();
        } else if (comm && int32_t(now - comm_deadline) >= 0) {
            comm = 0;
            if (overlay_open()) close_overlays();
            wplay::suspend_session();
            return;
        }
    }
    was_up = up;
    // The move timer: at 0 the late player gets 10 seconds more, then forfeits
    const int32_t left = timer_left_ms();
    if (left != INT32_MIN && G.turn() == S.human_side) {
        if (left <= 0 && !grace_up) {
            grace_up = true;
            sound(Sound::Error);
            grace_question();
        }
        if (left <= -int32_t(kGraceMs)) {
            if (overlay_open()) close_overlays();
            forfeit(true);
            return;
        }
    }
    const int secs = left == INT32_MIN ? -1 : int(left / 1000);
    // What the status and info lines say about the link: redraw on a change
    const int look = (up ? 1 : 0) | (L->peer_again() ? 4 : 0) | (L->again() ? 8 : 0) | (L->voided() ? 16 : 0)
                   | (int32_t(start_note_until - now) > 0 ? 32 : 0);
    if (look != link_look || secs != last_secs) {
        link_look = look;
        last_secs = secs;
        update_status();
    }
}

} // namespace

State& state() { return S; }
bool take_wireless_start();

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
    // The header's back arrow leaves the game; in a wireless game that is
    // still going it asks first: leaving forfeits (Tom)
    set_game_back_hook([]() -> bool {
        if (!attached || !wl_going()) return false;
        confirm_leave();
        return true;
    });
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
    key_pad = pad;
    key_full_w = m.w - 2 * pad;
    key_half_w = (m.w - 3 * pad) / 2;
    const int ky = m.h - pad - ih - pad - kh;
    again_k = make_key(scr, key_full_w, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);
    new_k = make_key(scr, key_half_w, kh, new_cb, 0);
    key_label(new_k, "New Game", menu_font());
    lv_obj_set_pos(new_k, pad, ky);
    lv_obj_set_hidden(new_k, true);
    done_k = make_key(scr, key_half_w, kh, done_cb, 0);
    key_label(done_k, "Done", menu_font());
    lv_obj_set_pos(done_k, pad + key_half_w + pad, ky);
    lv_obj_set_hidden(done_k, true);
    *top = bar.h;
    *bottom = ky - pad;
}

void restart_view()
{
    clock_ = kit::Clock{};
    start_failed = false;
    was_busy = false;
    now_ms = lv_tick_get();
    link_look = -1;
    last_secs = -1;
    comm = 0;
    was_up = true;
    if (wl() && link()) {
        S.human_side = uint8_t(link()->my_side());
        start_turn();
        // It ended while this game was closed: take it in without leaving
        if (link()->ended() && handled_end != link()->session()) wl_ended(false);
    }
    if (!wl() && wplay::take_resumed_note(G.id)) resumed_note_until = now_ms + 8000;
    if (take_wireless_start()) return;
    // A session for this game that the game doesn't know about (it never
    // switched to it - e.g. a crash in between): it can't be played; let it go
    if (!wl() && wplay::session_for(G.id)) wplay::clear_sessions();
    if (computer_to_move()) think_after_ms = now_ms + think_pause();
    update_status();
}

bool take_wireless_start()
{
    if (!attached || !wplay::take_start(G.id)) return false;
    // A new session agreed in Wireless Play: whatever was on here gives way
    // (a one-player game was put aside by wplay and comes back afterwards)
    ai_stop();
    thinking = false;
    close_overlays();
    S.mode = Mode::Wireless;
    wl_game_starts();
    return true;
}

void refresh() { update_status(); }

void detach()
{
    ai_stop();
    thinking = false;
    kit::flash_stop();
    bar = kit::TopBar{};
    info_l = again_k = new_k = done_k = nullptr;
    attached = false;
    set_game_back_hook(nullptr);
}

void closed()
{
    // The game closed (after its last save). A session that ended is taken
    // in (recorded) quietly; leaving after a game is over = Goodbye. Its
    // one-player game comes back once the session is over.
    if (net::Link* L = link()) {
        if (!L->ended() && game_done()) L->done(now_ms);
        if (L->ended() && handled_end != L->session()) wl_ended(false, true);
        wplay::session_save();
    }
    wplay::game_closed(G.id);
}

bool human_may_move()
{
    if (!attached || over() || overlay_open()) return false;
    if (wl()) {
        net::Link* L = link();
        uint32_t m;
        return L && !L->ended() && !L->voided() && !L->suspended() && L->up(now_ms) && !L->next_move(&m)
            && G.turn() == S.human_side && !(G.busy && G.busy());
    }
    return S.mode == Mode::PassAndPlay || G.turn() == S.human_side;
}

int legal_moves(int* out, int cap)
{
    if (G.list) return G.list(out, cap);
    int n = 0;
    for (int m = 0; m < 32768 && n < cap && G.legal; ++m) if (G.legal(m)) out[n++] = m;
    return n;
}

bool try_move(int move)
{
    if (!human_may_move() || (G.legal && !G.legal(move))) return false;
    human_move(move);
    return true;
}

void human_move(int move)
{
    if (!human_may_move()) return;
    if (G.legal && !G.legal(move)) return;          // never play (or send) an illegal move
    S.human_moved = 1;
    net::Link* L = link();
    if (L && L->ply() < 4) log_event("Wireless: %s ply %d played here (%d)", G.id, L->ply() + 1, move);
    G.play(move);
    if (L) L->played(uint32_t(move), now_ms);
    after_move(false);
}

void tick(uint32_t now)
{
    now_ms = now;
    if (!attached) return;
    if (take_wireless_start()) return;
    if (wl()) wl_tick(now);
    if (clock_.tick(now, !over() && (!wl() || wl_live()), S.seconds)) update_status();
    // Computer: start thinking after the pause, pick up the move when done
    // A game still animating its last move holds the computer back; the
    // pause starts when the animation ends
    const bool busy = G.busy && G.busy();
    if (was_busy && !busy && computer_to_move()) think_after_ms = now + think_pause();
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
    if (wl_going()) {
        // No Forfeit once a game is over (Tom)
        char line[112];
        snprintf(line, sizeof line, "Playing %s. Forfeit Game: a loss for you, a win for %s.", peer(), peer());
        h.exit_game = confirm_leave;
        kit::menu_wireless(G.title, line, h, G.options ? "Options" : nullptr);
        return;
    }
    kit::menu_two_player(G.title, h, G.legal && wplay::radio_present(), G.options ? "Options" : nullptr);
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
    if (wl()) wplay::session_save();      // the link's state goes with every save
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
