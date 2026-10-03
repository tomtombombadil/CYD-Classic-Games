// Solitaire (Klondike): registry entry, save file and screen. Rules and
// scoring in solitaire_core.*; cards, backs and the win show in
// common/cards.*.
//
// Screen: top bar (clock, score, ☰); the table, one custom-drawn object:
// stock, waste, the four foundations, the seven columns; Undo | Hint keys.
// Moving: tap a face-up card (it and the cards on it are picked, amber
// edge), then tap where it goes. Tapping the picked card again (a double
// tap) sends it to its foundation if it can go, else to the first column
// that takes it. The foundations always go Spades, Hearts, Clubs, Diamonds
// (empty ones show their suit), and the whole foundation row is one target:
// a card tapped up there lands on its own suit (Tom). Tap
// the stock to turn cards. When every card is face up and the stock is
// empty the rest go up by themselves, then the cards bounce off the
// table (tap to stop).
//
// Options (☰ → Options): Draw 1 / Draw 3 (default 3), Standard / Vegas /
// No scoring, the card back. Draw takes effect at once; scoring on the
// next deal.
//
// Deals: only winnable ones (Tom). The next deal is searched for in the
// background (solitaire_solve.*, on the AI core) while the current one is
// played, so New Game is usually instant; if it isn't ready, the table says
// "Shuffling..." until it is.
//
// Sounds: none while playing (Tom: card games are quiet) except a soft
// "aww" (Error) for a move that isn't allowed; Fanfare when won.
#include <cstdio>
#include <new>
#include "games/common/ai_task.h"
#include "games/common/cards.h"
#include "games/common/game_kit.h"
#include "games/common/puzzle_stats.h"
#include "games/registry.h"
#include "solitaire_core.h"
#include "solitaire_solve.h"
#include "ui/shell.h"
#include "ui/sound.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace {

using namespace solitaire;
using namespace ui;

constexpr const char* kId = "solitaire";
const char* const kLevels[3] = {"Draw 1", "Draw 3", ""};

struct State {
    Game     g;
    uint8_t  recorded = 0;
    uint32_t seconds = 0;
    uint8_t  opt_draw = 3;                 // for the next deal
    uint8_t  opt_scoring = 0;
    int32_t  bank = 0;                     // Vegas: dollars from earlier deals
    uint32_t next_seed = 0;                // a winnable deal found in the background
    uint8_t  next_draw = 0, next_scoring = 0, next_ok = 0;
};
State* S = nullptr;
constexpr size_t kExtraOld = 1 + 4 + 1 + 1 + 4;          // saves from before the deal check
constexpr size_t kExtra = kExtraOld + 4 + 1 + 1 + 1;
constexpr size_t kSaveBytes = Game::kSaveBytes + kExtra;

kit::TopBar bar;
kit::Clock  clock_;
lv_obj_t*   table = nullptr;
lv_obj_t*   undo_k = nullptr;
lv_obj_t*   hint_k = nullptr;
lv_obj_t*   again_k = nullptr;
lv_timer_t* finish_timer = nullptr;

// Geometry (inside `table`)
int cw = 0, ch = 0, pitch = 0, x0 = 0, row_y = 0, tab_y = 0, tab_bottom = 0, fan = 0;
int down_step = 0, up_step = 0;

int sel_pile = -1, sel_idx = -1;           // picked cards
int hint_to = -1;                          // Hint: where the picked card can go
bool searching = false;                    // a background deal search is running
bool shuffling = false;                    // the player is waiting for it
uint32_t last_save_ms = 0;
bool     dirty = false;

void build();
void open_menu();
void update_status();

// ---- Save ---------------------------------------------------------------------------------
void put32(uint8_t* b, uint32_t v) { for (int k = 0; k < 4; ++k) b[k] = uint8_t(v >> (8 * k)); }
uint32_t get32(const uint8_t* b) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(b[k]) << (8 * k); return v; }

