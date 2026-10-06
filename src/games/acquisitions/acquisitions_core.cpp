#include "acquisitions_core.h"

#include <cstdio>
#include <cstring>

namespace acq {

namespace {

const char* const kNames[kChains] = {"Sunrise", "Oakwood", "Harbor", "Meadow", "Lagoon", "Royal", "Crimson"};
const char kLetters[kChains] = {'S', 'O', 'H', 'M', 'L', 'R', 'C'};
const uint8_t kTier[kChains] = {0, 0, 1, 1, 1, 2, 2};

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

int neighbours(int t, int* out)
{
    const int r = t / kCols, c = t % kCols;
    int n = 0;
    if (r > 0) out[n++] = t - kCols;
    if (r < kRows - 1) out[n++] = t + kCols;
    if (c > 0) out[n++] = t - 1;
    if (c < kCols - 1) out[n++] = t + 1;
    return n;
}

int round_up_100(int v) { return (v + 99) / 100 * 100; }

} // namespace

const char* chain_name(int c) { return c >= 0 && c < kChains ? kNames[c] : "?"; }
char chain_letter(int c) { return c >= 0 && c < kChains ? kLetters[c] : '?'; }
int chain_tier(int c) { return c >= 0 && c < kChains ? kTier[c] : 0; }

void tile_name(int t, char* buf, size_t cap)
{
    if (t < 0 || t >= kTiles) { snprintf(buf, cap, "-"); return; }
    snprintf(buf, cap, "%d%c", t % kCols + 1, 'A' + t / kCols);
}

int price_for(int c, int size)
{
    if (size < 2) return 0;
    int b;
    if (size <= 5) b = size - 2;
    else if (size <= 10) b = 4;
    else if (size <= 20) b = 5;
    else if (size <= 30) b = 6;
    else if (size <= 40) b = 7;
    else b = 8;
    return (2 + b + chain_tier(c)) * 100;
}

uint32_t Game::rand_next()
{
    uint32_t s = rng ? rng : 0x9E3779B9u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    rng = s;
    return s;
}

int Game::size(int c) const
{
    int n = 0;
    for (int t = 0; t < kTiles; ++t) n += board[t] == c + 2;
    return n;
}

int Game::bank(int c) const
{
    int n = kShares;
    for (int i = 0; i < kPlayers; ++i) n -= p[i].shares[c];
    return n;
}

int Game::actor() const { return phase == Phase::Dispose ? disposer : turn; }

int Game::touching(int t, uint8_t* chains) const
{
    int nb[4];
    const int n = neighbours(t, nb);
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const uint8_t v = board[nb[i]];
        if (v < 2) continue;
        bool seen = false;
        for (int j = 0; j < k; ++j) seen |= chains[j] == v - 2;
        if (!seen) chains[k++] = uint8_t(v - 2);
    }
    return k;
}

TileState Game::tile_state(int t) const
{
    if (t < 0 || t >= kTiles || board[t]) return TileState::Dead;
    uint8_t ch[4];
    const int k = touching(t, ch);
    int safe_n = 0;
    for (int i = 0; i < k; ++i) safe_n += safe(ch[i]);
    if (safe_n >= 2) return TileState::Dead;
    if (k == 0) {
        int nb[4];
        const int n = neighbours(t, nb);
        bool loose = false;
        for (int i = 0; i < n; ++i) loose |= board[nb[i]] == 1;
        if (loose) {
            bool free_chain = false;
            for (int c = 0; c < kChains; ++c) free_chain |= !active(c);
            if (!free_chain) return TileState::Wait;
        }
    }
    return TileState::Ok;
}

bool Game::has_playable(int player) const
{
    for (int i = 0; i < kHand; ++i)
        if (p[player].hand[i] != kNone && tile_state(p[player].hand[i]) == TileState::Ok) return true;
    return false;
}

int32_t Game::worth(int player) const
{
    int32_t w = p[player].cash;
    for (int c = 0; c < kChains; ++c) w += p[player].shares[c] * price(c);
    return w;
}

void Game::draw_to_full(int player)
{
    for (int i = 0; i < kHand && bag_n > 0; ++i)
        if (p[player].hand[i] == kNone) p[player].hand[i] = bag[--bag_n];
}

