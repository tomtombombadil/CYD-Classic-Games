#include "wplay.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <lvgl.h>
#include "games/registry.h"
#include "match.h"
#include "net/names.h"
#include "net_games.h"
#include "two_player.h"
#include "game_kit.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace wplay {

using namespace ui;

namespace {

constexpr const char* kFlasher = "tomtombombadil.github.io/CYD-Classic-Games";
constexpr uint32_t kConnectMs = 10000;       // Connecting... gives up after this

// ---- The wireless games -------------------------------------------------------------------------
int wl_games[net::kMaxGames];               // registry index
int wl_keys[net::kMaxGames];                // net_games key
int wl_n = -1;

void collect()
{
    if (wl_n >= 0) return;
    wl_n = 0;
    for (int i = 0; i < games::count() && wl_n < net::kMaxGames; ++i) {
        if (!(games::get(i).modes & games::kNetwork)) continue;
        const netgames::Entry* e = netgames::by_id(games::get(i).id);
        if (!e) continue;                    // a wireless game needs a key (net_games.h)
        wl_games[wl_n] = i;
        wl_keys[wl_n] = e->key;
        ++wl_n;
    }
}

int index_of_key(int key)
{
    collect();
    for (int g = 0; g < wl_n; ++g) if (wl_keys[g] == key) return g;
    return -1;
}
const char* title_of_key(int key) { const int g = index_of_key(key); return g >= 0 ? games::get(wl_games[g]).title : "?"; }
int version_of_key(int key) { const netgames::Entry* e = netgames::by_key(key); return e ? e->version : -1; }

// ---- Names --------------------------------------------------------------------------------------
// A name for showing (rotating buffers: a few may be in use at once)
const char* show_name(uint16_t a, uint16_t b)
{
    static char buf[4][names::kNameMax + 1];
    static int k = 0;
    char* out = buf[k = (k + 1) % 4];
    if (a == 0xFFFF) snprintf(out, names::kNameMax + 1, "Older board");
    else names::format(a, b, out, names::kNameMax + 1);
    return out;
}

// ---- The player's setup -----------------------------------------------------------------------
struct Profile {
    uint16_t name_a = 0, name_b = 0;
    bool     named = false;
    uint16_t games = 0xFFFF;                // bit key-1 = will play that game
    bool     available = false;             // 2P
    uint16_t timer = 30;                    // Move Timer, seconds; 0 = Off
};
Profile P;
bool    loaded = false;
constexpr const char* kProfileFile = "player";
constexpr size_t kProfileBytes = 4 + 2 + 2 + 2 + 1 + 2;
const uint16_t kTimers[] = {30, 60, 120, 300, 0};

bool game_on(int key) { return key >= 1 && key <= 16 && ((P.games >> (key - 1)) & 1); }

net::Mac my_mac()
{
    net::Mac m;
    if (shell().radio_mac) shell().radio_mac(m.b);
    return m;
}

void save_profile()
{
    uint8_t buf[kProfileBytes] = {'P', 'L', 'R', '3'};
    buf[4] = uint8_t(P.name_a); buf[5] = uint8_t(P.name_a >> 8);
    buf[6] = uint8_t(P.name_b); buf[7] = uint8_t(P.name_b >> 8);
    buf[8] = uint8_t(P.games); buf[9] = uint8_t(P.games >> 8);
    buf[10] = P.available ? 1 : 0;
    buf[11] = uint8_t(P.timer); buf[12] = uint8_t(P.timer >> 8);
    if (shell().save_game) shell().save_game(kProfileFile, buf, sizeof buf);
}

// ---- Radio and presence -------------------------------------------------------------------------
bool           radio_on = false;
bool           dozing = false;
uint32_t       radio_retry_ms = 0;
bool           radio_failed = false;
net::Air       the_air;
net::Presence* pres = nullptr;

void air_send(const uint8_t* d, size_t n, void*)
{
    if (radio_on && shell().radio_send) shell().radio_send(d, n);
}

// ---- The session ----------------------------------------------------------------------------------
struct Session {
    int       reg = -1;                    // registry index of its game
    char      id[16] = "";
    net::Link link;
    bool      connecting = false;          // agreed, waiting to hear the other board
    uint32_t  connect_since = 0;
    bool      fresh = false;               // connected, its game hasn't started it yet
    bool      noted = false;               // its end was taken in (recorded)
    bool      over = false;                // its game is over (a rematch may follow): not busy
};
Session* S = nullptr;
constexpr const char* kSessionFile = "wl_session";
constexpr int kTombs = 2;
// Sessions that ended here: their ending is said again whenever the other
// board asks about them (a forfeit reaches a partner who was out of range)
struct Tomb {
    net::Mac peer;
    uint32_t session = 0;
    uint16_t flags = 0;
};
Tomb tombs[kTombs];
constexpr size_t kTombBytes = 6 + 4 + 2;
constexpr size_t kSessionBytes = 4 + 16 + 1 + net::Link::kSaveBytes + kTombs * kTombBytes;

bool live() { return S && !S->link.ended(); }
// In a session that is going (or put away, or starting): nobody can ask this board
bool busy() { return live() && (!S->over || S->link.suspended()); }

// Signal for the header's wifi icon: the partner's while a session is
// going, else the strongest board heard in the last few seconds
int8_t   sig_rssi = -127;
uint32_t sig_ms = 0;
void heard_signal(const net::Mac& from, int8_t rssi, uint32_t now)
{
    const bool partner = live() && from == S->link.peer();
    const bool stale = now - sig_ms > 3000;
    if (partner || (!live() && (stale || rssi > sig_rssi))) {
        sig_rssi = stale ? rssi : int8_t((sig_rssi * 3 + rssi) / 4);   // smoothed: no flicker
        sig_ms = now;
    }
}

void save_session()
{
    if (!shell().save_game) return;
    uint8_t buf[kSessionBytes] = {};
    memcpy(buf, "WLS2", 4);
    size_t at = 4 + 16 + 1;
    if (S) {
        memcpy(buf + 4, S->id, strlen(S->id));
        buf[20] = uint8_t((S->over ? 1 : 0) | (S->noted ? 2 : 0) | 4);
        if (!S->link.save(buf + at, net::Link::kSaveBytes)) return;
    }
    at += net::Link::kSaveBytes;
    for (const Tomb& t : tombs) {
        memcpy(buf + at, t.peer.b, 6);
        for (int k = 0; k < 4; ++k) buf[at + 6 + k] = uint8_t(t.session >> (8 * k));
        buf[at + 10] = uint8_t(t.flags);
        buf[at + 11] = uint8_t(t.flags >> 8);
        at += kTombBytes;
    }
    shell().save_game(kSessionFile, buf, sizeof buf);
}

void bury(const net::Link& l)
{
    for (int k = kTombs - 1; k > 0; --k) tombs[k] = tombs[k - 1];
    tombs[0].peer = l.peer();
    tombs[0].session = l.session();
    tombs[0].flags = l.end_flags();
}

void drop_session()
{
    if (S && S->link.ended()) bury(S->link);
    delete S;
    S = nullptr;
    save_session();
}

// ---- The one-player game put aside while a session plays its game ----------------------------
void stash_name(const char* id, char* out, size_t cap) { snprintf(out, cap, "1p_%s", id); }
char resumed_note_id[16] = "";

void stash_1p(int reg)
{
    const Shell& H = shell();
    if (!H.load_game || !H.save_game) return;
    const char* id = games::get(reg).id;
    if (app_current_game() == reg) app_save_current();
    char sn[24];
    stash_name(id, sn, sizeof sn);
    uint8_t* buf = static_cast<uint8_t*>(malloc(4096));
    if (!buf) return;
    if (H.load_game(sn, buf, 4096) > 4) { free(buf); return; }     // one is put aside already
    const size_t n = H.load_game(id, buf, 4096);
    match::State st;
    if (n > match::kStateBytes && match::read_state(buf + n - match::kStateBytes, match::kStateBytes, st)
        && st.mode != twoplayer::Mode::Wireless) {
        H.save_game(sn, buf, n);
        log_step("Wireless: %s one-player game put aside", id);
    }
    free(buf);
}

// Put it back (the game isn't open): it opens as it was, with a note
void restore_1p(const char* id)
{
    const Shell& H = shell();
    if (!H.load_game || !H.save_game) return;
    char sn[24];
    stash_name(id, sn, sizeof sn);
    uint8_t* buf = static_cast<uint8_t*>(malloc(4096));
    if (!buf) return;
    const size_t n = H.load_game(sn, buf, 4096);
    if (n > 4) {
        H.save_game(id, buf, n);
        const uint8_t none[4] = {'N', 'O', 'N', 'E'};
        H.save_game(sn, none, sizeof none);
        snprintf(resumed_note_id, sizeof resumed_note_id, "%s", id);
        log_step("Wireless: %s one-player game back", id);
    }
    free(buf);
}

// Record a session's ending for a game that isn't open (a put-away session
// the partner forfeited): the game's screen would have done it
void record_unseen_end()
{
    if (!S || S->noted || !S->link.ended()) return;
    S->noted = true;
    if (S->link.end_reason() == net::Link::End::PeerForfeited && !S->over) {
        twoplayer::Record r;
        r.mode = twoplayer::Mode::Wireless;
        r.result = twoplayer::Result::Side1;
        r.moves = uint16_t(S->link.ply());
        kit::record_two_player(S->id, r, twoplayer::Sides{"", ""});
    }
    save_session();
}

void load_all(uint32_t now)
{
    the_air.send = air_send;
    if (loaded) return;
    loaded = true;
    collect();
    const Shell& H = shell();
    uint8_t* buf = static_cast<uint8_t*>(malloc(kSessionBytes));
    if (!buf) return;
    size_t n = H.load_game ? H.load_game(kProfileFile, buf, kSessionBytes) : 0;
    if (n == kProfileBytes && memcmp(buf, "PLR3", 4) == 0) {
        P.name_a = uint16_t(buf[4] | buf[5] << 8);
        P.name_b = uint16_t(buf[6] | buf[7] << 8);
        P.named = P.name_a < names::first_count() && P.name_b < names::second_count();
        P.games = uint16_t(buf[8] | buf[9] << 8);
        P.available = buf[10] != 0;
        P.timer = uint16_t(buf[11] | buf[12] << 8);
    } else if (n >= 4 && memcmp(buf, "PLR2", 4) == 0 && n == 4 + 13 + 2 + 1) {
        // v0.20: a typed name (dropped - names are picked now), games by the
        // same order as the keys, 2P
        P.games = uint16_t(buf[4 + 13] | (buf[4 + 14] << 8));
        P.available = buf[4 + 15] != 0;
    }
    bool ok_timer = false;
    for (uint16_t t : kTimers) ok_timer |= t == P.timer;
    if (!ok_timer) P.timer = 30;
    if (!P.named) {
        // A starting name from the board's address (Random or Pick From List changes it)
        const net::Mac m = my_mac();
        names::random_pair(uint32_t(m.b[2]) << 24 | uint32_t(m.b[3]) << 16 | uint32_t(m.b[4]) << 8 | m.b[5],
                           &P.name_a, &P.name_b);
        P.named = true;
    }
    n = H.load_game ? H.load_game(kSessionFile, buf, kSessionBytes) : 0;
    if (n == kSessionBytes && memcmp(buf, "WLS2", 4) == 0) {
        size_t at = 4 + 16 + 1 + net::Link::kSaveBytes;
        for (Tomb& t : tombs) {
            memcpy(t.peer.b, buf + at, 6);
            t.session = 0;
            for (int k = 0; k < 4; ++k) t.session |= uint32_t(buf[at + 6 + k]) << (8 * k);
            t.flags = uint16_t(buf[at + 10] | buf[at + 11] << 8);
            at += kTombBytes;
        }
        const uint8_t f = buf[20];
        char id[17] = {};
        memcpy(id, buf + 4, 16);
        const int reg = games::find(id);
        if (f & 4) {
            Session* s = new (std::nothrow) Session();
            if (s && reg >= 0 && game_of(id) >= 0 && s->link.load(buf + 21, net::Link::kSaveBytes, the_air, now)) {
                s->reg = reg;
                s->over = f & 1;
                s->noted = (f & 2) != 0;
                snprintf(s->id, sizeof s->id, "%s", id);
                S = s;
                // Kept through a restart (Tom, 2026-10-04: an oops reboot
                // mustn't end a game); it comes back put away until both
                // players meet again. An ended one is let go.
                if (S->link.ended()) {
                    S->noted = true;
                    drop_session();
                } else {
                    log_event("Wireless: %s with %s put away (restart)", S->id,
                              show_name(S->link.peer_name_a(), S->link.peer_name_b()));
                }
            } else {
                delete s;
            }
        }
    }
    free(buf);
    // A one-player game still put aside with no session for its game: back it comes
    for (int g = 0; g < game_count(); ++g) {
        const char* id = games::get(wl_games[g]).id;
        if (!(S && strcmp(S->id, id) == 0)) {
            restore_1p(id);
            resumed_note_id[0] = 0;            // (not worth a note at start)
        }
    }
}

net::Profile air_profile()
{
    net::Profile p;
    p.name_a = P.name_a;
    p.name_b = P.name_b;
    p.fw = net::Version::parse(shell().firmware_version);
    p.available = P.available;
    p.busy = busy();
    p.move_timer = P.timer;
    for (int g = 0; g < game_count() && p.n_games < net::kMaxGames; ++g) {
        if (!game_on(wl_keys[g])) continue;
        p.games[p.n_games].key = uint8_t(wl_keys[g]);
        p.games[p.n_games].version = uint8_t(version_of_key(wl_keys[g]));
        ++p.n_games;
    }
    return p;
}

// ---- Screens ----------------------------------------------------------------------------------------
enum class Ui {
    None, Main, Games, Players, Player, Version, Asking, Offer, Pick, Connecting, Meet, Name, Words, Timer, Notice,
};
Ui        ui_now = Ui::None;
Ui        before_popup = Ui::None;     // where a request / meeting popped up from
void    (*menu_back)() = nullptr;
net::Mac  who_mac;                     // the player on the Play With / version pages
uint16_t  who_a = 0, who_b = 0;
bool      who_known = false;           // who_mac is set
bool      waiting_pick = false;        // Asking: they said Other Game and pick one now
uint32_t  waiting_since = 0;
char      note[128] = "";
uint32_t  note_until = 0;
char      shown[480] = "";
int       words_list = 0;              // Pick From List: 0 first word, 1 second
int       words_page = 0;
uint16_t  pick_a = 0;                  // the first word picked
char      notice_text[128] = "";

enum Key : intptr_t {
    kBack = 1, kAvailable, kFind, kMyGames, kResume, kAllGames, kStop, kHand, kClear, kName, kTimer,
    kPlay, kNoThanks, kOtherGame, kContinue, kCloseGame, kRandom, kPickList, kPrev, kNext, kOk,
    kGame0 = 100, kPlayer0 = 200, kWord0 = 300, kTimer0 = 900,
};

void set_note(const char* text, uint32_t ms = 10000)
{
    snprintf(note, sizeof note, "%s", text);
    note_until = lv_tick_get() + ms;
}

void on_closed() { ui_now = Ui::None; }

void show(Ui u);

// A version label for a player on the list: "needs update" (their board),
// "later version" (this board needs it), else "" (a version both can use)
const char* version_label(const net::Nearby& b)
{
    if (b.link != net::kLink) return b.link < net::kLink ? "needs update" : "later version";
    return "";
}

int playable_count(const net::Nearby& b)
{
    int n = 0;
    for (int g = 0; g < game_count(); ++g)
        if (game_on(wl_keys[g]) && pres && pres->playable(b, wl_keys[g])) ++n;
    return n;
}

// The list's word for a player
const char* status_of(const net::Nearby& b)
{
    const char* v = version_label(b);
    if (v[0]) return v;
    if (b.busy) return "busy";
    if (b.n_games == 0) return "no games";
    if (playable_count(b)) return "available";
    // Nothing to play: a game both have on but in different versions says who needs the update
    for (int k = 0; k < b.n_games; ++k) {
        const int mine = game_on(b.games[k].key) ? version_of_key(b.games[k].key) : -1;
        if (mine >= 0 && b.games[k].version != mine) return b.games[k].version < mine ? "needs update" : "later version";
    }
    return "available";
}

// Everything a page shows, as one string: rebuild only when it changes
void describe(char* buf, size_t cap)
{
    int n = snprintf(buf, cap, "%d|%u,%u|%d|%04x|%u|%s|%d|%d|%d|", int(ui_now), P.name_a, P.name_b, P.available, P.games,
                     P.timer, note, radio_failed, S ? int(S->link.end_reason()) * 4 + S->over * 2 + S->link.suspended() : -1,
                     waiting_pick);
    if (pres && (ui_now == Ui::Players || ui_now == Ui::Player || ui_now == Ui::Version)) {
        for (int i = 0; i < pres->count() && n < int(cap) - 48; ++i) {
            const net::Nearby& b = pres->at(i);
            n += snprintf(buf + n, cap - n, "%u,%u,%s,%d;", b.name_a, b.name_b, status_of(b), playable_count(b));
        }
    }
}

lv_obj_t* row(int h)
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

void key_cb(lv_event_t* e);

// A key in a row, tall enough for a two-line game title
lv_obj_t* grid_key(lv_obj_t* r, const char* text, intptr_t id, bool on, bool dim)
{
    const int h = lv_obj_get_style_height(r, LV_PART_MAIN);
    lv_obj_t* k = make_key(r, 10, h, key_cb, id);
    lv_obj_set_flex_grow(k, 1);
    lv_obj_t* l = key_label(k, text, menu_font());
    lv_obj_set_style_pad_hor(k, 2, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
    set_checked(k, on);
    if (dim) set_dim(k, true);
    return k;
}

int grid_h() { return 2 * lv_font_get_line_height(menu_font()) + (metrics().large ? 12 : 8); }

// The header's back arrow (no Back key on the page)
void back_arrow() { overlay_back(key_cb, kBack); }

int count_on()
{
    int n = 0;
    for (int g = 0; g < game_count(); ++g) if (game_on(wl_keys[g])) ++n;
    return n;
}

void note_text()
{
    if (note[0]) overlay_text(note, false);
}

const char* timer_text(uint16_t s)
{
    switch (s) {
        case 0:   return "Off";
        case 30:  return "30 Seconds";
        case 60:  return "1 Minute";
        case 120: return "2 Minutes";
        case 300: return "5 Minutes";
        default:  return "?";
    }
}

// A choice between two words, with a switch pointing at the chosen one;
// `name` (optional) leads the row ("Play Mode  1P (o) 2P")
void switch_row(const char* left, const char* right, bool right_on, intptr_t id, const char* name = nullptr)
{
    const int kh = menu_btn_h();
    lv_obj_t* r = row(kh * 3 / 4);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, metrics().large ? 14 : 10, 0);
    const lv_font_t* f = &lv_font_montserrat_14;
    if (name) {
        lv_obj_t* n = lv_label_create(r);
        lv_label_set_text(n, name);
        lv_obj_set_style_text_font(n, f, 0);
        lv_obj_set_style_text_color(n, pal().ink, 0);
        lv_obj_set_style_margin_right(n, metrics().large ? 10 : 6, 0);
    }
    lv_obj_t* l = lv_label_create(r);
    lv_label_set_text(l, left);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, right_on ? pal().muted : pal().ink, 0);
    lv_obj_t* sw = lv_switch_create(r);
    lv_obj_set_size(sw, kh * 3 / 2, kh / 2);
    static const lv_part_t kParts[] = {LV_PART_MAIN, LV_PART_INDICATOR};
    for (lv_part_t part : kParts) {
        // Both sides are a choice, not on/off: the track looks the same
        lv_obj_set_style_bg_color(sw, pal().key_on, part);
        lv_obj_set_style_bg_color(sw, pal().key_on, part | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, part);
    }
    lv_obj_set_style_bg_color(sw, pal().ink, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -2, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sw, 6);
    if (right_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, key_cb, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void*>(id));
    lv_obj_t* rl = lv_label_create(r);
    lv_label_set_text(rl, right);
    lv_obj_set_style_text_font(rl, f, 0);
    lv_obj_set_style_text_color(rl, right_on ? pal().ink : pal().muted, 0);
}