void save()
{
    if (!S || !shell().save_game) return;
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return;
    size_t n = S->g.serialize(buf, kSaveBytes);
    buf[n++] = S->recorded;
    put32(buf + n, S->seconds); n += 4;
    buf[n++] = S->opt_draw;
    buf[n++] = S->opt_scoring;
    put32(buf + n, uint32_t(S->bank)); n += 4;
    put32(buf + n, S->next_seed); n += 4;
    buf[n++] = S->next_draw;
    buf[n++] = S->next_scoring;
    buf[n++] = S->next_ok;
    shell().save_game(kId, buf, n);
    delete[] buf;
    dirty = false;
}

bool load(State& st)
{
    uint8_t* buf = new (std::nothrow) uint8_t[kSaveBytes];
    if (!buf) return false;
    const size_t n = shell().load_game ? shell().load_game(kId, buf, kSaveBytes) : 0;
    bool ok = (n == kSaveBytes || n == Game::kSaveBytes + kExtraOld) && st.g.deserialize(buf, n);
    if (ok) {
        const uint8_t* q = buf + Game::kSaveBytes;
        st.recorded = q[0];
        st.seconds = get32(q + 1);
        st.opt_draw = q[5] == 1 ? 1 : 3;
        st.opt_scoring = q[6] <= 2 ? q[6] : 0;
        st.bank = int32_t(get32(q + 7));
        if (n == kSaveBytes) {
            st.next_seed = get32(q + 11);
            st.next_draw = q[15];
            st.next_scoring = q[16];
            st.next_ok = q[17];
        }
    }
    delete[] buf;
    return ok;
}

// ---- Geometry -------------------------------------------------------------------------------
int col_x(int i) { return x0 + i * pitch; }

void pile_xy(int p, int* x, int* y)
{
    if (p == Stock)        { *x = col_x(0); *y = row_y; }
    else if (p == Waste)   { *x = col_x(1); *y = row_y; }
    else if (p < Tab0)     { *x = col_x(3 + (p - Found0)); *y = row_y; }
    else                   { *x = col_x(p - Tab0); *y = tab_y; }
}

// Card offsets in a column: tighter when the column would run off the table
void column_steps(int p, int* down, int* up)
{
    const Stack& s = S->g.pile[p];
    const int downs = S->g.first_up(p), ups = s.n - downs;
    *down = down_step;
    *up = up_step;
    const int room = tab_bottom - tab_y - ch;
    if (ups > 1 && downs * *down + (ups - 1) * *up > room) {
        *up = (room - downs * *down) / (ups - 1);
        const int min_up = cards::index_h(cw, ch) * 2 / 3;
        if (*up < min_up) {
            *up = min_up;
            *down = downs ? (room - (ups - 1) * *up) / downs : *down;
            if (*down < 3) *down = 3;
        }
    }
}

int card_y(int p, int i)
{
    if (p < Tab0) return row_y;
    int d, u;
    column_steps(p, &d, &u);
    const int downs = S->g.first_up(p);
    return tab_y + (i < downs ? i * d : downs * d + (i - downs) * u);
}

// The waste shows up to three cards fanned (Draw 3) or just the top one
int waste_shown() { return S->g.draw == 3 ? (S->g.pile[Waste].n < 3 ? S->g.pile[Waste].n : 3) : (S->g.pile[Waste].n ? 1 : 0); }
int waste_top_x()  { const int k = waste_shown(); return col_x(1) + (k > 1 ? (k - 1) * fan : 0); }

// What's under a tap: pile and card index (-1 = the empty pile / its area)
bool hit(int px, int py, int* pile, int* idx)
{
    const Game& g = S->g;
    if (py >= row_y && py < row_y + ch) {
        if (px >= col_x(0) && px < col_x(0) + cw) { *pile = Stock; *idx = -1; return true; }
        if (px >= col_x(1) && px < waste_top_x() + cw && px < col_x(3) - 2) {
            *pile = Waste; *idx = g.pile[Waste].n - 1; return true;
        }
        // the foundation row: one wide target from the first pile to the right edge
        if (px >= col_x(3) - pitch / 3) {
            int f = (px - col_x(3) + (pitch - cw) / 2) / pitch;
            f = f < 0 ? 0 : f > 3 ? 3 : f;
            *pile = Found0 + f; *idx = g.pile[Found0 + f].n - 1;
            return true;
        }
        return false;
    }
    if (py < tab_y - 3) return false;
    for (int c = 0; c < 7; ++c) {
        if (px < col_x(c) - 1 || px >= col_x(c) + cw + 1) continue;
        const int p = Tab0 + c;
        const Stack& s = g.pile[p];
        *pile = p;
        *idx = -1;
        for (int i = s.n - 1; i >= 0; --i)
            if (py >= card_y(p, i)) { *idx = i; break; }
        if (*idx < 0 && s.n) *idx = 0;
        return true;
    }
    return false;
}

