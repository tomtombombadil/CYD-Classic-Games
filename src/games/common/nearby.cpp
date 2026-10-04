#include "nearby.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <lvgl.h>
#include "games/registry.h"
#include "ui/keyboard.h"
#include "ui/shell.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace nearby {

using namespace ui;

namespace {

// ---- Radio ------------------------------------------------------------------------------------
int      radio_refs = 0;
net::Air the_air;

void air_send(const uint8_t* d, size_t n, void*)
{
    if (radio_refs > 0 && shell().radio_send) shell().radio_send(d, n);
}

// ---- Player name ------------------------------------------------------------------------------
char name_buf[net::kNameMax + 1] = "";
constexpr const char* kNameFile = "player";

// ---- Play Nearby ------------------------------------------------------------------------------
enum Key : intptr_t { kBack = 1, kChange, kPlay, kNo, kStop, kBoard0 = 100 };
constexpr int kMaxRows = 4;

struct LobbyState {
    net::Lobby  lobby;
    const char* game_id = "";
    const char* title = "";
    Started     started = nullptr;
    void      (*back)() = nullptr;
    bool        radio_failed = false;
    bool        keyboard_up = false;
    char        note[64] = "";
    uint32_t    note_until = 0;
    char        shown[320] = "";           // what the overlay shows, to rebuild only on a change
    int         rows[kMaxRows] = {};       // board index behind each row key
};
LobbyState* L = nullptr;

void set_note(const char* text)
{
    snprintf(L->note, sizeof L->note, "%s", text);
    L->note_until = lv_tick_get() + 5000;
}

const char* game_title(const char* id)
{
    const int g = games::find(id);
    return g >= 0 ? games::get(g).title : id;
}

// The order the boards are listed in: those you can ask first
int order(int* out)
{
    int n = 0;
    const net::Lobby& lb = L->lobby;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < lb.count() && n < kMaxRows; ++i)
            if (lb.can_invite(i) == (pass == 0)) out[n++] = i;
    return n;
}

void row_text(int i, char* buf, size_t cap)
{
    const net::Lobby& lb = L->lobby;
    const net::Nearby& b = lb.at(i);
    if (lb.can_invite(i))           snprintf(buf, cap, "%s", b.name);
    else if (!lb.same_version(i))   snprintf(buf, cap, "%s (other version)", b.name);
    else                            snprintf(buf, cap, "%s (%s)", b.name, game_title(b.game));
}

// Everything the overlay shows, as one string
void describe(char* buf, size_t cap)
{
    const net::Lobby& lb = L->lobby;
    int n = snprintf(buf, cap, "%s|%d|%s|", player_name(), L->radio_failed ? 1 : 0, L->note);
    if (lb.asked())         n += snprintf(buf + n, cap - n, "asked %s", lb.asker_name());
    else if (lb.inviting()) n += snprintf(buf + n, cap - n, "asking %s", lb.invitee_name());
    else {
        int idx[kMaxRows];
        const int k = order(idx);
        for (int r = 0; r < k && n < int(cap); ++r) {
            char t[48];
            row_text(idx[r], t, sizeof t);
            n += snprintf(buf + n, cap - n, "%s;", t);
        }
    }
}

void build();

void name_done(const char* text)
{
    if (!L) return;
    if (text[0]) set_player_name(text);
    L->lobby.set_name(player_name(), lv_tick_get());
    L->keyboard_up = false;
    build();
}

void key_cb(lv_event_t* e)
{
    if (!L) return;
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const uint32_t now = lv_tick_get();
    net::Lobby& lb = L->lobby;
    if (id == kBack) {
        void (*back)() = L->back;
        lobby_close();
        if (back) back();
        return;
    }
    if (id == kChange) {
        L->keyboard_up = true;
        keyboard_open("Your Name", player_name(), net::kNameMax, name_done);
        return;
    }
    if (id == kPlay) { lb.accept(now); lobby_tick(now); return; }
    if (id == kNo)   { lb.decline(now); build(); return; }
    if (id == kStop) { lb.cancel(now); build(); return; }
    if (id >= kBoard0 && id < kBoard0 + kMaxRows) {
        const int i = L->rows[id - kBoard0];
        if (i < lb.count() && lb.can_invite(i)) {
            const uint32_t session = shell().random_seed ? shell().random_seed() : now;
            lb.invite(i, session ? session : 1, now);
            build();
        }
    }
}