void Game::replace_dead(int player)
{
    for (int i = 0; i < kHand; ++i)
        while (p[player].hand[i] != kNone && tile_state(p[player].hand[i]) == TileState::Dead)
            p[player].hand[i] = bag_n > 0 ? bag[--bag_n] : kNone;     // the new one may be dead too
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 4; ++k) rand_next();
    for (int t = 0; t < kTiles; ++t) bag[t] = uint8_t(t);
    for (int i = kTiles - 1; i > 0; --i) {
        const int j = int(rand_next() % uint32_t(i + 1));
        const uint8_t x = bag[i]; bag[i] = bag[j]; bag[j] = x;
    }
    bag_n = kTiles;
    // Each player lays one tile; the one nearest 1A starts
    int first = 0, best = kTiles;
    for (int i = 0; i < kPlayers; ++i) {
        const uint8_t t = bag[--bag_n];
        board[t] = 1;
        if (t < best) { best = t; first = i; }
    }
    for (int i = 0; i < kPlayers; ++i) draw_to_full(i);
    turn = uint8_t(first);
    begin_turn();
}

void Game::begin_turn()
{
    bought = 0;
    pending = kNone;
    phase = Phase::Play;
    // No tile left anywhere that can go down: the game is over
    bool any = false;
    for (int i = 0; i < kPlayers; ++i) { replace_dead(i); any |= has_playable(i); }
    if (!any && bag_n == 0) { finish(); return; }
}

void Game::fill_from(int t, uint8_t value)
{
    // Flood through loose tiles (and t itself) - small boards: a simple stack
    uint8_t stack[kTiles];
    int sp = 0;
    board[t] = value;
    stack[sp++] = uint8_t(t);
    while (sp) {
        const int x = stack[--sp];
        int nb[4];
        const int n = neighbours(x, nb);
        for (int i = 0; i < n; ++i)
            if (board[nb[i]] == 1) { board[nb[i]] = value; stack[sp++] = uint8_t(nb[i]); }
    }
}

bool Game::play(int slot)
{
    if (phase != Phase::Play || slot < 0 || slot >= kHand) return false;
    const uint8_t t = p[turn].hand[slot];
    if (t == kNone || tile_state(t) != TileState::Ok) return false;
    p[turn].hand[slot] = kNone;
    last_tile = t;
    uint8_t ch[4];
    const int k = touching(t, ch);
    if (k == 0) {
        int nb[4];
        const int n = neighbours(t, nb);
        bool loose = false;
        for (int i = 0; i < n; ++i) loose |= board[nb[i]] == 1;
        board[t] = 1;
        if (loose) { pending = t; phase = Phase::Found; return true; }
        phase = Phase::Buy;
        return true;
    }
    if (k == 1) {
        fill_from(t, uint8_t(ch[0] + 2));
        phase = Phase::Buy;
        return true;
    }
    // A merger
    pending = t;
    board[t] = 1;
    int big = 0;
    for (int i = 0; i < k; ++i) if (size(ch[i]) > big) big = size(ch[i]);
    int tied = 0;
    for (int i = 0; i < k; ++i) tied += size(ch[i]) == big;
    defunct_n = 0;
    for (int i = 0; i < k; ++i) { defunct[defunct_n] = ch[i]; defunct_size[defunct_n] = uint8_t(size(ch[i])); ++defunct_n; }
    if (tied > 1) { survivor = kNone; phase = Phase::Survivor; return true; }
    for (int i = 0; i < k; ++i) if (size(ch[i]) == big) survivor = ch[i];
    begin_merge();
    return true;
}

bool Game::skip_play()
{
    if (phase != Phase::Play || has_playable(turn)) return false;
    phase = Phase::Buy;
    return true;
}

bool Game::found(int c)
{
    if (phase != Phase::Found || c < 0 || c >= kChains || active(c)) return false;
    fill_from(pending, uint8_t(c + 2));
    if (bank(c) > 0) ++p[turn].shares[c];
    pending = kNone;
    phase = Phase::Buy;
    return true;
}

bool Game::choose_survivor(int c)
{
    if (phase != Phase::Survivor) return false;
    int big = 0;
    for (int i = 0; i < defunct_n; ++i) if (defunct_size[i] > big) big = defunct_size[i];
    bool ok = false;
    for (int i = 0; i < defunct_n; ++i) ok |= defunct[i] == c && defunct_size[i] == big;
    if (!ok) return false;
    survivor = uint8_t(c);
    begin_merge();
    return true;
}

