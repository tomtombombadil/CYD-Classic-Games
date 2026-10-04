#include "wplay.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <lvgl.h>
#include "games/registry.h"
#include "ui/keyboard.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace wplay {

using namespace ui;

namespace {

// ---- The wireless games -------------------------------------------------------------------------
int wl_games[net::kMaxGames];
int wl_n = -1;

void collect()
{
    if (wl_n >= 0) return;
    wl_n = 0;
    for (int i = 0; i < games::count() && wl_n < net::kMaxGames; ++i)
        if (games::get(i).modes & games::kNetwork) wl_games[wl_n++] = i;
}

const char* title_of(int g) { return g >= 0 && g < game_count() ? games::get(wl_games[g]).title : "?"; }
uint16_t all_games() { return uint16_t((1u << game_count()) - 1); }

// ---- The player's setup -----------------------------------------------------------------------
struct Profile {
    char     name[net::kNameMax + 1] = "";
    uint16_t games = 0xFFFF;
    bool     available = false;
};
Profile P;
bool    loaded = false;
constexpr const char* kProfileFile = "player";
constexpr size_t kProfileBytes = 4 + (net::kNameMax + 1) + 2 + 1;

net::Mac my_mac()
{
    net::Mac m;
    if (shell().radio_mac) shell().radio_mac(m.b);
    return m;
}

void save_profile()
{
    uint8_t buf[kProfileBytes] = {'P', 'L', 'R', '2'};
    memcpy(buf + 4, P.name, strlen(P.name));
    buf[4 + net::kNameMax + 1] = uint8_t(P.games);
    buf[4 + net::kNameMax + 2] = uint8_t(P.games >> 8);
    buf[4 + net::kNameMax + 3] = P.available ? 1 : 0;
    if (shell().save_game) shell().save_game(kProfileFile, buf, sizeof buf);
}

// ---- Radio and presence -------------------------------------------------------------------------
bool           radio_on = false;
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
    bool      fresh = false;               // agreed, its game hasn't started it yet
    bool      noted = false;               // its end was taken in by the game
    bool      over = false;                // its game is over (a rematch may follow): not busy
};
Session* S = nullptr;
constexpr const char* kSessionFile = "wl_session";
constexpr size_t kSessionBytes = 4 + 16 + net::Link::kSaveBytes;

bool live() { return S && !S->link.ended(); }

// Signal for the header's wifi icon: the partner's while a session is
// going, else the strongest board heard in the last few seconds
int8_t   sig_rssi = -127;
uint32_t sig_ms = 0;
void heard_signal(const net::Mac& from, int8_t rssi, uint32_t now)
{
    const bool partner = S && !S->link.ended() && from == S->link.peer();
    const bool stale = now - sig_ms > 3000;
    if (partner || (!(S && !S->link.ended()) && (stale || rssi > sig_rssi))) {
        // smoothed, so the bars don't flicker
        sig_rssi = stale ? rssi : int8_t((sig_rssi * 3 + rssi) / 4);
        sig_ms = now;
    }
}
// In a game that is still going: others can't ask this board
bool playing() { return live() && !S->over; }

void save_session()
{
    if (!shell().save_game) return;
    uint8_t buf[kSessionBytes] = {};
    if (!S) {
        const uint8_t none[4] = {'W', 'L', 'S', '0'};
        shell().save_game(kSessionFile, none, sizeof none);
        return;
    }
    memcpy(buf, "WLS1", 4);
    memcpy(buf + 4, S->id, strlen(S->id));
    buf[19] = S->over ? 1 : 0;
    if (S->link.save(buf + 20, net::Link::kSaveBytes)) shell().save_game(kSessionFile, buf, sizeof buf);
}

void drop_session()
{
    delete S;
    S = nullptr;
    save_session();
}