// ---- Drawing --------------------------------------------------------------------------------
void draw_cb(lv_event_t* e)
{
    if (!S) return;
    const Game& g = S->g;
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    kit::fill_rect(layer, a.x1, a.y1, a.x2, a.y2, cards::felt(), 0);
    const int ox = a.x1, oy = a.y1;
    // Stock
    if (g.pile[Stock].n) cards::draw_back(layer, ox + col_x(0), oy + row_y, cw, ch);
    else {
        cards::draw_slot(layer, ox + col_x(0), oy + row_y, cw, ch);
        if (g.can_draw()) kit::text(layer, LV_SYMBOL_REFRESH, &lv_font_montserrat_20, pal().stone_light,
                                    ox + col_x(0), oy + row_y, cw, ch);
    }
    if (hint_to == Stock)
        kit::fill_rect(layer, ox + col_x(0), oy + row_y + ch - 4, ox + col_x(0) + cw - 1, oy + row_y + ch - 1, pal().target, 2);
    // Waste
    const int k = waste_shown();
    if (!k) cards::draw_slot(layer, ox + col_x(1), oy + row_y, cw, ch);
    for (int i = 0; i < k; ++i) {
        const Stack& w = g.pile[Waste];
        const int ci = w.n - k + i;
        const bool picked = sel_pile == Waste && i == k - 1;
        cards::draw_face(layer, ox + col_x(1) + i * fan, oy + row_y, cw, ch, Game::card(w.c[ci]), picked);
    }
    // Foundations
    for (int f = 0; f < 4; ++f) {
        const Stack& s = g.pile[Found0 + f];
        const int x = ox + col_x(3 + f), y = oy + row_y;
        if (s.n) cards::draw_face(layer, x, y, cw, ch, Game::card(s.top()), sel_pile == Found0 + f);
        else     cards::draw_slot(layer, x, y, cw, ch, kFoundSuit[f]);    // the suit it takes
        if (hint_to == Found0 + f) kit::fill_rect(layer, x, y + ch - 4, x + cw - 1, y + ch - 1, pal().target, 2);
    }
    // Columns
    for (int c = 0; c < 7; ++c) {
        const int p = Tab0 + c;
        const Stack& s = g.pile[p];
        const int x = ox + col_x(c);
        if (!s.n) cards::draw_slot(layer, x, oy + tab_y, cw, ch);
        for (int i = 0; i < s.n; ++i) {
            const int y = oy + card_y(p, i);
            if (!Game::face_up(s.c[i])) cards::draw_back(layer, x, y, cw, ch);
            else cards::draw_face(layer, x, y, cw, ch, Game::card(s.c[i]), sel_pile == p && i >= sel_idx);
        }
        if (hint_to == p) {
            const int y = oy + (s.n ? card_y(p, s.n - 1) : tab_y) + ch - 4;
            kit::fill_rect(layer, x, y, x + cw - 1, y + 3, pal().target, 2);
        }
    }
}

// ---- Flow -------------------------------------------------------------------------------------
int32_t shown_score()
{
    return S->g.scoring == Scoring::Vegas ? S->bank + S->g.score : S->g.score;
}

// Clock ticks only change the top bar: the board isn't redrawn for them
// (a full card table redraw every second slowed taps down - Tom).
bool ticking = false;