// Survivor known: the others are defunct, biggest first
void Game::begin_merge()
{
    uint8_t d[4], ds[4];
    int n = 0;
    for (int i = 0; i < defunct_n; ++i)
        if (defunct[i] != survivor) { d[n] = defunct[i]; ds[n] = defunct_size[i]; ++n; }
    for (int i = 1; i < n; ++i)                        // by size, biggest first
        for (int j = i; j > 0 && ds[j] > ds[j - 1]; --j) {
            uint8_t x = d[j]; d[j] = d[j - 1]; d[j - 1] = x;
            x = ds[j]; ds[j] = ds[j - 1]; ds[j - 1] = x;
        }
    defunct_n = uint8_t(n);
    for (int i = 0; i < 4; ++i) { defunct[i] = i < n ? d[i] : kNone; defunct_size[i] = i < n ? ds[i] : 0; }
    defunct_i = 0;
    phase = Phase::Dispose;
    pay_bonuses(defunct[0], defunct_size[0]);
    disposer = first_holder(defunct[0]);
    if (disposer == kNone) next_defunct();
}

// The first player holding shares of c, from the merging player round the table
uint8_t Game::first_holder(int c) const
{
    for (int k = 0; k < kPlayers; ++k) {
        const int i = (turn + k) % kPlayers;
        if (p[i].shares[c] > 0) return uint8_t(i);
    }
    return kNone;
}

void Game::pay_bonuses(int c, int sz)
{
    const int pr = price_for(c, sz);
    const int major = pr * 10, minor = pr * 5;
    int first = 0, second = 0;
    for (int i = 0; i < kPlayers; ++i) {
        const int s = p[i].shares[c];
        if (s > first) { second = first; first = s; }
        else if (s > second && s < first) second = s;
    }
    if (first == 0) return;
    int n1 = 0, n2 = 0;
    for (int i = 0; i < kPlayers; ++i) { n1 += p[i].shares[c] == first; n2 += second > 0 && p[i].shares[c] == second; }
    if (n1 > 1 || second == 0) {
        // Tied for the most (or one holder alone): both bonuses shared among the first
        const int each = round_up_100((major + minor) / n1);
        for (int i = 0; i < kPlayers; ++i) if (p[i].shares[c] == first) p[i].cash += each;
        return;
    }
    for (int i = 0; i < kPlayers; ++i) if (p[i].shares[c] == first) p[i].cash += major;
    const int each = round_up_100(minor / n2);
    for (int i = 0; i < kPlayers; ++i) if (p[i].shares[c] == second) p[i].cash += each;
}

bool Game::dispose(int sell, int trade)
{
    if (phase != Phase::Dispose || disposer == kNone) return false;
    const int c = defunct[defunct_i];
    Player& pl = p[disposer];
    if (sell < 0 || trade < 0 || (trade & 1) || sell + trade > pl.shares[c] || trade / 2 > bank(survivor)) return false;
    pl.cash += sell * price_for(c, defunct_size[defunct_i]);
    pl.shares[c] = uint8_t(pl.shares[c] - sell - trade);
    pl.shares[survivor] = uint8_t(pl.shares[survivor] + trade / 2);
    // On round the table; back at the merging player = everyone has decided
    const int me = disposer;
    for (int k = 1; k < kPlayers; ++k) {
        const int i = (me + k) % kPlayers;
        if (i == turn) break;
        if (p[i].shares[c] > 0) { disposer = uint8_t(i); return true; }
    }
    disposer = kNone;
    next_defunct();
    return true;
}

void Game::next_defunct()
{
    if (disposer != kNone) return;
    // The defunct chain's hotels join the survivor
    const int c = defunct[defunct_i];
    for (int t = 0; t < kTiles; ++t) if (board[t] == c + 2) board[t] = uint8_t(survivor + 2);
    if (++defunct_i < defunct_n) {
        pay_bonuses(defunct[defunct_i], defunct_size[defunct_i]);
        disposer = first_holder(defunct[defunct_i]);
        if (disposer == kNone) next_defunct();
        return;
    }
    end_merge();
}

void Game::end_merge()
{
    fill_from(pending, uint8_t(survivor + 2));
    pending = kNone;
    for (int i = 0; i < 4; ++i) { defunct[i] = kNone; defunct_size[i] = 0; }
    defunct_n = defunct_i = 0;
    disposer = kNone;
    phase = Phase::Buy;
}

bool Game::buy(int c)
{
    if (phase != Phase::Buy || bought >= kMaxBuy || c < 0 || c >= kChains || !active(c)) return false;
    const int pr = price(c);
    if (bank(c) <= 0 || p[turn].cash < pr) return false;
    p[turn].cash -= pr;
    ++p[turn].shares[c];
    ++bought;
    return true;
}