const char* partner()
{
    return S ? show_name(S->link.peer_name_a(), S->link.peer_name_b()) : "";
}

// The Play page: the stylus hand, then 1P / 2P (2P = wireless play: the
// radio listens, others can find this board) and the 2P keys under it
void build_main()
{
    char t[128];
    lv_obj_t* o = overlay_begin("Play", on_closed);
    lv_obj_set_style_pad_row(o, metrics().large ? 8 : 4, 0);    // seven rows and a note on 240 x 320
    switch_row("Left Hand", "Right Hand", right_handed(), kHand);
    switch_row("1P", "2P", P.available, kAvailable, "Play Mode");
    const bool on = P.available && radio_present();
    const char* game = S ? games::get(S->reg).title : "";
    if (busy() && !S->link.suspended() && !S->connecting && app_current_game() != S->reg) {
        snprintf(t, sizeof t, "Resume %s", game);
        overlay_button(overlay(), t, key_cb, kResume, true);
    } else {
        set_dim(overlay_button(overlay(), "Find Players", key_cb, kFind), !on || busy());
    }
    snprintf(t, sizeof t, "Games I'll Play (%d of %d)", count_on(), game_count());
    set_dim(overlay_button(overlay(), t, key_cb, kMyGames), !on);
    snprintf(t, sizeof t, "Name: %s", show_name(P.name_a, P.name_b));
    set_dim(overlay_button(overlay(), t, key_cb, kName), !on);
    snprintf(t, sizeof t, "Move Timer: %s", timer_text(P.timer));
    set_dim(overlay_button(overlay(), t, key_cb, kTimer), !on);
    // The way out of a 2-player game that's stuck (Tom, 2026-10-04): works
    // in 1P too; lit only while there is a session to clear
    set_dim(overlay_button(overlay(), "Clear 2P Sessions", key_cb, kClear), !S);
    if (note[0]) note_text();
    else if (!radio_present()) overlay_text("This board has no radio for 2-player play.", true);
    else if (radio_failed) overlay_text("The radio couldn't start: the board is low on memory.", false);
    else if (live() && S->link.suspended()) {
        snprintf(t, sizeof t, "%s with %s goes on when you meet again.", game, partner());
        overlay_text(t, true);
    } else if (busy()) {
        snprintf(t, sizeof t, "You're playing %s with %s.", game, partner());
        overlay_text(t, true);
    } else if (!P.available) {
        overlay_text("2P: play with boards nearby.", true);
    }
    back_arrow();
}