lv_obj_t* new_row(int h)
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), h);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    return r;
}

void build()
{
    if (!L || L->keyboard_up) return;
    describe(L->shown, sizeof L->shown);
    const net::Lobby& lb = L->lobby;
    const int kh = menu_btn_h();
    overlay_begin("Play Nearby");
    // Who you are (Change Name sits beside Back at the bottom)
    char t[96];
    snprintf(t, sizeof t, "You are %s.", player_name());
    overlay_text(t, false);

    if (L->radio_failed) {
        overlay_text("The radio couldn't start: the board is low on memory. "
                     "Exit Game, open the game again and try once more.", false);
    } else if (lb.asked()) {
        snprintf(t, sizeof t, "%s asks you to play %s. %s moves first.", lb.asker_name(), L->title, lb.asker_name());
        overlay_text(t, false);
        overlay_pair("Play", key_cb, kPlay, "No Thanks", key_cb, kNo);
        lv_obj_t* r = lv_obj_get_child(overlay(), -1);
        set_checked(lv_obj_get_child(r, 0), true);
    } else if (lb.inviting()) {
        snprintf(t, sizeof t, "Asking %s to play %s...", lb.invitee_name(), L->title);
        overlay_text(t, false);
        overlay_button(overlay(), "Stop Asking", key_cb, kStop);
    } else {
        int idx[kMaxRows];
        const int n = order(idx);
        for (int r = 0; r < n; ++r) {
            L->rows[r] = idx[r];
            row_text(idx[r], t, sizeof t);
            lv_obj_t* k = overlay_button(overlay(), t, key_cb, kBoard0 + r);
            if (!lb.can_invite(idx[r])) set_dim(k, true);
        }
        if (n == 0) overlay_text("Looking for boards nearby...", false);
        snprintf(t, sizeof t, "Boards show up here while they are in Play Nearby. Tap one to ask it to play %s.",
                 L->title);
        overlay_text(t, true);
    }
    if (L->note[0]) overlay_text(L->note, false);
    lv_obj_t* bottom = new_row(kh);
    lv_obj_set_ignore_layout(bottom, true);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_t* bk = make_key(bottom, 10, kh, key_cb, kBack);
    lv_obj_set_flex_grow(bk, 2);                   // "Change Name" needs the room
    lv_obj_add_state(bk, LV_STATE_CHECKED);
    key_label(bk, "Back", menu_font());
    lv_obj_t* ck = make_key(bottom, 10, kh, key_cb, kChange);
    lv_obj_set_flex_grow(ck, 3);
    key_label(ck, "Change Name", menu_font());
}

} // namespace

// ---- Radio ------------------------------------------------------------------------------------
bool available()
{
    const Shell& H = shell();
    return H.radio_on && H.radio_off && H.radio_send && H.radio_recv && H.radio_mac;
}

bool radio_start()
{
    if (!available()) return false;
    if (radio_refs == 0) {
        if (!shell().radio_on()) {
            log_event("Wireless: the radio could not start");
            return false;
        }
        uint32_t fr = 0, big = 0;
        if (shell().memory) shell().memory(&fr, &big);
        log_event("Wireless: radio on, %lu KB free", (unsigned long)(fr / 1024));
    }
    ++radio_refs;
    return true;
}

void radio_stop()
{
    if (radio_refs <= 0) return;
    if (--radio_refs == 0) {
        shell().radio_off();
        log_step("Wireless: radio off");
    }
}

bool radio_running() { return radio_refs > 0; }

const net::Air& air()
{
    the_air.send = air_send;
    the_air.ctx = nullptr;
    return the_air;
}

net::Mac my_mac()
{
    net::Mac m;
    if (shell().radio_mac) shell().radio_mac(m.b);
    return m;
}