bool Game::unbuy(int c)
{
    if (phase != Phase::Buy || bought == 0 || c < 0 || c >= kChains || p[turn].shares[c] == 0) return false;
    p[turn].cash += price(c);
    --p[turn].shares[c];
    --bought;
    return true;
}

bool Game::can_end() const
{
    bool any = false, all_safe = true;
    for (int c = 0; c < kChains; ++c) {
        const int s = size(c);
        if (!s) continue;
        any = true;
        if (s >= kEndSize) return true;
        if (s < kSafe) all_safe = false;
    }
    return any && all_safe;
}

bool Game::call_end()
{
    if ((phase != Phase::Buy && phase != Phase::Play) || !can_end()) return false;
    end_called = true;
    return true;
}

bool Game::buy_done()
{
    if (phase != Phase::Buy) return false;
    if (end_called) { finish(); return true; }
    draw_to_full(turn);
    turn = uint8_t((turn + 1) % kPlayers);
    ++turns;
    begin_turn();
    return true;
}

void Game::finish()
{
    for (int c = 0; c < kChains; ++c) {
        const int s = size(c);
        if (!s) continue;
        pay_bonuses(c, s);
        for (int i = 0; i < kPlayers; ++i) { p[i].cash += p[i].shares[c] * price_for(c, s); p[i].shares[c] = 0; }
    }
    phase = Phase::Over;
}

int32_t Game::final_money(int player) const
{
    if (phase == Phase::Over) return p[player].cash;
    Game g = *this;
    g.finish();
    return g.p[player].cash;
}

void Game::ranking(uint8_t* order) const
{
    int32_t m[kPlayers];
    for (int i = 0; i < kPlayers; ++i) { order[i] = uint8_t(i); m[i] = final_money(i); }
    for (int i = 1; i < kPlayers; ++i)
        for (int j = i; j > 0 && m[order[j]] > m[order[j - 1]]; --j) { const uint8_t x = order[j]; order[j] = order[j - 1]; order[j - 1] = x; }
}

// ---- Computer players -------------------------------------------------------------------
namespace {
// What a chain's majority is worth to `me` right now: a share of its bonuses
float stake(const Game& g, int me, int c)
{
    const int mine = g.p[me].shares[c];
    if (!mine || !g.active(c)) return 0;
    int better = 0, equal = 0;
    for (int i = 0; i < kPlayers; ++i) {
        if (i == me) continue;
        better += g.p[i].shares[c] > mine;
        equal += g.p[i].shares[c] == mine;
    }
    const float pr = float(g.price(c));
    if (better == 0) return pr * (equal ? 15.0f / float(equal + 1) : 10.0f);
    if (better == 1) return pr * 5.0f / float(equal + 1);
    return 0;
}

float value_for(const Game& g, int me, int level)
{
    float v = float(g.worth(me));
    if (level >= 2) {
        for (int c = 0; c < kChains; ++c) v += 0.5f * stake(g, me, c);
        float best_other = 0;
        for (int i = 0; i < kPlayers; ++i) if (i != me && g.worth(i) > best_other) best_other = float(g.worth(i));
        v -= 0.3f * best_other;
    }
    return v;
}

// Finish any choices a laid tile asked for, the computer's way
void settle(Game& g, int level)
{
    for (int guard = 0; guard < 32 && g.phase != Phase::Buy && g.phase != Phase::Over && g.phase != Phase::Play; ++guard)
        g.ai_act(level);
}
} // namespace

int Game::ai_tile(int level) const
{
    const int me = turn;
    int slots[kHand], n = 0;
    for (int i = 0; i < kHand; ++i)
        if (p[me].hand[i] != kNone && tile_state(p[me].hand[i]) == TileState::Ok) slots[n++] = i;
    if (!n) return -1;
    if (level == 0) return slots[(rng >> 7) % uint32_t(n)];
    int best = slots[0];
    float bv = -1e30f;
    for (int k = 0; k < n; ++k) {
        Game g = *this;
        g.play(slots[k]);
        settle(g, level);
        const float v = value_for(g, me, level) + float((rng >> (k * 3)) & 7);   // ties: a little noise
        if (v > bv) { bv = v; best = slots[k]; }
    }
    return best;
}

