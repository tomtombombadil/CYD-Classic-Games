#include "match.h"

#include <cstdio>
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
lv_obj_t*    again_k = nullptr;        // Play Again (full width; half with Done beside it)
lv_obj_t*    done_k  = nullptr;        // wireless: Done (no more games) / Wireless Play
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

// Wireless: the session's link lives in the Wireless Play service (wplay.*)
int      link_look = -1;              // the link's state the screen shows
uint32_t handled_end = 0;             // the session whose end was dealt with here

bool over()             { return G.result && G.result() != -1; }
bool computer_to_move() { return S.mode == Mode::Computer && !over() && G.turn() != S.human_side; }
bool wl()               { return S.mode == Mode::Wireless; }
net::Link* link()       { return wl() ? wplay::session_for(G.id) : nullptr; }
bool wl_live()          { net::Link* l = link(); return l && !l->ended(); }
const char* peer()      { net::Link* l = link(); return l ? l->peer_name() : "Partner"; }

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
    if (!L) { snprintf(buf, cap, "Wireless game over. New games are in the menu"); return; }
    switch (L->end_reason()) {
        case End::PeerForfeited: snprintf(buf, cap, "%s forfeited the game", peer()); return;
        case End::YouForfeited:  snprintf(buf, cap, "You forfeited the game"); return;
        case End::PeerDone:      snprintf(buf, cap, "%s is done playing. Thanks!", peer()); return;
        case End::YouDone:       snprintf(buf, cap, "Thanks for playing!"); return;
        case End::PeerGone:      snprintf(buf, cap, "%s's board ended this game", peer()); return;
        case End::OutOfStep:     snprintf(buf, cap, "The boards' games differ: it ended"); return;
        case End::None:          break;
    }
    if (!L->up(now_ms)) { snprintf(buf, cap, "%s's board is out of range", peer()); return; }
    if (L->peer_away()) { snprintf(buf, cap, "%s closed %s for now", peer(), G.title); return; }
    if (over()) {
        if (L->again() && !L->peer_again()) snprintf(buf, cap, "Waiting for %s to play again", peer());
        else if (L->peer_again())          snprintf(buf, cap, "%s wants to play again!", peer());
        else                               snprintf(buf, cap, "Play again with %s?", peer());
    }
}

void place_keys()
{
    if (!again_k || !done_k) return;
    // Wireless: [Play Again | Done] while a rematch is possible; just
    // "Wireless Play" once the session is over. Elsewhere: Play Again.
    net::Link* L = link();
    const bool show_wl = wl() && over();
    const bool rematch = show_wl && L && !L->ended();
    const bool ended = wl() && (!L || L->ended());
    lv_obj_set_hidden(again_k, !(over() && (!wl() || rematch)));
    lv_obj_set_hidden(done_k, !(rematch || ended));
    lv_obj_set_width(again_k, rematch ? key_half_w : key_full_w);
    lv_obj_set_width(done_k, rematch ? key_half_w : key_full_w);
    lv_obj_set_x(done_k, rematch ? key_pad + key_half_w + key_pad : key_pad);
    lv_label_set_text(lv_obj_get_child(done_k, 0), rematch ? "Done" : "Wireless Play");
    set_checked(again_k, !(L && L->again()));
    set_checked(done_k, ended);
}