void load_all(uint32_t now)
{
    the_air.send = air_send;
    if (loaded) return;
    loaded = true;
    collect();
    const Shell& H = shell();
    uint8_t buf[kSessionBytes > kProfileBytes ? kSessionBytes : kProfileBytes];
    size_t n = H.load_game ? H.load_game(kProfileFile, buf, sizeof buf) : 0;
    if (n >= 4 + net::kNameMax + 1 && (memcmp(buf, "PLR1", 4) == 0 || memcmp(buf, "PLR2", 4) == 0)) {
        buf[4 + net::kNameMax] = 0;
        net::clean_name(reinterpret_cast<const char*>(buf + 4), P.name, sizeof P.name);
        if (memcmp(buf, "PLR2", 4) == 0 && n == kProfileBytes) {
            P.games = uint16_t(buf[4 + net::kNameMax + 1] | (buf[4 + net::kNameMax + 2] << 8));
            P.available = buf[4 + net::kNameMax + 3] != 0;
        }
    }
    if (!P.name[0]) net::default_name(my_mac(), P.name, sizeof P.name);
    n = H.load_game ? H.load_game(kSessionFile, buf, sizeof buf) : 0;
    if (n == kSessionBytes && memcmp(buf, "WLS1", 4) == 0) {
        const bool over = buf[19] != 0;
        buf[19] = 0;
        const int reg = games::find(reinterpret_cast<const char*>(buf + 4));
        Session* s = new (std::nothrow) Session();
        if (s && reg >= 0 && game_of(games::get(reg).id) >= 0
            && s->link.load(buf + 20, net::Link::kSaveBytes, the_air, now)) {
            s->reg = reg;
            s->over = over;
            snprintf(s->id, sizeof s->id, "%s", games::get(reg).id);
            S = s;
        } else {
            delete s;
        }
    }
}

// ---- Screens ----------------------------------------------------------------------------------------
enum class Ui { None, Main, Games, Players, Player, Asking, Offer };
Ui        ui_now = Ui::None;
Ui        before_offer = Ui::None;     // where an offer popped up from
void    (*menu_back)() = nullptr;
net::Mac  who_mac;                     // the player on the Play With screen
char      who_name[net::kNameMax + 1] = "";
char      offer_from[net::kNameMax + 1] = "";
char      note[112] = "";
uint32_t  note_until = 0;
char      shown[400] = "";
bool      keyboard_up = false;
lv_obj_t* picker_label = nullptr;

enum Key : intptr_t {
    kBack = 1, kChangeName, kAvailable, kFind, kMyGames, kResume, kAllGames, kStop, kHand,
    kPlay, kNotNow, kOtherGame, kGame0 = 100, kPlayer0 = 200,
};

void set_note(const char* text, uint32_t ms = 8000)
{
    snprintf(note, sizeof note, "%s", text);
    note_until = lv_tick_get() + ms;
}

void on_closed() { ui_now = Ui::None; }

void show(Ui u);