int Game::ai_found(int level) const
{
    // Dear chains are worth more per share; Easy picks any
    int best = -1;
    for (int c = 0; c < kChains; ++c) {
        if (active(c)) continue;
        if (level == 0 && best >= 0 && ((rng >> c) & 1)) continue;
        if (best < 0 || chain_tier(c) > chain_tier(best) || level == 0) best = c;
    }
    return best;
}

int Game::ai_survivor(int level) const
{
    int big = 0;
    for (int i = 0; i < defunct_n; ++i) if (defunct_size[i] > big) big = defunct_size[i];
    int best = -1, bs = -1;
    for (int i = 0; i < defunct_n; ++i) {
        if (defunct_size[i] != big) continue;
        const int c = defunct[i];
        // Keep the one I hold most of (my shares of the others pay bonuses and trade)
        const int s = level == 0 ? 0 : p[turn].shares[c] * 10 + chain_tier(c);
        if (s > bs) { bs = s; best = c; }
    }
    return best;
}

void Game::ai_dispose(int level, int& sell, int& trade) const
{
    const int me = disposer, c = defunct[defunct_i];
    const int have = p[me].shares[c];
    sell = have; trade = 0;
    if (level == 0) return;
    const int pd = price_for(c, defunct_size[defunct_i]), ps = price(survivor);
    // Two old shares for one in the survivor when the survivor's share is worth more
    if (ps > 2 * pd || (level >= 2 && ps * 2 >= 3 * pd)) {
        trade = have / 2 * 2;
        if (trade / 2 > bank(survivor)) trade = bank(survivor) * 2;
        sell = have - trade;
    }
}

int Game::ai_buy(int level) const
{
    const int me = turn;
    if (bought >= kMaxBuy) return -1;
    const int cash = p[me].cash;
    int best = -1;
    float bv = 0;
    for (int c = 0; c < kChains; ++c) {
        if (!active(c) || bank(c) <= 0) continue;
        const int pr = price(c);
        if (pr > cash) continue;
        if (level == 0) {
            if (((rng >> (c + bought * 3)) & 3) == 0 && (best < 0)) best = c;
            continue;
        }
        // Keep money back for later turns: a reserve that shrinks as the game goes on
        const int reserve = level >= 2 ? (turns < 40 ? 1200 : 400) : 800;
        if (cash - pr < reserve) continue;
        const int mine = p[me].shares[c];
        int top = 0;
        for (int i = 0; i < kPlayers; ++i) if (i != me && p[i].shares[c] > top) top = p[i].shares[c];
        float v = 0;
        if (mine + 1 > top) v += 3;                     // takes or keeps the lead
        else if (mine + 1 == top) v += 2;               // draws level
        else if (top - mine <= 2) v += 1;               // still in the race
        if (safe(c)) v += level >= 2 ? 0.5f : 0;        // safe chains keep their value
        else v += 1.5f;                                 // small chains can still grow
        v += float(12 - size(c) > 0 ? 12 - size(c) : 0) * 0.08f;
        v -= float(pr) / 1000.0f;
        if (v > bv + 0.01f) { bv = v; best = c; }
    }
    return bv >= (level >= 2 ? 1.5f : 1.0f) || level == 0 ? best : -1;
}

bool Game::ai_end(int level) const
{
    (void)level;
    if (!can_end()) return false;
    uint8_t order[kPlayers];
    ranking(order);
    return order[0] == turn;
}

void Game::ai_act(int level)
{
    switch (phase) {
        case Phase::Play: {
            const int s = ai_tile(level);
            if (s < 0) skip_play(); else play(s);
            break;
        }
        case Phase::Found:    found(ai_found(level)); break;
        case Phase::Survivor: choose_survivor(ai_survivor(level)); break;
        case Phase::Dispose: {
            int sell = 0, trade = 0;
            ai_dispose(level, sell, trade);
            if (!dispose(sell, trade)) dispose(p[disposer].shares[defunct[defunct_i]], 0);
            break;
        }
        case Phase::Buy: {
            if (!end_called && ai_end(level)) call_end();
            const int c = ai_buy(level);
            if (c < 0 || !buy(c)) buy_done();
            break;
        }
        case Phase::Over: break;
    }
}