void build_games()
{
    overlay_begin("Games I'll Play", on_closed);
    overlay_text("Lit games are the ones others can ask you to play.", true);
    const int h = grid_h();
    const int n = game_count() + 1;                 // + All Games
    const uint16_t all = uint16_t((1u << 16) - 1);
    bool every = true;
    for (int g = 0; g < game_count(); ++g) every &= game_on(wl_keys[g]);
    for (int k = 0; k < n; k += 2) {
        lv_obj_t* r = row(h);
        for (int j = k; j < k + 2 && j < n; ++j) {
            if (j < game_count()) grid_key(r, games::get(wl_games[j]).title, kGame0 + j, game_on(wl_keys[j]), false);
            else grid_key(r, "All Games", kAllGames, every, false);
        }
    }
    (void)all;
    back_arrow();
}

void build_players()
{
    char t[64];
    overlay_begin("Find Players", on_closed);
    const int kh = menu_btn_h();
    int rows = 0;
    const int max_rows = 5;
    const lv_font_t* nf = menu_font();
    const lv_font_t* sf = &lv_font_montserrat_14;
    for (int i = 0; pres && i < pres->count() && rows < max_rows; ++i) {
        const net::Nearby& b = pres->at(i);
        const char* st = status_of(b);
        const bool lit = !strcmp(st, "available");
        const char* who = show_name(b.name_a, b.name_b);
        lv_obj_t* k = make_key(overlay(), lv_pct(100), kh, key_cb, kPlayer0 + i);
        // "available" becomes "free" when the row is too narrow for it
        const int room = metrics().w - 2 * (metrics().large ? 16 : 10) - 24;
        snprintf(t, sizeof t, "%s", st);
        if (lit && text_width(who, nf) + text_width(t, sf) + 14 > room) snprintf(t, sizeof t, "free");
        lv_obj_t* l = lv_label_create(k);
        lv_label_set_text(l, who);
        lv_obj_set_style_text_font(l, nf, 0);
        lv_obj_set_style_text_color(l, lit ? pal().ink : pal().key_dim_text, 0);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
        lv_obj_t* d = lv_label_create(k);
        lv_label_set_text(d, t);
        lv_obj_set_style_text_font(d, sf, 0);
        lv_obj_set_style_text_color(d, pal().muted, 0);
        lv_obj_align(d, LV_ALIGN_RIGHT_MID, -8, 0);
        if (!lit) set_dim(k, true);
        ++rows;
    }
    overlay_text("Searching...", rows > 0);
    if (note[0]) note_text();
    else if (!rows) overlay_text("Players show up here when their Play Mode is 2P.", true);
    back_arrow();
}