// Everything a screen shows, as one string: rebuild only when it changes
void describe(char* buf, size_t cap)
{
    int n = snprintf(buf, cap, "%d|%s|%d|%04x|%s|%d|%d|", int(ui_now), P.name, P.available, P.games, note,
                     radio_failed, live() ? S->reg : -1);
    if (pres) {
        n += snprintf(buf + n, cap - n, "%d%d%d|", pres->offering(), pres->asked(), pres->asked_game());
        for (int i = 0; i < pres->count() && n < int(cap) - 40; ++i) {
            const net::Nearby& b = pres->at(i);
            n += snprintf(buf + n, cap - n, "%s,%d%d%d,%d,%04x;", b.name, b.available, b.busy, b.paused,
                          b.busy_game, b.games);
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

// A key in a row of two, tall enough for a two-line game title
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
void bottom(const char*, intptr_t) { overlay_back(key_cb, kBack); }

int nearby_available()
{
    int n = 0;
    if (pres)
        for (int i = 0; i < pres->count(); ++i)
            if (pres->at(i).available && !pres->at(i).busy && pres->same_version(i)) ++n;
    return n;
}

int count_on()
{
    int n = 0;
    for (int g = 0; g < game_count(); ++g) if ((P.games >> g) & 1) ++n;
    return n;
}

void note_text()
{
    if (note[0]) overlay_text(note, false);
}

// A choice between two words, with a switch pointing at the chosen one
void switch_row(const char* left, const char* right, bool right_on, intptr_t id)
{
    const int kh = menu_btn_h();
    lv_obj_t* r = row(kh);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, metrics().large ? 14 : 10, 0);
    const lv_font_t* f = &lv_font_montserrat_14;
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

// Play settings: the stylus hand, then 1P / 2P (2P = wireless play: the
// radio on, others can find this board) and the 2P keys under it
void build_main()
{
    char t[112];
    overlay_begin("Play", on_closed);
    switch_row("Left Hand", "Right Hand", right_handed(), kHand);
    overlay_text("Play Mode", true);
    switch_row("1P", "2P", P.available, kAvailable);
    const bool on = P.available && radio_present();
    if (playing()) {
        snprintf(t, sizeof t, "Resume %s With %s", games::get(S->reg).title, S->link.peer_name());
        overlay_button(overlay(), t, key_cb, kResume, true);
    } else {
        const int n = nearby_available();
        if (on && n) snprintf(t, sizeof t, "Find Players (%d Nearby)", n);
        else         snprintf(t, sizeof t, "Find Players");
        set_dim(overlay_button(overlay(), t, key_cb, kFind), !on);
    }
    snprintf(t, sizeof t, "Games I'll Play (%d of %d)", count_on(), game_count());
    set_dim(overlay_button(overlay(), t, key_cb, kMyGames), !on);
    snprintf(t, sizeof t, "Change Name (%s)", P.name);
    set_dim(overlay_button(overlay(), t, key_cb, kChangeName), !on);
    if (note[0]) note_text();
    else if (!radio_present()) overlay_text("This board has no radio for 2-player play.", true);
    else if (radio_failed) overlay_text("The radio couldn't start: the board is low on memory.", false);
    else if (playing()) {
        snprintf(t, sizeof t, "Your %s game with %s is waiting. Finish it, or forfeit it in its menu, to play another.",
                 games::get(S->reg).title, S->link.peer_name());
        overlay_text(t, true);
    } else if (P.available)
        overlay_text("2P: players nearby can find you and ask you to play, wherever you are on this board.", true);
    else
        overlay_text("2P turns on wireless play with boards nearby.", true);
    bottom(nullptr, 0);
}

void build_games()
{
    overlay_begin("Games I'll Play", on_closed);
    overlay_text("Lit games are the ones others can ask you to play.", true);
    const int h = grid_h();
    const int n = game_count() + 1;                 // + All Games
    for (int k = 0; k < n; k += 2) {
        lv_obj_t* r = row(h);
        for (int j = k; j < k + 2 && j < n; ++j) {
            if (j < game_count()) grid_key(r, title_of(j), kGame0 + j, (P.games >> j) & 1, false);
            else grid_key(r, "All Games", kAllGames, (P.games & all_games()) == all_games(), false);
        }
    }
    bottom(nullptr, 0);
}

int who_index()
{
    if (!pres) return -1;
    for (int i = 0; i < pres->count(); ++i) if (pres->at(i).mac == who_mac) return i;
    return -1;
}

void build_players()
{
    char t[64];
    overlay_begin("Players Nearby", on_closed);
    const int kh = menu_btn_h();
    int shown_rows = 0;
    const int max_rows = 5;
    for (int pass = 0; pass < 2 && pres; ++pass)
        for (int i = 0; i < pres->count() && shown_rows < max_rows; ++i) {
            const net::Nearby& b = pres->at(i);
            int offerable = 0;
            for (int g = 0; g < game_count(); ++g) if (pres->can_offer(i, g)) ++offerable;
            if ((offerable > 0) != (pass == 0)) continue;
            if (!pres->same_version(i))   snprintf(t, sizeof t, "other version");
            else if (b.busy)              snprintf(t, sizeof t, "playing %s", title_of(b.busy_game));
            else if (offerable == 1)      snprintf(t, sizeof t, "1 game");
            else                          snprintf(t, sizeof t, "%d games", offerable);
            lv_obj_t* k = make_key(overlay(), lv_pct(100), kh, key_cb, kPlayer0 + i);
            lv_obj_t* l = lv_label_create(k);
            lv_label_set_text(l, b.name);
            lv_obj_set_style_text_font(l, menu_font(), 0);
            lv_obj_set_style_text_color(l, offerable ? pal().ink : pal().key_dim_text, 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
            lv_obj_t* d = lv_label_create(k);
            lv_label_set_text(d, t);
            lv_obj_set_style_text_font(d, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(d, pal().muted, 0);
            lv_obj_align(d, LV_ALIGN_RIGHT_MID, -8, 0);
            if (!offerable) set_dim(k, true);
            ++shown_rows;
        }
    if (!shown_rows) overlay_text("Looking for players nearby...", false);
    if (note[0]) note_text();
    else overlay_text("Players show up here while their Available To Play is on. Tap one to see the games "
                      "they'll play.", true);
    bottom(nullptr, 0);
}

void build_player()
{
    char t[112];
    snprintf(t, sizeof t, "Play With %s", who_name);
    overlay_begin(t, on_closed);
    const int i = who_index();
    if (i < 0) {
        snprintf(t, sizeof t, "%s isn't nearby any more.", who_name);
        overlay_text(t, false);
    } else if (!pres->same_version(i)) {
        snprintf(t, sizeof t, "%s's board runs another version. Both boards need the same one: "
                              "update them at the web flasher.", who_name);
        overlay_text(t, false);
    } else if (pres->at(i).busy) {
        snprintf(t, sizeof t, "%s is playing %s now.", who_name, title_of(pres->at(i).busy_game));
        overlay_text(t, false);
    } else {
        int n = 0;
        const int h = grid_h();
        lv_obj_t* r = nullptr;
        for (int g = 0; g < game_count(); ++g) {
            if (!pres->can_offer(i, g)) continue;
            if (n % 2 == 0) r = row(h);
            grid_key(r, title_of(g), kGame0 + g, false, false);
            ++n;
        }
        if (!n) {
            snprintf(t, sizeof t, "%s hasn't picked any games to play.", who_name);
            overlay_text(t, false);
        } else if (!note[0]) {
            snprintf(t, sizeof t, "Tap a game to ask %s to play it. You move first.", who_name);
            overlay_text(t, true);
        }
    }
    note_text();
    bottom(nullptr, 0);
}

void build_asking()
{
    char t[112];
    snprintf(t, sizeof t, "Play With %s", who_name);
    overlay_begin(t, on_closed);
    snprintf(t, sizeof t, "Asking %s to play %s...", pres ? pres->offer_name() : who_name,
             title_of(pres ? pres->offer_game() : -1));
    overlay_text(t, false);
    snprintf(t, sizeof t, "%s's board rings and shows your offer. Waiting for an answer.", who_name);
    overlay_text(t, true);
    overlay_button(overlay(), "Stop Asking", key_cb, kStop);
    bottom(nullptr, 0);
}

void build_offer()
{
    char t[128];
    const char* game = title_of(pres->asked_game());
    snprintf(t, sizeof t, "Play %s?", game);
    overlay_begin(t, on_closed);
    snprintf(t, sizeof t, "%s would like to play %s with you. %s moves first.", offer_from, game, offer_from);
    overlay_text(t, false);
    const int cur = app_current_game();
    if (cur >= 0 && cur != wl_games[pres->asked_game()]) {
        snprintf(t, sizeof t, "Your %s game is saved for later.", games::get(cur).title);
        overlay_text(t, true);
    }
    overlay_button(overlay(), "Play", key_cb, kPlay, true);
    overlay_pair("Not Now", key_cb, kNotNow, "Other Game", key_cb, kOtherGame);
    bottom(nullptr, 0);
}

void show(Ui u)
{
    if (keyboard_up) return;
    const Ui was = ui_now;
    switch (u) {
        case Ui::Main:    build_main(); break;
        case Ui::Games:   build_games(); break;
        case Ui::Players: build_players(); break;
        case Ui::Player:  build_player(); break;
        case Ui::Asking:  build_asking(); break;
        case Ui::Offer:   build_offer(); break;
        case Ui::None:    if (was != Ui::None) close_overlays(); ui_now = Ui::None; return;
    }
    ui_now = u;
    describe(shown, sizeof shown);
}

void name_done(const char* text)
{
    keyboard_up = false;
    if (text[0]) {
        net::clean_name(text, P.name, sizeof P.name);
        if (!P.name[0]) net::default_name(my_mac(), P.name, sizeof P.name);
        save_profile();
    }
    show(Ui::Main);
}

void set_available(bool on)
{
    if (P.available == on) return;
    P.available = on;
    save_profile();
    log_event("Wireless: available %s", on ? "on" : "off");
}

void key_cb(lv_event_t* e)
{
    const intptr_t id = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const uint32_t now = lv_tick_get();
    switch (id) {
        case kBack:
            note[0] = 0;
            switch (ui_now) {
                case Ui::Games: case Ui::Players: show(Ui::Main); return;
                case Ui::Player: show(Ui::Players); return;
                case Ui::Asking:                     // = Stop Asking
                    if (pres) pres->cancel(now);
                    show(Ui::Player);
                    return;
                case Ui::Offer:                      // = Not Now
                    if (pres && pres->asked()) pres->decline(net::Reason::NotNow, now);
                    show(before_offer);
                    return;
                default: {
                    void (*back)() = menu_back;
                    menu_back = nullptr;
                    show(Ui::None);
                    if (back) back();
                    return;
                }
            }
        case kHand:
            settings().left_handed = !settings().left_handed;
            save_settings();
            app_theme_changed();                 // the open game lays itself out again
            show(Ui::Main);
            return;
        case kChangeName:
            if (!P.available) return;
            keyboard_up = true;
            ui_now = Ui::None;
            keyboard_open("Your Name", P.name, net::kNameMax, name_done);
            return;
        case kAvailable:
            note[0] = 0;
            set_available(!P.available);
            show(Ui::Main);
            return;
        case kFind:
            if (!P.available) return;            // 2P first
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
        case kAllGames:
            P.games = (P.games & all_games()) == all_games() ? 0 : 0xFFFF;
            save_profile();
            show(Ui::Games);
            return;
        case kStop:
            if (pres) pres->cancel(now);
            show(Ui::Player);
            return;
        case kPlay:
            if (pres && pres->asked()) pres->accept(now);   // tick() starts the game
            tick(now);
            return;
        case kNotNow:
        case kOtherGame:
            if (pres && pres->asked())
                pres->decline(id == kNotNow ? net::Reason::NotNow : net::Reason::OtherGame, now);
            note[0] = 0;
            show(before_offer);
            return;
        default: break;
    }
    if (id >= kPlayer0 && pres) {
        const int i = int(id - kPlayer0);
        if (i < pres->count()) {
            who_mac = pres->at(i).mac;
            snprintf(who_name, sizeof who_name, "%s", pres->at(i).name);
            note[0] = 0;
            show(Ui::Player);
        }
        return;
    }
    if (id >= kGame0 && id < kGame0 + net::kMaxGames) {
        const int g = int(id - kGame0);
        if (ui_now == Ui::Games) {
            P.games ^= uint16_t(1u << g);
            save_profile();
            show(Ui::Games);
        } else if (ui_now == Ui::Player && pres && !playing()) {
            const int i = who_index();
            if (i >= 0 && pres->can_offer(i, g)) {
                const uint32_t session = shell().random_seed ? shell().random_seed() : now;
                note[0] = 0;
                pres->offer(i, g, session ? session : 1, now);
                show(Ui::Asking);
            }
        }
    }
}

// ---- Events ------------------------------------------------------------------------------------
void start_session(uint32_t now)
{
    const int g = pres->game();
    if (g < 0 || g >= game_count()) return;
    if (S && !S->link.ended()) {
        // A finished game still open for a rematch: no more games there
        S->link.done(now);
        for (int k = 0; k < 2; ++k) S->link.say_end(now);
    }
    if (S) drop_session();
    S = new (std::nothrow) Session();
    if (!S) return;
    S->reg = wl_games[g];
    snprintf(S->id, sizeof S->id, "%s", games::get(S->reg).id);
    S->link.begin(my_mac(), pres->partner(), pres->partner_name(), pres->session(), pres->inviter(), the_air, now);
    S->fresh = true;
    save_session();
    log_event("Wireless: %s with %s", S->id, pres->partner_name());
    note[0] = 0;
    menu_back = nullptr;
    show(Ui::None);
    close_overlays();
    if (app_current_game() != S->reg) app_open_game(S->reg);
}

void handle_events(uint32_t now)
{
    char t[112];
    switch (pres->poll()) {
        case net::Presence::Event::Started:
            start_session(now);
            return;
        case net::Presence::Event::Declined: {
            const char* n = pres->offer_name();
            switch (pres->reason()) {
                case net::Reason::NotNow:
                    snprintf(t, sizeof t, "%s can't play right now. Thanks for asking!", n); break;
                case net::Reason::OtherGame:
                    snprintf(t, sizeof t, "%s would rather play another game. Pick a different one.", n); break;
                case net::Reason::Busy:
                    snprintf(t, sizeof t, "%s just started another game.", n); break;
                case net::Reason::GameOff:
                    snprintf(t, sizeof t, "%s isn't playing that game right now.", n); break;
            }
            set_note(t);
            sound(Sound::Turn);
            if (ui_now == Ui::Asking) show(Ui::Player);
            return;
        }
        case net::Presence::Event::NoAnswer:
            snprintf(t, sizeof t, "%s didn't answer. %s may be away from the board.", pres->offer_name(),
                     pres->offer_name());
            set_note(t);
            if (ui_now == Ui::Asking) show(Ui::Player);
            return;
        case net::Presence::Event::Gone:
            snprintf(t, sizeof t, "%s's board went out of range.", pres->offer_name());
            set_note(t);
            if (ui_now == Ui::Asking) show(Ui::Player);
            return;
        case net::Presence::Event::None:
            break;
    }
    // Someone asks this board: the question pops up over whatever is on
    if (pres->asked() && ui_now != Ui::Offer && !keyboard_up) {
        before_offer = (ui_now == Ui::Main || ui_now == Ui::Games || ui_now == Ui::Players || ui_now == Ui::Player)
                     ? ui_now : Ui::None;
        snprintf(offer_from, sizeof offer_from, "%s", pres->asker_name());
        sound(Sound::Call);
        show(Ui::Offer);
    } else if (!pres->asked() && ui_now == Ui::Offer) {
        snprintf(t, sizeof t, "%s stopped asking.", offer_from);
        set_note(t, 5000);
        show(before_offer);
    }
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
        uint32_t fr = 0, big = 0;
        if (H.memory) H.memory(&fr, &big);
        log_event("Wireless: radio on, %lu KB free", (unsigned long)(fr / 1024));
        if (!pres) pres = new (std::nothrow) net::Presence();
        if (pres) pres->begin(my_mac(), H.firmware_version ? H.firmware_version : "", the_air, now);
    } else if (!want && radio_on) {
        H.radio_off();
        radio_on = false;
        delete pres;
        pres = nullptr;
        log_step("Wireless: radio off");
    }
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

void tick(uint32_t now)
{
    the_air.send = air_send;
    load_all(now);
    // An ended session goes once the game took it in and the ending was said
    if (S && S->link.ended() && S->noted && S->link.linger_over(now)) drop_session();
    const bool session_radio = S && (!S->link.ended() || !S->link.linger_over(now));
    radio_power(P.available || session_radio, now);
    if (radio_on) {
        uint8_t buf[net::kPacketMax];
        net::Mac from;
        for (int k = 0; k < 16; ++k) {
            int8_t rssi = -100;
            const size_t n = shell().radio_recv(from.b, buf, sizeof buf, &rssi);
            if (!n) break;
            heard_signal(from, rssi, now);
            const uint32_t s = net::status_session(buf, n);
            if (S && s && s == S->link.session()) S->link.receive(from, buf, n, now);
            else if (pres) pres->receive(from, buf, n, now);
        }
        const bool paused = S && app_current_game() != S->reg;
        if (S) {
            S->link.set_away(paused, now);
            S->link.tick(now);
        }
        if (pres) {
            pres->set_profile(P.name, P.available, uint16_t(P.games & all_games()),
                              playing() ? game_of(S->id) : -1, playing() && paused, now);
            pres->tick(now);
            handle_events(now);
        }
    }
    if (note[0] && int32_t(now - note_until) >= 0) note[0] = 0;
    // Screens follow what they show
    if (ui_now != Ui::None && ui_now != Ui::Offer && !keyboard_up && overlay_open()) {
        char d[sizeof shown];
        describe(d, sizeof d);
        if (strcmp(d, shown) != 0) show(ui_now);
    }
    if (picker_label) {
        char t[48];
        picker_status(t, sizeof t);
        if (strcmp(lv_label_get_text(picker_label), t) != 0) lv_label_set_text(picker_label, t);
    }
}

net::Link* session_for(const char* id) { return S && strcmp(S->id, id) == 0 ? &S->link : nullptr; }

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

void session_finished()
{
    if (!S) return;
    S->noted = true;
    save_session();
}

namespace {
char after_note[112] = "";
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

void picker_status(char* buf, size_t cap)
{
    if (!radio_present()) { snprintf(buf, cap, "-"); return; }
    // Short: it shares a row with "Wireless Play" on a 240-px screen
    if (playing()) { snprintf(buf, cap, "Playing"); return; }
    if (!P.available) { snprintf(buf, cap, "Off"); return; }
    const int n = nearby_available();
    if (n) snprintf(buf, cap, "%d nearby", n);
    else   snprintf(buf, cap, "On");
}

void set_picker_label(lv_obj_t* label) { picker_label = label; }

int wifi_level()
{
    if (!radio_on || (!P.available && !live())) return -1;     // 1P: off
    if (lv_tick_get() - sig_ms > 4000) return 0;                // nobody heard: the dot
    if (sig_rssi >= -60) return 3;
    if (sig_rssi >= -70) return 2;
    if (sig_rssi >= -80) return 1;
    return 0;
}

int two_player_state() { return playing() ? 1 : 0; }

void resume_session()
{
    if (!playing() || app_current_game() == S->reg) return;
    show(Ui::None);
    close_overlays();
    app_open_game(S->reg);
}

void set_two_player(bool on) { set_available(on); }

void debug_state(char* buf, size_t cap)
{
    int n = snprintf(buf, cap, "ui=%d S=%d live=%d over=%d end=%d avail=%d radio=%d", int(ui_now), S != nullptr,
                     live(), S ? S->over : -1, S ? int(S->link.end_reason()) : -1, P.available, radio_on);
    if (pres) {
        n += snprintf(buf + n, cap - n, " offering=%d asked=%d near=%d", pres->offering(), pres->asked(), pres->count());
        for (int i = 0; i < pres->count(); ++i)
            n += snprintf(buf + n, cap - n, " [%s a%d b%d g%04x]", pres->at(i).name, pres->at(i).available,
                          pres->at(i).busy, pres->at(i).games);
    }
}

} // namespace wplay