// ---- Save ----------------------------------------------------------------------------------
namespace {
void put32(uint8_t* b, size_t& n, uint32_t v) { for (int k = 0; k < 4; ++k) b[n++] = uint8_t(v >> (8 * k)); }
uint32_t get32(const uint8_t* b, size_t& n) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(b[n++]) << (8 * k); return v; }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "ACQ1", 4); n = 4;
    memcpy(buf + n, board, kTiles); n += kTiles;
    memcpy(buf + n, bag, kTiles); n += kTiles;
    buf[n++] = bag_n;
    for (int i = 0; i < kPlayers; ++i) {
        put32(buf, n, uint32_t(p[i].cash));
        memcpy(buf + n, p[i].shares, kChains); n += kChains;
        memcpy(buf + n, p[i].hand, kHand); n += kHand;
    }
    buf[n++] = turn;
    buf[n++] = uint8_t(phase);
    buf[n++] = pending;
    buf[n++] = survivor;
    memcpy(buf + n, defunct, 4); n += 4;
    memcpy(buf + n, defunct_size, 4); n += 4;
    buf[n++] = defunct_n;
    buf[n++] = defunct_i;
    buf[n++] = disposer;
    buf[n++] = bought;
    buf[n++] = end_called ? 1 : 0;
    buf[n++] = last_tile;
    buf[n++] = uint8_t(turns); buf[n++] = uint8_t(turns >> 8);
    put32(buf, n, rng);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "ACQ1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    memcpy(g.board, buf + n, kTiles); n += kTiles;
    for (int t = 0; t < kTiles; ++t) if (g.board[t] > kChains + 1) return false;
    memcpy(g.bag, buf + n, kTiles); n += kTiles;
    g.bag_n = buf[n++];
    if (g.bag_n > kTiles) return false;
    for (int i = 0; i < g.bag_n; ++i) if (g.bag[i] >= kTiles) return false;
    for (int i = 0; i < kPlayers; ++i) {
        g.p[i].cash = int32_t(get32(buf, n));
        memcpy(g.p[i].shares, buf + n, kChains); n += kChains;
        memcpy(g.p[i].hand, buf + n, kHand); n += kHand;
        for (int k = 0; k < kHand; ++k) if (g.p[i].hand[k] != kNone && g.p[i].hand[k] >= kTiles) return false;
    }
    for (int c = 0; c < kChains; ++c) if (g.bank(c) < 0) return false;
    g.turn = buf[n++];
    if (g.turn >= kPlayers || buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.pending = buf[n++];
    g.survivor = buf[n++];
    memcpy(g.defunct, buf + n, 4); n += 4;
    memcpy(g.defunct_size, buf + n, 4); n += 4;
    g.defunct_n = buf[n++];
    g.defunct_i = buf[n++];
    g.disposer = buf[n++];
    g.bought = buf[n++];
    g.end_called = buf[n++] != 0;
    g.last_tile = buf[n++];
    g.turns = uint16_t(buf[n] | (buf[n + 1] << 8)); n += 2;
    g.rng = get32(buf, n);
    if (g.pending != kNone && g.pending >= kTiles) return false;
    if (g.survivor != kNone && g.survivor >= kChains) return false;
    if (g.defunct_n > 4 || g.defunct_i > g.defunct_n || g.bought > kMaxBuy) return false;
    for (int i = 0; i < 4; ++i) if (g.defunct[i] != kNone && g.defunct[i] >= kChains) return false;
    if (g.disposer != kNone && g.disposer >= kPlayers) return false;
    if (g.last_tile != kNone && g.last_tile >= kTiles) return false;
    if ((g.phase == Phase::Found || g.phase == Phase::Survivor || g.phase == Phase::Dispose) && g.pending == kNone) return false;
    if (g.phase == Phase::Dispose && (g.disposer == kNone || g.survivor == kNone || g.defunct_i >= g.defunct_n)) return false;
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Money,Level,Seconds,Time";

namespace {
const char* const kLevelNames[3] = {"Easy", "Medium", "Hard"};
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%ld,%s,%lu,%s\n", unsigned(r.place), long(r.money),
                           kLevelNames[r.level < 3 ? r.level : 0], (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    long money = 0;
    unsigned place = 0;
    char lv[12] = {}, tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%ld,%11[^,],%lu,%15[^,\r\n]", &seq, &place, &money, lv, &secs, tm) != 6) return false;
    out.place = uint8_t(place);
    out.money = int32_t(money);
    out.level = 0;
    for (int i = 0; i < 3; ++i) if (!strcmp(lv, kLevelNames[i])) out.level = uint8_t(i);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    wins += r.place == 1;
    if (r.money > best) best = r.money;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace acq