const net::Nearby* who()
{
    return pres && who_known ? pres->find(who_mac) : nullptr;
}

void build_version()
{
    char t[200];
    const char* name = show_name(who_a, who_b);
    overlay_begin(name, on_closed);
    const net::Nearby* b = who();
    const bool theirs = b ? !strcmp(status_of(*b), "needs update") : true;
    if (theirs)
        snprintf(t, sizeof t, "%s's board needs an update before you can play together. Update it at %s", name, kFlasher);
    else
        snprintf(t, sizeof t, "Your board needs an update before you can play with %s. Update it at %s", name, kFlasher);
    overlay_text(t, false);
    back_arrow();
}

// Play With Bob: every wireless game; lit = both will play it (same version)
void build_player()
{
    char t[128];
    const char* name = show_name(who_a, who_b);
    overlay_begin(name, on_closed);              // (the player's name is the title: "Play With ..." won't fit)
    const net::Nearby* b = who();
    const bool partner_now = S && who_mac == S->link.peer();
    if (!b && !partner_now) {
        snprintf(t, sizeof t, "Looking for %s...", name);
        overlay_text(t, false);
    }
    const int h = grid_h();
    lv_obj_t* r = nullptr;
    int lit = 0;
    for (int g = 0; g < game_count(); ++g) {
        const int key = wl_keys[g];
        bool ok = game_on(key);
        bool update = false;
        if (b) {
            ok = ok && pres->playable(*b, key);
            update = game_on(key) && b->version_of(key) >= 0 && b->version_of(key) != version_of_key(key);
        }
        if (g % 2 == 0) r = row(h);
        lv_obj_t* k = grid_key(r, games::get(wl_games[g]).title, kGame0 + g, false, !ok);
        if (update) {
            // The game's title, and under it in small letters why it's grey
            lv_obj_t* l = lv_obj_get_child(k, 0);
            lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_height(l, lv_font_get_line_height(menu_font()));
            lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 2);
            lv_obj_t* u = lv_label_create(k);
            lv_label_set_text(u, "needs update");
            lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(u, pal().key_dim_text, 0);
            lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -2);
        }
        lit += ok;
    }
    if (note[0]) note_text();
    else if (b && b->n_games == 0) { snprintf(t, sizeof t, "%s hasn't picked any games to play.", name); overlay_text(t, true); }
    else if (b && b->busy) { snprintf(t, sizeof t, "%s is playing a game now.", name); overlay_text(t, true); }
    else if (lit) { snprintf(t, sizeof t, "Tap a game to ask %s to play it.", name); overlay_text(t, true); }
    back_arrow();
}

// Other Game: this board picks one of the asker's games
net::GameOffer pick_games[net::kMaxGames];
int pick_n = 0;
uint16_t pick_from_a = 0, pick_from_b = 0;
net::Mac pick_from;

bool pick_ok(int key)
{
    if (!game_on(key)) return false;
    for (int k = 0; k < pick_n; ++k)
        if (pick_games[k].key == key) return pick_games[k].version == version_of_key(key);
    return false;
}

void build_pick()
{
    char t[96];
    const char* name = show_name(pick_from_a, pick_from_b);
    overlay_begin(name, on_closed);
    const int h = grid_h();
    lv_obj_t* r = nullptr;
    for (int g = 0; g < game_count(); ++g) {
        if (g % 2 == 0) r = row(h);
        grid_key(r, games::get(wl_games[g]).title, kGame0 + g, false, !pick_ok(wl_keys[g]));
    }
    snprintf(t, sizeof t, "Pick the game you'd rather play with %s.", name);
    overlay_text(t, true);
    back_arrow();
}

void build_asking()
{
    char t[128];
    const char* name = show_name(who_a, who_b);
    overlay_begin(name, on_closed);
    if (waiting_pick) {
        snprintf(t, sizeof t, "%s would rather play a different game. Waiting for %s to pick one...", name, name);
        overlay_text(t, false);
        overlay_button(overlay(), "Stop Waiting", key_cb, kStop);
    } else {
        overlay_text("Requesting...", false);
        snprintf(t, sizeof t, "Asking %s to play %s.", name, title_of_key(pres ? pres->request_key() : -1));
        overlay_text(t, true);
        overlay_button(overlay(), "Stop Asking", key_cb, kStop);
    }
    back_arrow();
}

void build_offer()
{
    char t[128];
    const char* game = title_of_key(pres->asked_key());
    snprintf(t, sizeof t, "Play %s?", game);
    overlay_begin(t, on_closed);
    snprintf(t, sizeof t, "%s would like to play %s.", show_name(pres->asker_name_a(), pres->asker_name_b()), game);
    overlay_text(t, false);
    // The shorter Move Timer applies: say so only when it isn't this player's
    const uint16_t theirs = pres->asker_timer();
    const uint16_t agreed = !P.timer ? theirs : !theirs ? P.timer : (P.timer < theirs ? P.timer : theirs);
    if (agreed != P.timer) {
        snprintf(t, sizeof t, "Move Timer: %s.", timer_text(agreed));
        overlay_text(t, true);
    }
    overlay_button(overlay(), "Play", key_cb, kPlay, true);
    overlay_pair("No Thanks", key_cb, kNoThanks, "Other Game", key_cb, kOtherGame);
    back_arrow();
}