void update_status()
{
    if (!bar.center || !S) return;
    const Game& g = S->g;
    char t[16], s[40];
    twoplayer::format_time(t, sizeof t, S->seconds);
    lv_label_set_text(bar.left, t);
    if (shuffling) snprintf(s, sizeof s, "Shuffling...");
    else if (clock_.paused && !g.won()) snprintf(s, sizeof s, "Paused");
    else if (g.scoring == Scoring::Standard) snprintf(s, sizeof s, g.won() ? "Won! Score %ld" : "Score %ld", long(g.score));
    else if (g.scoring == Scoring::Vegas) {
        const long v = long(shown_score());
        snprintf(s, sizeof s, v < 0 ? "-$%ld" : "$%ld", v < 0 ? -v : v);
    } else snprintf(s, sizeof s, g.won() ? "Won in %u moves" : "Moves %u", unsigned(g.moves));
    kit::top_bar_status(bar, s);
    const bool won = g.won();
    lv_obj_set_hidden(again_k, !won);
    lv_obj_set_hidden(undo_k, won);
    lv_obj_set_hidden(hint_k, won);
    set_dim(undo_k, g.undo_n == 0);
    if (!ticking) lv_obj_invalidate(table);
}

void record(bool won)
{
    puzzle::Record r;
    r.level = S->g.draw == 1 ? 0 : 1;
    r.solved = won;
    r.lost = !won;                         // a deal left unfinished counts as lost, like Windows
    r.moves = S->g.moves;
    r.seconds = S->seconds;
    kit::record_solo(kId, r, kLevels);
}

void clear_pick() { sel_pile = sel_idx = hint_to = -1; }

// ---- Winnable deals, found in the background ----------------------------------------
constexpr uint32_t kNodeLimit = 20000;     // per candidate deal (PC: ~7 ms; board: ~0.2 s)
struct Search {
    uint32_t seed;
    uint8_t  draw, scoring;
    uint32_t result;
    int      tries;                        // 0 = no memory for the search
    uint32_t ms;
    volatile bool done;
};
Search search{};

void search_job(void* ctx, volatile bool* stop)
{
    Search* s = static_cast<Search*>(ctx);
    const uint32_t t0 = lv_tick_get();
    int tries = 0;
    s->result = find_winnable(s->seed, s->draw, Scoring(s->scoring), kNodeLimit, stop, &tries);
    s->tries = tries;
    s->ms = lv_tick_get() - t0;
    s->done = true;                        // a stopped search is dropped by whoever stopped it
}

void start_search()
{
    if (searching || !S) return;
    search.seed = shell().random_seed ? shell().random_seed() : lv_tick_get();
    search.draw = S->opt_draw;
    search.scoring = S->opt_scoring;
    search.done = false;
    log_step("solitaire: deal search, draw %d", search.draw);
    searching = ai_start(search_job, &search, 20 * 1024);   // moves live on the heap, frames are small
    if (!searching && shuffling) {
        // No task (out of memory): deal an unchecked shuffle rather than wait forever
        log_event("Solitaire: deal search could not start");
        search.result = search.seed;
        search.tries = 0;
        search.done = true;
        searching = true;
    }
}

void deal_seed(uint32_t seed);

// Called from tick(): a finished search becomes the next deal
void poll_search()
{
    if (!searching || !search.done) return;
    searching = false;
    log_step("solitaire: deal found, %d tries, %lu ms", search.tries, (unsigned long)search.ms);
    if (search.tries == 0 || search.tries >= solitaire::kMaxTries)
        log_event("Solitaire: no proven deal (%d tries, %lu ms)", search.tries, (unsigned long)search.ms);
    S->next_seed = search.result;
    S->next_draw = search.draw;
    S->next_scoring = search.scoring;
    S->next_ok = 1;
    if (shuffling) {
        if (S->next_draw == S->opt_draw && S->next_scoring == S->opt_scoring) {
            shuffling = false;
            S->next_ok = 0;
            deal_seed(S->next_seed);
        }
        start_search();                       // options changed meanwhile: look again
        return;
    }
    dirty = true;
}

void stop_finish()
{
    if (finish_timer) { lv_timer_delete(finish_timer); finish_timer = nullptr; }
}

void deal_seed(uint32_t seed)
{
    S->g.deal(seed, S->opt_draw, Scoring(S->opt_scoring));
    S->recorded = 0;
    S->seconds = 0;
    clear_pick();
    save();
    build();
    start_search();                           // get the one after ready
}