bool receive(net::Mac* from, uint8_t* buf, size_t* len)
{
    if (radio_refs <= 0 || !shell().radio_recv) return false;
    const size_t n = shell().radio_recv(from->b, buf, net::kPacketMax);
    if (!n) return false;
    *len = n;
    return true;
}

// ---- Player name ------------------------------------------------------------------------------
const char* player_name()
{
    if (!name_buf[0]) {
        uint8_t buf[4 + net::kNameMax + 1] = {};
        const Shell& H = shell();
        const size_t n = H.load_game ? H.load_game(kNameFile, buf, sizeof buf) : 0;
        if (n == sizeof buf && memcmp(buf, "PLR1", 4) == 0) {
            buf[sizeof buf - 1] = 0;
            net::clean_name(reinterpret_cast<const char*>(buf + 4), name_buf, sizeof name_buf);
        }
        if (!name_buf[0]) net::default_name(my_mac(), name_buf, sizeof name_buf);
    }
    return name_buf;
}

void set_player_name(const char* name)
{
    char clean[net::kNameMax + 1];
    net::clean_name(name, clean, sizeof clean);
    if (!clean[0]) return;
    snprintf(name_buf, sizeof name_buf, "%s", clean);
    uint8_t buf[4 + net::kNameMax + 1] = {'P', 'L', 'R', '1'};
    memcpy(buf + 4, name_buf, strlen(name_buf));
    if (shell().save_game) shell().save_game(kNameFile, buf, sizeof buf);
}

// ---- Play Nearby ------------------------------------------------------------------------------
void lobby_open(const char* game_id, const char* title, Started started, void (*back)())
{
    lobby_close();
    L = new (std::nothrow) LobbyState();
    if (!L) { if (back) back(); return; }
    L->game_id = game_id;
    L->title = title;
    L->started = started;
    L->back = back;
    const uint32_t now = lv_tick_get();
    L->radio_failed = !radio_start();
    L->lobby.begin(my_mac(), player_name(), game_id, shell().firmware_version ? shell().firmware_version : "", air(), now);
    build();
}

bool lobby_active() { return L != nullptr; }

void lobby_tick(uint32_t now)
{
    if (!L) return;
    if (!L->radio_failed) {
        net::Mac from;
        uint8_t buf[net::kPacketMax];
        size_t n;
        for (int k = 0; k < 16 && receive(&from, buf, &n); ++k) L->lobby.receive(from, buf, n, now);
        L->lobby.tick(now);
    }
    switch (L->lobby.poll()) {
        case net::Lobby::Event::Started: {
            const net::Lobby& lb = L->lobby;
            const net::Mac peer = lb.partner();
            char name[net::kNameMax + 1];
            snprintf(name, sizeof name, "%s", lb.partner_name());
            const uint32_t session = lb.session();
            const bool inviter = lb.inviter();
            Started cb = L->started;
            log_event("Wireless: %s with %s", L->game_id, name);
            radio_start();                         // the game's hold on the radio
            lobby_close();
            close_overlays();
            if (cb) cb(peer, name, session, inviter);
            radio_stop();                          // the game took its own if it wanted one
            return;
        }
        case net::Lobby::Event::Declined: {
            char t[64];
            snprintf(t, sizeof t, "%s said no thanks.", L->lobby.invitee_name());
            set_note(t);
            break;
        }
        case net::Lobby::Event::Gone: {
            char t[64];
            snprintf(t, sizeof t, "%s went away.", L->lobby.invitee_name());
            set_note(t);
            break;
        }
        case net::Lobby::Event::None: break;
    }
    if (L->note[0] && int32_t(now - L->note_until) >= 0) L->note[0] = 0;
    if (L->keyboard_up) return;
    char now_shows[sizeof L->shown];
    describe(now_shows, sizeof now_shows);
    if (strcmp(now_shows, L->shown) != 0) build();
}

void lobby_close()
{
    if (!L) return;
    if (!L->radio_failed) radio_stop();
    delete L;
    L = nullptr;
}

} // namespace nearby