void build_connecting()
{
    char t[64];
    snprintf(t, sizeof t, "Play %s", S ? games::get(S->reg).title : "");
    overlay_begin(t, on_closed);
    overlay_text("Connecting...", false);
    back_arrow();
}

void build_meet()
{
    char t[128];
    const char* game = S ? games::get(S->reg).title : "";
    snprintf(t, sizeof t, "Continue %s?", game);
    overlay_begin(t, on_closed);
    if (S && S->link.continue_said()) {
        snprintf(t, sizeof t, "Waiting for %s...", partner());
        overlay_text(t, false);
        overlay_button(overlay(), "Close Game", key_cb, kCloseGame);
    } else {
        snprintf(t, sizeof t, "%s is back in range. Continue %s?", partner(), game);
        overlay_text(t, false);
        overlay_button(overlay(), "Continue", key_cb, kContinue, true);
        overlay_button(overlay(), "Close Game", key_cb, kCloseGame);
    }
    back_arrow();
}

void build_name()
{
    overlay_begin("Your Name", on_closed);
    lv_obj_t* l = overlay_text(show_name(P.name_a, P.name_b), false);
    lv_obj_set_style_text_font(l, title_font(), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    overlay_button(overlay(), "Random", key_cb, kRandom, true);
    overlay_button(overlay(), "Pick From List", key_cb, kPickList);
    overlay_text("Players nearby see this name. Pick From List: a word from each list.", true);
    back_arrow();
}

// The words of a list in alphabetical order (their numbers stay as they are)
int sorted_words(int list, uint16_t* out, int cap)
{
    const int n = list == 0 ? names::first_count() : names::second_count();
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = uint16_t(i);
    auto word = [list](uint16_t i) { return list == 0 ? names::first(i) : names::second(i); };
    for (int i = 1; i < k; ++i)                          // insertion sort: ~100 words, once per page
        for (int j = i; j > 0 && strcmp(word(out[j - 1]), word(out[j])) > 0; --j) {
            const uint16_t x = out[j]; out[j] = out[j - 1]; out[j - 1] = x;
        }
    return k;
}

void build_words()
{
    overlay_begin(words_list == 0 ? "First Word" : "Second Word", on_closed);
    const Metrics& m = metrics();
    const int kh = menu_btn_h();
    const int gap = m.large ? 8 : 6;
    // Three words a row when the longest fits a third of the width, else two
    int widest = 0;
    const int nw = words_list == 0 ? names::first_count() : names::second_count();
    for (int i = 0; i < nw; ++i) {
        const int w = text_width(words_list == 0 ? names::first(i) : names::second(i), menu_font());
        if (w > widest) widest = w;
    }
    const int inner = m.w - 2 * (m.large ? 16 : 10);
    const int cols = (inner - 2 * gap) / 3 >= widest + 8 ? 3 : 2;
    const int rows = (m.h - 2 * (m.large ? 16 : 10) + gap) / (kh + gap) - 1;   // (the last row: < page >)
    const int per = cols * (rows > 1 ? rows : 1);
    uint16_t* order = static_cast<uint16_t*>(malloc(256 * sizeof(uint16_t)));
    if (!order) { back_arrow(); return; }
    const int n = sorted_words(words_list, order, 256);
    const int pages = (n + per - 1) / per;
    if (words_page >= pages) words_page = pages - 1;
    if (words_page < 0) words_page = 0;
    const uint16_t current = words_list == 0 ? P.name_a : P.name_b;
    lv_obj_t* r = nullptr;
    for (int k = 0; k < per; ++k) {
        const int i = words_page * per + k;
        if (k % cols == 0) r = row(kh);
        if (i >= n) {                                      // keep the grid's shape on the last page
            lv_obj_t* sp = lv_obj_create(r);
            lv_obj_remove_style_all(sp);
            lv_obj_set_flex_grow(sp, 1);
            continue;
        }
        const char* w = words_list == 0 ? names::first(order[i]) : names::second(order[i]);
        lv_obj_t* key = make_key(r, 10, kh, key_cb, kWord0 + order[i]);
        lv_obj_set_flex_grow(key, 1);
        lv_obj_set_style_pad_hor(key, 1, 0);
        key_label(key, w, menu_font());
        set_checked(key, order[i] == current);
    }
    free(order);
    // < page > along the bottom
    lv_obj_t* nav = row(kh);
    lv_obj_t* prev = make_key(nav, 10, kh, key_cb, kPrev);
    lv_obj_set_flex_grow(prev, 1);
    key_label(prev, LV_SYMBOL_LEFT, menu_font());
    set_dim(prev, words_page == 0);
    char t[16];
    snprintf(t, sizeof t, "%d / %d", words_page + 1, pages);
    lv_obj_t* pl = lv_label_create(nav);
    lv_label_set_text(pl, t);
    lv_obj_set_style_text_font(pl, menu_font(), 0);
    lv_obj_set_style_text_color(pl, pal().muted, 0);
    lv_obj_t* next = make_key(nav, 10, kh, key_cb, kNext);
    lv_obj_set_flex_grow(next, 1);
    key_label(next, LV_SYMBOL_RIGHT, menu_font());
    set_dim(next, words_page >= pages - 1);
    back_arrow();
}

void build_timer()
{
    overlay_begin("Move Timer", on_closed);
    overlay_text("The time each player has for a move in a 2-player game. "
                 "When two timers differ, the shorter one is used.", true);
    for (size_t k = 0; k < sizeof kTimers / sizeof kTimers[0]; ++k) {
        lv_obj_t* b = overlay_button(overlay(), timer_text(kTimers[k]), key_cb, kTimer0 + intptr_t(k));
        set_checked(b, kTimers[k] == P.timer);
    }
    back_arrow();
}

// A message for a player who isn't on the Play pages (e.g. a request that
// went away while they were in a game)
void build_notice()
{
    overlay_begin("Play", on_closed);
    overlay_text(notice_text, false);
    overlay_button(overlay(), "OK", key_cb, kOk, true);
    back_arrow();
}

bool play_page(Ui u)
{
    return u == Ui::Main || u == Ui::Games || u == Ui::Players || u == Ui::Player || u == Ui::Version
        || u == Ui::Asking || u == Ui::Name || u == Ui::Words || u == Ui::Timer || u == Ui::Pick;
}

void show(Ui u)
{
    const Ui was = ui_now;
    switch (u) {
        case Ui::Main:       build_main(); break;
        case Ui::Games:      build_games(); break;
        case Ui::Players:    build_players(); break;
        case Ui::Player:     build_player(); break;
        case Ui::Version:    build_version(); break;
        case Ui::Asking:     build_asking(); break;
        case Ui::Offer:      build_offer(); break;
        case Ui::Pick:       build_pick(); break;
        case Ui::Connecting: build_connecting(); break;
        case Ui::Meet:       build_meet(); break;
        case Ui::Name:       build_name(); break;
        case Ui::Words:      build_words(); break;
        case Ui::Timer:      build_timer(); break;
        case Ui::Notice:     build_notice(); break;
        case Ui::None:       if (was != Ui::None) close_overlays(); ui_now = Ui::None; return;
    }
    ui_now = u;
    describe(shown, sizeof shown);
}

// A message: on the Play pages as a note, else in its own little page
void tell(const char* text, Ui back_to)
{
    if (play_page(back_to)) {
        set_note(text);
        show(back_to);
    } else {
        snprintf(notice_text, sizeof notice_text, "%s", text);
        before_popup = Ui::None;
        show(Ui::Notice);
    }
}

void set_available(bool on)
{
    if (P.available == on) return;
    P.available = on;
    save_profile();
    log_event("Wireless: 2P %s", on ? "on" : "off");
}

uint32_t new_session_id(uint32_t now)
{
    const uint32_t s = shell().random_seed ? shell().random_seed() : now;
    return s ? s : 1;
}

void ask(const net::Mac& to, uint16_t a, uint16_t b, int key, uint32_t now)
{
    who_mac = to;
    who_a = a;
    who_b = b;
    who_known = true;
    waiting_pick = false;
    note[0] = 0;
    pres->request(to, a, b, key, new_session_id(now), now);
    show(Ui::Asking);
}

// Drop the session: a forfeit when it's going with the partner there, else unrecorded
void clear_session(uint32_t now)
{
    if (!S) return;
    if (S->link.ended()) { drop_session(); return; }
    const bool open = app_current_game() == S->reg;
    if (!S->over && !S->link.suspended() && !S->connecting && S->link.up(now)) {
        S->link.forfeit(now);                  // the game records the loss as it closes
        log_event("Wireless: %s forfeited (Clear 2P Sessions)", S->id);
    } else {
        S->link.leave(now);
        log_event("Wireless: %s cleared", S->id);
    }
    if (!open) S->noted = true;
    save_session();
    if (open) app_go_home_now();
    else restore_1p(S->id);
}

void key_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const uint32_t now = lv_tick_get();
    switch (id) {
        case kBack:
            switch (ui_now) {
                case Ui::Games: case Ui::Players: case Ui::Name: case Ui::Timer:
                    note[0] = 0; show(Ui::Main); return;
                case Ui::Player: case Ui::Version:
                    note[0] = 0;
                    show(S && who_mac == S->link.peer() && S->over ? Ui::None : Ui::Players);
                    return;
                case Ui::Words: show(Ui::Name); return;
                case Ui::Asking:                         // = Stop Asking
                    if (pres) pres->cancel(now);
                    waiting_pick = false;
                    show(Ui::Player);
                    return;
                case Ui::Offer:                          // = No Thanks
                    if (pres && pres->asked()) pres->decline(net::Answer::NoThanks, now);
                    show(before_popup);
                    return;
                case Ui::Pick: show(before_popup); return;
                case Ui::Connecting:                     // give up
                    if (S && S->connecting) { S->link.leave(now); S->noted = true; drop_session(); }
                    show(Ui::Main);
                    return;
                case Ui::Meet: show(before_popup); return;
                case Ui::Notice: show(Ui::None); return;
                default: {
                    void (*back)() = menu_back;
                    menu_back = nullptr;
                    note[0] = 0;
                    show(Ui::None);
                    if (back) back();
                    return;
                }
            }
        case kOk: show(Ui::None); return;
        case kHand:
            settings().left_handed = !settings().left_handed;
            save_settings();
            app_theme_changed();                 // the open game lays itself out again
            show(Ui::Main);
            return;
        case kClear:
            if (S) {
                clear_session(now);
                set_note("2P sessions cleared.", 6000);
            }
            show(Ui::Main);
            return;
        case kName: if (!P.available) return; note[0] = 0; show(Ui::Name); return;
        case kTimer: if (!P.available) return; note[0] = 0; show(Ui::Timer); return;
        case kRandom: {
            uint16_t a = P.name_a, b = P.name_b;
            for (int k = 0; k < 8 && a == P.name_a && b == P.name_b; ++k)
                names::random_pair(new_session_id(now) + uint32_t(k), &a, &b);
            P.name_a = a;
            P.name_b = b;
            save_profile();
            show(Ui::Name);
            return;
        }
        case kPickList: words_list = 0; words_page = 0; show(Ui::Words); return;
        case kPrev: if (words_page > 0) --words_page; show(Ui::Words); return;
        case kNext: ++words_page; show(Ui::Words); return;
        case kAvailable:
            note[0] = 0;
            set_available(!P.available);
            show(Ui::Main);
            return;
        case kFind:
            if (!P.available || busy()) return;
            note[0] = 0;
            show(Ui::Players);
            return;
        case kMyGames: if (!P.available) return; note[0] = 0; show(Ui::Games); return;
        case kResume:
            if (S) {
                show(Ui::None);
                if (app_current_game() != S->reg) app_open_game(S->reg);
            }
            return;
        case kAllGames: {
            bool every = true;
            for (int g = 0; g < game_count(); ++g) every &= game_on(wl_keys[g]);
            P.games = every ? 0 : 0xFFFF;
            save_profile();
            show(Ui::Games);
            return;
        }
        case kStop:
            if (pres) pres->cancel(now);
            waiting_pick = false;
            show(Ui::Player);
            return;
        case kPlay:
            if (pres && pres->asked()) pres->accept(now);   // tick() starts the session
            tick(now);
            return;
        case kNoThanks:
            if (pres && pres->asked()) pres->decline(net::Answer::NoThanks, now);
            note[0] = 0;
            show(before_popup);
            return;
        case kOtherGame:
            if (pres && pres->asked()) {
                pick_n = pres->asker_games(pick_games, net::kMaxGames);
                pick_from = pres->asker();
                pick_from_a = pres->asker_name_a();
                pick_from_b = pres->asker_name_b();
                pres->decline(net::Answer::OtherGame, now);
                show(Ui::Pick);
            }
            return;
        case kContinue:
            if (S && live()) S->link.agree_continue(now);
            show(Ui::Meet);
            return;
        case kCloseGame:
            if (S && live()) {
                S->link.close(now);
                S->noted = true;
                save_session();
                log_event("Wireless: %s closed after losing touch", S->id);
                if (app_current_game() == S->reg) app_go_home_now();
                else restore_1p(S->id);
            }
            set_note("Communications failed. Game not counted.");
            show(Ui::Main);
            return;
        default: break;
    }
    if (id >= kTimer0 && id < kTimer0 + intptr_t(sizeof kTimers / sizeof kTimers[0])) {
        P.timer = kTimers[id - kTimer0];
        save_profile();
        show(Ui::Main);
        return;
    }
    if (id >= kWord0 && id < kWord0 + 600) {
        const uint16_t w = uint16_t(id - kWord0);
        if (words_list == 0) {
            pick_a = w;
            words_list = 1;
            words_page = 0;
            show(Ui::Words);
        } else {
            P.name_a = pick_a;
            P.name_b = w;
            save_profile();
            show(Ui::Name);
        }
        return;
    }
    if (id >= kPlayer0 && id < kPlayer0 + net::Presence::kMaxNearby && pres) {
        const int i = int(id - kPlayer0);
        if (i < pres->count()) {
            const net::Nearby& b = pres->at(i);
            who_mac = b.mac;
            who_a = b.name_a;
            who_b = b.name_b;
            who_known = true;
            note[0] = 0;
            const char* st = status_of(b);
            if (!strcmp(st, "needs update") || !strcmp(st, "later version")) show(Ui::Version);
            else show(Ui::Player);
        }
        return;
    }
    if (id >= kGame0 && id < kGame0 + net::kMaxGames) {
        const int g = int(id - kGame0);
        const int key = wl_keys[g];
        if (ui_now == Ui::Games) {
            P.games ^= uint16_t(1u << (key - 1));
            save_profile();
            show(Ui::Games);
        } else if (ui_now == Ui::Player && pres && !busy()) {
            const net::Nearby* b = who();
            const bool ok = b ? pres->playable(*b, key) : game_on(key);
            if (ok) ask(who_mac, who_a, who_b, key, now);
        } else if (ui_now == Ui::Pick && pres && pick_ok(key)) {
            ask(pick_from, pick_from_a, pick_from_b, key, now);
        }
    }
}