void deal(bool same)
{
    stop_finish();
    cards::celebrate_stop();
    kit::flash_stop();
    Game& g = S->g;
    if (!shuffling) {
        if (!S->recorded && g.moves > 0 && !g.won()) record(false);
        if (g.scoring == Scoring::Vegas) S->bank += g.score;     // this deal's dollars stay
    }
    if (same) {                                // Restart: the same (winnable) deal again
        g.deal(g.seed, S->opt_draw, g.scoring);
        S->recorded = 0;
        S->seconds = 0;
        clear_pick();
        save();
        build();
        return;
    }
    if (S->next_ok && S->next_draw == S->opt_draw && S->next_scoring == S->opt_scoring) {
        S->next_ok = 0;
        deal_seed(S->next_seed);
        return;
    }
    // Not ready yet: wait for the search ("Shuffling...")
    shuffling = true;
    S->recorded = 1;                           // nothing to record until the deal arrives
    clear_pick();
    start_search();
    poll_search();                             // the PC preview finishes at once
    if (shuffling) update_status();
}

void play_show();

void won_now()
{
    stop_finish();
    if (S->recorded) return;
    S->recorded = 1;
    Game& g = S->g;
    if (g.scoring == Scoring::Standard && S->seconds >= 30) g.score += int32_t(700000 / S->seconds);
    record(true);
    sound(Sound::Fanfare);
    save();
    update_status();
    play_show();
}

void finish_cb(lv_timer_t*)
{
    if (!S) return;
    if (!S->g.finish_step()) { stop_finish(); return; }
    update_status();
    if (S->g.won()) won_now();
}

void after_move()
{
    clear_pick();
    dirty = true;
    Game& g = S->g;
    if (g.won()) { won_now(); return; }
    if (g.can_finish() && !finish_timer) finish_timer = lv_timer_create(finish_cb, 90, nullptr);
    update_status();
}

// Win show: Kings first, round the four foundations, like Windows
void show_done() { update_status(); }

void play_show()
{
    cards::Launch list[52];
    int n = 0;
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    for (int r = 13; r >= 1; --r)
        for (int f = 0; f < 4; ++f) {
            int x, y;
            pile_xy(Found0 + f, &x, &y);
            const Stack& s = S->g.pile[Found0 + f];
            if (s.n < r) continue;
            list[n++] = cards::Launch{int16_t(a.x1 + x), int16_t(a.y1 + y), Game::card(s.c[r - 1]),
                                      uint8_t(r >= 2 ? Game::card(s.c[r - 2]) : 0xFF)};
        }
    cards::celebrate(list, n, cw, ch, show_done);
}

// ---- Taps --------------------------------------------------------------------------------------
bool pickable(int p, int i)
{
    const Game& g = S->g;
    if (i < 0 || i >= g.pile[p].n || !Game::face_up(g.pile[p].c[i])) return false;
    if (p == Waste || (p >= Found0 && p < Tab0)) return i == g.pile[p].n - 1;
    return p >= Tab0;
}

void table_cb(lv_event_t*)
{
    if (!S || overlay_open() || finish_timer || S->g.won() || shuffling) return;
    lv_point_t pt;
    lv_indev_get_point(lv_indev_active(), &pt);
    lv_area_t a;
    lv_obj_get_coords(table, &a);
    int p, i;
    if (!hit(pt.x - a.x1, pt.y - a.y1, &p, &i)) { clear_pick(); update_status(); return; }
    Game& g = S->g;
    if (p == Stock) {
        clear_pick();
        if (g.draw_stock()) dirty = true;
        else sound(Sound::Error);
        update_status();
        return;
    }
    if (sel_pile >= 0) {
        const int sp = sel_pile, si = sel_idx;
        if (p == sp && (i == si || p < Tab0)) {               // the picked card again: send it
            const int to = g.best_target(sp, si);
            if (to >= 0 && g.move(sp, si, to)) {after_move(); return; }
            clear_pick();
            sound(Sound::Error);
            update_status();
            return;
        }
        // The whole foundation row is one target: the card goes to its suit's pile
        const int dest = (p >= Found0 && p < Tab0 && si == g.pile[sp].n - 1) ? found_for(g.pile[sp].c[si]) : p;
        if (g.move(sp, si, dest)) {after_move(); return; }
        if (pickable(p, i)) { sel_pile = p; sel_idx = i; hint_to = -1; update_status(); return; }
        clear_pick();
        sound(Sound::Error);
        update_status();
        return;
    }
    if (pickable(p, i)) { sel_pile = p; sel_idx = i; hint_to = -1; }
    update_status();
}