void update_status()
{
    if (!bar.center) return;
    char t[16];
    twoplayer::format_time(t, sizeof t, S.seconds);
    lv_label_set_text(bar.left, t);
    char st[40];
    const char* short_st = nullptr;     // when the header has no room for it
    const int r = G.result();
    if (wl()) {
        net::Link* L = link();
        if (r == 2)                    snprintf(st, sizeof st, "Draw");
        else if (r == S.human_side)    snprintf(st, sizeof st, "You win!");
        else if (r >= 0)               snprintf(st, sizeof st, "%s wins", peer());
        else if (!L)                   snprintf(st, sizeof st, "Game over");
        else if (L->end_reason() == End::PeerForfeited) snprintf(st, sizeof st, "You win!");
        else if (L->end_reason() == End::YouForfeited)  snprintf(st, sizeof st, "Forfeited");
        else if (L->ended())           snprintf(st, sizeof st, "Game ended");
        else if (!L->up(now_ms) || L->peer_away()) { snprintf(st, sizeof st, "Waiting for %s...", peer()); short_st = "Waiting..."; }
        else if (G.turn() == S.human_side) { snprintf(st, sizeof st, "Your turn (%s)", side_name(S.human_side)); short_st = "Your turn"; }
        else                           snprintf(st, sizeof st, "%s's turn", peer());
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
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
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
    S.mode = Mode::Wireless;
    S.human_side = uint8_t(L->my_side());
    S.recorded = 0;
    S.human_moved = 0;
    S.seconds = 0;
    clock_ = kit::Clock{};
    G.reset();
    if (G.redraw) G.redraw();
    wplay::session_over(false);
    wplay::session_save();
    update_status();
}

// The session ended: record what it means here (once), then let it go.
// navigate: it ended while this game was on screen (then "no more games"
// from the other board takes this one back to Wireless Play too).
void wl_ended(bool navigate)
{
    net::Link* L = link();
    if (!L || !L->ended()) return;
    handled_end = L->session();
    const End why = L->end_reason();
    if (!S.recorded) {
        S.recorded = 1;
        if (why == End::PeerForfeited && !over()) {
            record(twoplayer::Result::Side1);              // they left: a win here
            sound(Sound::Win);
            kit::flash();
        } else if (why == End::YouForfeited && !over()) {
            record(twoplayer::Result::Side2);
        }
    }
    wplay::session_finished();
    update_status();
    // "No more games" from the other board: back to Wireless Play together
    if (why == End::PeerDone && navigate) {
        char t[96];
        snprintf(t, sizeof t, "%s is done playing. Thanks for the game!", peer());
        wplay::back_after_game(t);
    }
}

void forfeit()
{
    net::Link* L = link();
    if (!L || L->ended()) return;
    char t[96];
    snprintf(t, sizeof t, "You forfeited %s with %s.", G.title, peer());
    L->forfeit(now_ms);
    log_event("Wireless: %s forfeited", G.id);
    wl_ended(false);
    sound(Sound::Lose);
    wplay::back_after_game(t);
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

void done_cb(lv_event_t*)
{
    if (!wl()) return;
    net::Link* L = link();
    if (L && !L->ended()) {
        L->done(now_ms);
        wl_ended(false);
        wplay::back_after_game("Thanks for playing!");
    } else {
        wplay::back_after_game(nullptr);
    }
}

void menu_pick(int id)
{
    if (id == kit::kForfeit) { forfeit(); return; }
    if (id <= kit::kLevel2) start_new(Mode::Computer, static_cast<twoplayer::Level>(id));
    else if (id == kit::kPassAndPlay) start_new(Mode::PassAndPlay, S.level);
    else if (id == kit::kWireless) wplay::open_menu(open_menu);
}
void menu_stats() { kit::stats_two_player(G.id, G.sides, open_menu); }
void menu_back()  { if (G.redraw) G.redraw(); update_status(); }

// Wireless, every tick: a new game, the other board's moves, the end
void wl_tick(uint32_t now)
{
    net::Link* L = link();
    if (!L) return;
    if (L->started_next()) wl_game_starts();
    if (L->ended()) {
        if (handled_end != L->session()) wl_ended(true);
        return;
    }
    // The other board's moves, one at a time (a game may animate each),
    // each checked against this board's rules first; held while a menu is
    // open, like the computer's (it would land unseen)
    const bool busy = (G.busy && G.busy()) || overlay_open();
    const int m = busy ? -1 : L->next_move();
    if (m >= 0) {
        if (over() || G.turn() == S.human_side || (G.legal && !G.legal(m))) {
            log_event("Wireless: %s got a move it can't play (%d)", G.id, m);
            L->disagree(now);
            update_status();
        } else {
            G.play(m);
            L->played(m, now);
            after_move(true);
        }
    }
    // What the status and info lines say about the link: redraw on a change
    const int look = (L->up(now) ? 1 : 0) | (L->peer_away() ? 2 : 0) | (L->peer_again() ? 4 : 0) | (L->again() ? 8 : 0);
    if (look != link_look) {
        link_look = look;
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
    // still going it forfeits (Tom: going back may cost you the game)
    set_game_back_hook([]() -> bool {
        if (!attached || !wl_live()) return false;
        forfeit();
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
    link_look = -1;
    if (wl() && link()) {
        S.human_side = uint8_t(link()->my_side());
        // It ended while this game was closed: take it in without leaving
        if (link()->ended() && handled_end != link()->session()) wl_ended(false);
    }
    if (take_wireless_start()) return;
    if (computer_to_move()) think_after_ms = now_ms + kThinkPauseMs;
    update_status();
}

bool take_wireless_start()
{
    if (!attached || !wplay::take_start(G.id)) return false;
    // A new session agreed in Wireless Play: whatever was on here gives way
    ai_stop();
    thinking = false;
    if (!over() && !S.recorded && S.mode == Mode::Computer && S.human_moved)
        record(twoplayer::Result::Side2);
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
    info_l = again_k = done_k = nullptr;
    attached = false;
    set_game_back_hook(nullptr);
}

void closed()
{
    // A wireless game closed here is paused: the service tells the other board
    if (wl()) wplay::session_save();
}

bool human_may_move()
{
    if (!attached || over() || overlay_open()) return false;
    if (wl()) {
        net::Link* L = link();
        return L && !L->ended() && L->up(now_ms) && !L->peer_away() && L->next_move() < 0
            && G.turn() == S.human_side && !(G.busy && G.busy());
    }
    return S.mode == Mode::PassAndPlay || G.turn() == S.human_side;
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
    G.play(move);
    if (net::Link* L = link()) L->played(move, now_ms);
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
    if (wl_live()) {
        char title[48], line[96];
        snprintf(title, sizeof title, "%s With %s", G.title, peer());
        snprintf(line, sizeof line, "Forfeit Game ends it as a loss for you and a win for %s. "
                                    "Exit Game pauses it.", peer());
        kit::menu_wireless(title, line, h);
        return;
    }
    kit::menu_two_player(G.title, h, G.legal && wplay::radio_present());
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