// ---- Events ------------------------------------------------------------------------------------
void start_session(uint32_t now)
{
    const int g = index_of_key(pres->game_key());
    if (g < 0) return;
    if (S && !S->link.ended()) {
        // A finished game still open for a rematch: no more games there
        S->link.done(now);
        S->noted = true;
    }
    if (S) drop_session();          // (its game's one-player game comes back when that game closes)
    stash_1p(wl_games[g]);
    S = new (std::nothrow) Session();
    if (!S) return;
    S->reg = wl_games[g];
    snprintf(S->id, sizeof S->id, "%s", games::get(S->reg).id);
    S->link.begin(my_mac(), pres->partner(), pres->partner_name_a(), pres->partner_name_b(), pres->session(),
                  pres->inviter(), pres->game_key(), pres->timer(), the_air, now);
    S->connecting = true;
    S->connect_since = now;
    save_session();
    log_event("Wireless: %s with %s", S->id, partner());
    note[0] = 0;
    waiting_pick = false;
    menu_back = nullptr;
    show(Ui::Connecting);
}

// Both boards hear each other: a trill, and the game opens
void connected()
{
    S->connecting = false;
    S->fresh = true;
    sound(Sound::Trill);
    show(Ui::None);
    close_overlays();
    if (app_current_game() != S->reg) app_open_game(S->reg);
}