void undo_cb(lv_event_t*)
{
    if (!S || finish_timer || shuffling || !S->g.undo()) return;
    clear_pick();

    dirty = true;
    update_status();
}

void hint_cb(lv_event_t*)
{
    if (!S || finish_timer || shuffling) return;
    int f, i, to;
    clear_pick();
    if (!S->g.hint(&f, &i, &to)) {update_status(); return; }
    if (f == Stock) { hint_to = Stock; sel_pile = -1; }
    else { sel_pile = f; sel_idx = i; hint_to = to; }

    update_status();
}

void again_cb(lv_event_t*) { deal(false); }

void build()
{
    kit::screen_begin();
    bar = kit::top_bar([](lv_event_t*) { open_menu(); });
    const Metrics& m = metrics();
    lv_obj_t* scr = lv_screen_active();
    const int pad = m.large ? 8 : 4, kh = menu_btn_h(), gap = m.large ? 8 : 6;
    const int ky = m.h - pad - kh, half = (m.w - 2 * pad - gap) / 2;
    // The table: everything between the bar and the keys
    const int top = bar.h;
    table = lv_obj_create(scr);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, m.w, ky - gap / 2 - top);
    lv_obj_set_pos(table, 0, top);
    lv_obj_set_clickable(table, true);
    lv_obj_add_event_cb(table, draw_cb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(table, table_cb, LV_EVENT_PRESSED, nullptr);
    undo_k = make_key(scr, half, kh, undo_cb, 0);
    key_label(undo_k, "Undo", menu_font());
    lv_obj_set_pos(undo_k, pad, ky);
    hint_k = make_key(scr, half, kh, hint_cb, 0);
    key_label(hint_k, "Hint", menu_font());
    lv_obj_set_pos(hint_k, m.w - pad - half, ky);
    again_k = make_key(scr, m.w - 2 * pad, kh, again_cb, 0);
    lv_obj_add_state(again_k, LV_STATE_CHECKED);
    key_label(again_k, "Play Again", menu_font());
    lv_obj_set_pos(again_k, pad, ky);

    const int mg = m.large ? 4 : 2;
    pitch = (m.w - 2 * mg) / 7;
    cw = pitch - (m.large ? 4 : 3);
    ch = cw * 7 / 5;
    x0 = mg + (m.w - 2 * mg - 7 * pitch) / 2 + (pitch - cw) / 2;
    row_y = m.large ? 6 : 4;
    tab_y = row_y + ch + (m.large ? 10 : 6);
    tab_bottom = ky - gap / 2 - top - 2;
    fan = (col_x(3) - col_x(1) - cw - 4) / 2;
    down_step = ch / 7 > 5 ? ch / 7 : 5;
    up_step = cards::index_h(cw, ch) + 2;
    clock_ = kit::Clock{};
    update_status();
}

// ---- Options ----------------------------------------------------------------------------------
void options_open();

void opt_cb(lv_event_t* e)
{
    const int id = int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    // Draw takes effect at once (Tom picked Draw 1 and the deal kept turning
    // three); scoring waits for the next deal, so a score never mixes rules
    if (id == 1 || id == 3) { S->opt_draw = uint8_t(id); S->g.draw = uint8_t(id); }
    else if (id >= 10 && id <= 12) S->opt_scoring = uint8_t(id - 10);
    if (id == 1 || id == 3 || (id >= 10 && id <= 12)) {          // look for a deal with the new rules
        if (searching) { ai_stop(); searching = false; }
        S->next_ok = 0;
        start_search();
    }
    else if (id == 99) { open_menu(); return; }
    else if (id == 98) { cards::back_screen(options_open); return; }
    dirty = true;
    options_open();
}

lv_obj_t* opt_row(const char* const* labels, const int* ids, int n, int on)
{
    lv_obj_t* r = lv_obj_create(overlay());
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), menu_btn_h());
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r, 6, 0);
    lv_obj_set_scrollable(r, false);
    for (int k = 0; k < n; ++k) {
        lv_obj_t* b = make_key(r, 10, menu_btn_h(), opt_cb, ids[k]);
        lv_obj_set_flex_grow(b, 1);
        key_label(b, labels[k], menu_font());
        set_checked(b, ids[k] == on);
    }
    return r;
}