// Why the request popup went away without an answer: 1 cancelled, 2 the asker out of range
int offer_end = 0;

void handle_events(uint32_t now)
{
    char t[128];
    const char* name = show_name(who_a, who_b);
    const Ui back_to = ui_now == Ui::Asking ? Ui::Player : ui_now;
    switch (pres->poll()) {
        case net::Presence::Event::Started:
            start_session(now);
            return;
        case net::Presence::Event::Answered:
            switch (pres->answer()) {
                case net::Answer::NoThanks:  snprintf(t, sizeof t, "%s said 'no thanks'.", name); break;
                case net::Answer::Busy:      snprintf(t, sizeof t, "%s can't play right now.", name); break;
                case net::Answer::GameOff:   snprintf(t, sizeof t, "That game is no longer available."); break;
                case net::Answer::OtherGame:
                    // They pick a game and ask back: wait here for it
                    waiting_pick = true;
                    waiting_since = now;
                    if (ui_now == Ui::Asking) show(Ui::Asking);
                    return;
                default: return;
            }
            sound(Sound::Turn);
            if (ui_now == Ui::Asking || play_page(ui_now)) tell(t, back_to);
            return;
        case net::Presence::Event::NoAnswer:
            snprintf(t, sizeof t, "There was no answer from %s.", name);
            if (ui_now == Ui::Asking || play_page(ui_now)) tell(t, back_to);
            return;
        case net::Presence::Event::Gone:
            snprintf(t, sizeof t, "%s went out of range.", name);
            if (ui_now == Ui::Asking || play_page(ui_now)) tell(t, back_to);
            return;
        case net::Presence::Event::Cancelled:
            offer_end = 1;                       // watch_offer() tells the player
            return;
        case net::Presence::Event::AskerGone:
            offer_end = 2;
            return;
        case net::Presence::Event::None:
            break;
    }
    // Waiting for the other player to pick a game after Other Game
    if (waiting_pick && now - waiting_since > net::kRequestMs) {
        waiting_pick = false;
        snprintf(t, sizeof t, "There was no answer from %s.", name);
        if (ui_now == Ui::Asking) tell(t, Ui::Player);
    }
}

// The request popup: shown while asked; when the question goes away
// (cancelled, the asker out of range) the player is told
uint16_t offer_a = 0, offer_b = 0;
bool     offer_up = false;
uint32_t offer_since = 0;

void watch_offer(uint32_t now)
{
    char t[128];
    if (pres->asked() && ui_now != Ui::Offer && ui_now != Ui::Connecting && ui_now != Ui::Meet && ui_now != Ui::Pick) {
        before_popup = play_page(ui_now) ? ui_now : Ui::None;
        offer_a = pres->asker_name_a();
        offer_b = pres->asker_name_b();
        offer_up = true;
        offer_since = now;
        offer_end = 0;
        sound(Sound::Call);
        show(Ui::Offer);
    } else if (!pres->asked() && ui_now == Ui::Offer && offer_up) {
        offer_up = false;
        // Not answered here: the asker stopped asking (or gave up waiting), or went away
        const char* n = show_name(offer_a, offer_b);
        if (offer_end == 2)                                   snprintf(t, sizeof t, "%s went out of range.", n);
        else if (now - offer_since > net::kRequestMs - 2000)  snprintf(t, sizeof t, "%s stopped waiting for an answer.", n);
        else                                                  snprintf(t, sizeof t, "%s cancelled the request.", n);
        offer_end = 0;
        tell(t, before_popup);
    }
    if (ui_now != Ui::Offer) offer_up = false;
}

void radio_power(bool want, uint32_t now)
{
    const Shell& H = shell();
    if (want && !radio_on) {
        if (!radio_present() || int32_t(now - radio_retry_ms) < 0) return;
        if (!H.radio_on()) {
            radio_failed = true;
            radio_retry_ms = now + 5000;
            log_event("Wireless: the radio could not start");
            return;
        }
        radio_failed = false;
        radio_on = true;
        dozing = false;
        uint32_t fr = 0, big = 0;
        if (H.memory) H.memory(&fr, &big);
        log_event("Wireless: radio on in %lu ms, %lu KB free", (unsigned long)(H.radio_start_ms ? H.radio_start_ms() : 0),
                  (unsigned long)(fr / 1024));
        if (!pres) pres = new (std::nothrow) net::Presence();
        if (pres) pres->begin(my_mac(), the_air, now);
    } else if (!want && radio_on) {
        H.radio_off();
        radio_on = false;
        dozing = false;
        delete pres;
        pres = nullptr;
        log_step("Wireless: radio off");
    }
}

// Session housekeeping every tick
void session_tick(uint32_t now)
{
    if (!S) return;
    if (S->connecting) {
        if (S->link.heard() && !S->link.ended()) { connected(); return; }
        if (S->link.ended() || now - S->connect_since > kConnectMs) {
            char t[96];
            snprintf(t, sizeof t, "Couldn't connect to %s.", partner());
            S->link.leave(now);
            S->noted = true;
            drop_session();
            tell(t, Ui::Main);
            return;
        }
    }
    // Back in range after one of the boards put it away: both players are asked
    if (live() && S->link.meet() && ui_now != Ui::Meet && ui_now != Ui::Offer && ui_now != Ui::Connecting) {
        before_popup = play_page(ui_now) ? ui_now : Ui::None;
        sound(Sound::Call);
        show(Ui::Meet);
    }
    if (live() && S->link.resumed()) {
        save_session();
        sound(Sound::Trill);
        if (ui_now == Ui::Meet) show(Ui::None);
        if (app_current_game() != S->reg) { close_overlays(); app_open_game(S->reg); }
    }
    // It ended while its game wasn't open (a put-away session): record it here
    if (S->link.ended() && !S->noted && app_current_game() != S->reg) {
        const net::Link::End why = S->link.end_reason();
        record_unseen_end();
        char t[128];
        t[0] = 0;
        const char* game = games::get(S->reg).title;
        if (why == net::Link::End::PeerForfeited)
            snprintf(t, sizeof t, "%s left and forfeited %s. You win!", partner(), game);
        else if (why == net::Link::End::PeerClosed || why == net::Link::End::PeerGone)
            snprintf(t, sizeof t, "Communications failed. Game not counted.");
        if (ui_now == Ui::Meet) {
            if (t[0]) tell(t, before_popup); else show(before_popup);
        } else if (t[0]) {
            tell(t, play_page(ui_now) ? ui_now : Ui::None);
        }
        restore_1p(S->id);
    }
    if (ui_now == Ui::Meet && S && !live()) show(before_popup);
    // An ended session goes once the game took it in and the ending was said
    if (S && S->link.ended() && S->noted && S->link.linger_over(now)) drop_session();
}

} // namespace