void options_open()
{
    overlay_begin("Options");
    overlay_text("Draw:", false);
    static const char* const dl[2] = {"Draw 1", "Draw 3"};
    static const int di[2] = {1, 3};
    opt_row(dl, di, 2, S->opt_draw);
    overlay_text("Scoring:", false);
    static const char* const sl[3] = {"Standard", "Vegas", "None"};
    static const int si[3] = {10, 11, 12};
    opt_row(sl, si, 3, 10 + S->opt_scoring);
    overlay_text("Draw changes at once; scoring starts with the next deal.", true);
    overlay_button(overlay(), "Card Back", opt_cb, 98);
    overlay_bottom_button("Back", opt_cb, 99);
}

// ---- Menu ------------------------------------------------------------------------------------
void menu_pick(int id)
{
    if (id == kit::kLevel0) deal(false);
    else if (id == kit::kRestart) deal(true);
    else if (id == kit::kOptions) options_open();
}
void menu_stats() { kit::stats_solo(kId, kLevels, open_menu, true, 2); }
void menu_back()  { update_status(); }

void open_menu()
{
    clear_pick();
    kit::MenuHandlers h{menu_pick, menu_stats, menu_back, open_menu};
    kit::menu_solo("Solitaire", nullptr, h, true, "Options");
}

// ---- Registry entry -----------------------------------------------------------------------------
void open()
{
    S = new (std::nothrow) State();
    if (!S) { app_go_home(); return; }
    searching = shuffling = false;
    const bool loaded = load(*S);
    if (!loaded) *S = State{};
    clear_pick();
    dirty = false;
    build();
    if (!loaded) { deal(false); return; }      // the first deal: a winnable one
    if (S->g.can_finish()) finish_timer = lv_timer_create(finish_cb, 90, nullptr);
    if (!S->next_ok) start_search();
}

void close()
{
    if (!S) return;
    ai_stop();
    searching = shuffling = false;
    stop_finish();
    cards::celebrate_stop();
    kit::flash_stop();
    save();
    bar = kit::TopBar{};
    table = undo_k = hint_k = again_k = nullptr;
    delete S;
    S = nullptr;
}

void tick(uint32_t now)
{
    if (!S) return;
    poll_search();
    if (clock_.tick(now, !S->g.won() && S->g.moves > 0, S->seconds) && !cards::celebrating()) { ticking = true; update_status(); ticking = false; }
    if ((dirty || now - last_save_ms > 30000) && !cards::celebrating()) { last_save_ms = now; save(); }
}

void restyle() { if (S) { cards::celebrate_stop(); build(); } }

bool summary(char* buf, size_t cap)
{
    State* tmp = nullptr;
    const State* st = S;
    if (!st) {
        tmp = new (std::nothrow) State();
        if (!tmp || !load(*tmp)) { delete tmp; return false; }
        st = tmp;
    }
    const Game& g = st->g;
    int up = 0;
    for (int f = 0; f < 4; ++f) up += g.pile[Found0 + f].n;
    if (g.won()) snprintf(buf, cap, "Draw %d, won", g.draw);
    else         snprintf(buf, cap, "Draw %d, %d of 52 cards up", g.draw, up);
    delete tmp;
    return true;
}

void save_now() { save(); }

// Icon: a short fanned column of cards on the felt
void icon_draw_cb(lv_event_t* e)
{
    lv_area_t a;
    lv_obj_get_coords(lv_event_get_target_obj(e), &a);
    lv_layer_t* layer = lv_event_get_layer(e);
    const int size = lv_area_get_width(&a), w = size * 55 / 100, h = size * 72 / 100;
    cards::draw_back(layer, a.x1, a.y1, w, h);
    cards::draw_face(layer, a.x1 + size - w, a.y1 + size / 10, w, h, cards::make(1, cards::Spades));
    cards::draw_face(layer, a.x1 + (size - w) / 2, a.y1 + size - h, w, h, cards::make(13, cards::Hearts));
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
extern const GameOps solitaire_ops;
const GameOps solitaire_ops = {open, close, save_now, tick, restyle, summary, icon};
} // namespace games