// ---- Public ---------------------------------------------------------------------------------------
int game_count() { collect(); return wl_n; }
int game_registry(int g) { collect(); return g >= 0 && g < wl_n ? wl_games[g] : -1; }

int game_of(const char* id)
{
    collect();
    for (int g = 0; g < wl_n; ++g) if (strcmp(games::get(wl_games[g]).id, id) == 0) return g;
    return -1;
}

bool radio_present()
{
    const Shell& H = shell();
    return H.radio_on && H.radio_off && H.radio_send && H.radio_recv && H.radio_mac;
}

bool available() { return P.available; }
uint16_t move_timer() { return P.timer; }
const char* my_name() { return show_name(P.name_a, P.name_b); }

void tick(uint32_t now)
{
    the_air.send = air_send;
    load_all(now);
    radio_power(P.available || S != nullptr, now);
    if (radio_on) {
        uint8_t buf[net::kPacketMax];
        net::Mac from;
        for (int k = 0; k < 16; ++k) {
            int8_t rssi = -100;
            const size_t n = shell().radio_recv(from.b, buf, sizeof buf, &rssi);
            if (!n) break;
            heard_signal(from, rssi, now);
            const uint32_t s = net::status_session(buf, n);
            if (S && s && s == S->link.session()) { S->link.receive(from, buf, n, now); continue; }
            // A session that ended here earlier: say how it ended
            bool tomb = false;
            for (const Tomb& t : tombs)
                if (s && t.session == s && t.peer == from) {
                    // (flags sit at byte 18 of a status: an ending isn't answered)
                    const uint16_t flags = uint16_t(buf[18] | buf[19] << 8);
                    if (!(flags & net::kFlagEnds)) net::send_end(the_air, from, s, t.flags);
                    tomb = true;
                }
            if (!tomb && pres) pres->receive(from, buf, n, now);
        }
        if (S) S->link.tick(now);
        if (pres) {
            pres->set_profile(air_profile(), now);
            pres->search(ui_now == Ui::Players || ui_now == Ui::Player || ui_now == Ui::Version || ui_now == Ui::Asking,
                         now);
            pres->tick(now);
            handle_events(now);
            watch_offer(now);
        }
        // Battery (Tom, 2026-10-04): the radio dozes unless something is going on
        const bool awake = !pres || pres->searching() || pres->requesting() || pres->asked()
                        || pres->called_lately(now) || ui_now == Ui::Offer || ui_now == Ui::Meet
                        || ui_now == Ui::Connecting || ui_now == Ui::Pick
                        || (S && (S->connecting || (S->link.ended() ? !S->link.linger_over(now)
                                  : !S->link.suspended() || S->link.bursting(now) || S->link.meet()
                                    || S->link.continue_said())));
        if (awake == dozing) {
            dozing = !awake;
            if (shell().radio_doze) shell().radio_doze(dozing);
            log_step("Wireless: radio %s", dozing ? "dozing" : "awake");
        }
    }
    session_tick(now);
    if (note[0] && int32_t(now - note_until) >= 0) note[0] = 0;
    // Pages follow what they show
    if (play_page(ui_now) && overlay_open()) {
        char d[sizeof shown];
        describe(d, sizeof d);
        if (strcmp(d, shown) != 0) show(ui_now);
    } else if (ui_now == Ui::Meet && overlay_open()) {
        char d[sizeof shown];
        describe(d, sizeof d);
        if (strcmp(d, shown) != 0) show(Ui::Meet);
    }
}

net::Link* session_for(const char* id) { return S && !S->connecting && strcmp(S->id, id) == 0 ? &S->link : nullptr; }

bool take_start(const char* id)
{
    if (!S || !S->fresh || strcmp(S->id, id) != 0) return false;
    S->fresh = false;
    return true;
}

void session_save() { if (S) save_session(); }

void session_over(bool over)
{
    if (!S || S->over == over) return;
    S->over = over;
    save_session();
}

void clear_sessions() { clear_session(lv_tick_get()); }

void session_finished()
{
    if (!S) return;
    S->noted = true;
    save_session();
}

void suspend_session()
{
    if (!live()) return;
    char t[128];
    snprintf(t, sizeof t, "%s with %s is saved. It goes on when you meet again.", games::get(S->reg).title,
             partner());
    S->link.suspend(lv_tick_get());
    save_session();
    log_event("Wireless: %s put away (no reply)", S->id);
    back_after_game(t);
}

const char* partner_name() { return partner(); }

void new_game_with_partner()
{
    if (!S) return;
    who_mac = S->link.peer();
    who_a = S->link.peer_name_a();
    who_b = S->link.peer_name_b();
    who_known = true;
    note[0] = 0;
    menu_back = nullptr;
    show(Ui::Player);
}

bool take_resumed_note(const char* id)
{
    if (!resumed_note_id[0] || strcmp(resumed_note_id, id) != 0) return false;
    resumed_note_id[0] = 0;
    return true;
}

void game_closed(const char* id)
{
    // The session's game closed after the session ended: its one-player game comes back
    if (S && strcmp(S->id, id) == 0 && !S->link.ended()) return;
    restore_1p(id);
}

namespace {
char after_note[128] = "";
void after_game_async(void*)
{
    app_go_home_now();
    menu_back = nullptr;
    if (after_note[0]) set_note(after_note, 12000);
    show(Ui::Main);
}
} // namespace

void back_after_game(const char* text)
{
    snprintf(after_note, sizeof after_note, "%s", text ? text : "");
    lv_async_call(after_game_async, nullptr);
}

void open_menu(void (*back)())
{
    load_all(lv_tick_get());
    menu_back = back;
    note[0] = 0;
    show(Ui::Main);
}

int wifi_level()
{
    if (!radio_on || (!P.available && !S)) return -1;           // 1P: off
    if (lv_tick_get() - sig_ms > 4000) return 0;                // nobody heard: the dot
    if (sig_rssi >= -60) return 3;
    if (sig_rssi >= -70) return 2;
    if (sig_rssi >= -80) return 1;
    return 0;
}

int two_player_state() { return busy() ? 1 : 0; }

const char* radio_state()
{
    if (!radio_present()) return "None";
    if (!radio_on) return "Off (1P)";
    return dozing ? "Dozing (2P)" : "Listening";
}

void resume_session()
{
    if (busy() && !S->link.suspended() && !S->connecting && app_current_game() != S->reg) {
        show(Ui::None);
        close_overlays();
        app_open_game(S->reg);
        return;
    }
    open_menu(nullptr);
}

void set_two_player(bool on) { load_all(lv_tick_get()); set_available(on); }

void set_name(uint16_t a, uint16_t b)
{
    load_all(lv_tick_get());
    P.name_a = a;
    P.name_b = b;
    save_profile();
}

void set_move_timer(uint16_t seconds)
{
    load_all(lv_tick_get());
    P.timer = seconds;
    save_profile();
}

void debug_state(char* buf, size_t cap)
{
    int n = snprintf(buf, cap, "ui=%d S=%d live=%d over=%d end=%d susp=%d conn=%d avail=%d radio=%d", int(ui_now),
                     S != nullptr, live(), S ? S->over : -1, S ? int(S->link.end_reason()) : -1,
                     S ? S->link.suspended() : -1, S ? S->connecting : -1, P.available, radio_on);
    if (pres) {
        n += snprintf(buf + n, cap - n, " req=%d asked=%d near=%d", pres->requesting(), pres->asked(), pres->count());
        for (int i = 0; i < pres->count(); ++i)
            n += snprintf(buf + n, cap - n, " [%s %s]", show_name(pres->at(i).name_a, pres->at(i).name_b),
                          status_of(pres->at(i)));
    }
}

} // namespace wplay
