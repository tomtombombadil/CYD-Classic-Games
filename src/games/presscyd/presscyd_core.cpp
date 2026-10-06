#include "presscyd_core.h"

#include <cstdio>
#include <cstring>

namespace presscyd {

namespace {
const uint16_t kMoney1[] = {250, 300, 400, 500, 500, 600, 700, 750, 750, 800, 1000, 1000, 1250, 1500, 2000};
const uint16_t kMoney2[] = {500, 750, 1000, 1000, 1250, 1500, 1500, 2000, 2500, 2500, 3000, 4000, 5000};
const uint16_t kSpin1[] = {500, 750, 1000};
const uint16_t kSpin2[] = {1000, 1500, 2000};
const int kGremlinSlots[kRounds] = {9, 12};
const int kSpinSlots[kRounds] = {7, 6};

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
}

uint32_t Game::rand_next()
{
    uint32_t s = rng ? rng : 0x9E3779B9u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    rng = s;
    return s;
}

void Game::make_board()
{
    const bool r2 = round == 1;
    for (int q = 0; q < kSquares; ++q)
        for (int k = 0; k < kSlots; ++k) {
            Slot& s = board[q][k];
            s.kind = kMoney;
            s.dollars = r2 ? kMoney2[rand_next() % (sizeof kMoney2 / sizeof kMoney2[0])]
                           : kMoney1[rand_next() % (sizeof kMoney1 / sizeof kMoney1[0])];
        }
    // Gremlins: at most one a square; then the spin squares
    int placed = 0;
    for (int guard = 0; placed < kGremlinSlots[round] && guard < 1000; ++guard) {
        const int q = int(rand_next() % kSquares), k = int(rand_next() % kSlots);
        bool has = false;
        for (int j = 0; j < kSlots; ++j) has |= board[q][j].kind == kGremlin;
        if (has) continue;
        board[q][k].kind = kGremlin;
        board[q][k].dollars = 0;
        ++placed;
    }
    placed = 0;
    for (int guard = 0; placed < kSpinSlots[round] && guard < 1000; ++guard) {
        const int q = int(rand_next() % kSquares), k = int(rand_next() % kSlots);
        if (board[q][k].kind != kMoney) continue;
        board[q][k].kind = kMoneySpin;
        board[q][k].dollars = r2 ? kSpin2[rand_next() % 3] : kSpin1[rand_next() % 3];
        ++placed;
    }
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 4; ++k) rand_next();
    round = 0;
    for (auto& pl : p) pl.earned = kSpins[0];
    make_board();
    phase = Phase::Ready;
    next_turn();
}

void Game::next_round()
{
    if (phase != Phase::RoundOver) return;
    if (round + 1 >= kRounds) { phase = Phase::Over; return; }
    ++round;
    for (auto& pl : p) { pl.passed = 0; pl.earned = pl.out() ? 0 : kSpins[round]; }
    make_board();
    phase = Phase::Ready;
    last = Landing{};
    last_player = -1;
    passed_to = 0xFF;
    next_turn();
}

// The player with spins and the least money goes next (ties: the first seat)
void Game::next_turn()
{
    int best = -1;
    for (int i = 0; i < kPlayers; ++i) {
        if (p[i].out() || p[i].spins() == 0) continue;
        if (best < 0 || p[i].money < p[best].money) best = i;
    }
    if (best < 0) {
        phase = round + 1 >= kRounds ? Phase::Over : Phase::RoundOver;
        return;
    }
    turn = uint8_t(best);
    phase = Phase::Ready;
}

int Game::pass_target() const
{
    int best = -1;
    for (int i = 0; i < kPlayers; ++i) {
        if (i == turn || p[i].out()) continue;
        if (best < 0 || p[i].money > p[best].money) best = i;
    }
    return best;
}

bool Game::can_pass() const
{
    if (phase != Phase::Ready) return false;
    const Player& me = p[turn];
    if (me.passed > 0 || me.earned == 0) return false;      // passed spins must be played
    const int t = pass_target();
    return t >= 0 && p[t].money > me.money;
}

bool Game::spin()
{
    if (phase != Phase::Ready || p[turn].spins() == 0) return false;
    phase = Phase::Spinning;
    return true;
}

bool Game::pass()
{
    if (!can_pass()) return false;
    const int t = pass_target();
    p[t].passed = uint8_t(p[t].passed + p[turn].earned);
    p[turn].earned = 0;
    passed_to = uint8_t(t);
    // The one they went to takes them now
    turn = uint8_t(t);
    phase = Phase::Ready;
    return true;
}

bool Game::stop(int square, int slot)
{
    if (phase != Phase::Spinning || square < 0 || square >= kSquares || slot < 0 || slot >= kSlots) return false;
    Player& me = p[turn];
    if (me.passed) --me.passed; else --me.earned;
    const Slot s = board[square][slot];
    last.square = square;
    last.slot = s;
    last_player = int8_t(turn);
    passed_to = 0xFF;
    switch (s.kind) {
        case kMoney: me.money += s.dollars; break;
        case kMoneySpin: me.money += s.dollars; ++me.earned; break;
        case kGremlin:
            me.money = 0;
            ++me.gremlins;
            me.earned = uint8_t(me.earned + me.passed);       // passed spins become yours
            me.passed = 0;
            if (me.out()) me.earned = 0;
            break;
    }
    phase = Phase::Ready;
    if (me.spins() == 0 || me.out()) next_turn();
    return true;
}

int Game::leader() const
{
    int best = 0, n = 0;
    for (int i = 0; i < kPlayers; ++i) {
        if (p[i].money > p[best].money) { best = i; n = 1; }
        else if (p[i].money == p[best].money) ++n;
    }
    return n > 1 ? -1 : best;
}

void Game::ranking(uint8_t* order) const
{
    for (int i = 0; i < kPlayers; ++i) order[i] = uint8_t(i);
    for (int i = 1; i < kPlayers; ++i)
        for (int j = i; j > 0 && p[order[j]].money > p[order[j - 1]].money; --j) {
            const uint8_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
        }
}

float gremlin_odds(const Game& g)
{
    int n = 0;
    for (int q = 0; q < kSquares; ++q) for (int k = 0; k < kSlots; ++k) n += g.board[q][k].kind == kGremlin;
    return float(n) / float(kSquares * kSlots);
}

bool Game::ai_pass(int level) const
{
    if (!can_pass() || level == 0) return false;
    const Player& me = p[turn];
    if (level == 1) return me.money >= 3000 && me.earned <= 2;
    // Hard: pass when a spin is worth less than nothing to keep - the money at
    // risk outweighs what a spin brings in - unless too far behind to catch up
    const float pg = gremlin_odds(*this);
    float avg = 0;
    int n = 0;
    for (int q = 0; q < kSquares; ++q)
        for (int k = 0; k < kSlots; ++k)
            if (board[q][k].kind != kGremlin) { avg += board[q][k].dollars; ++n; }
    avg = n ? avg / float(n) : 0;
    const float ev = (1 - pg) * avg - pg * float(me.money);
    const int t = pass_target();
    const int32_t gap = t >= 0 ? p[t].money - me.money : 0;
    const bool can_catch = float(gap) < float(me.earned) * avg * 0.9f + 1;
    return ev < 0 && (round == 0 || !can_catch || me.money * 3 >= p[t].money * 2);
}

// ---- Save ----------------------------------------------------------------------------------
namespace {
void put32(uint8_t* b, size_t& n, uint32_t v) { for (int k = 0; k < 4; ++k) b[n++] = uint8_t(v >> (8 * k)); }
uint32_t get32(const uint8_t* b, size_t& n) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(b[n++]) << (8 * k); return v; }
uint16_t pack(const Slot& s) { return uint16_t(s.kind | (s.dollars / 50) << 2); }
bool unpack(uint16_t v, Slot& s) { s.kind = uint8_t(v & 3); s.dollars = uint16_t((v >> 2) * 50); return s.kind <= kGremlin; }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "PYC1", 4); n = 4;
    for (int q = 0; q < kSquares; ++q)
        for (int k = 0; k < kSlots; ++k) { const uint16_t v = pack(board[q][k]); buf[n++] = uint8_t(v); buf[n++] = uint8_t(v >> 8); }
    for (int i = 0; i < kPlayers; ++i) {
        put32(buf, n, uint32_t(p[i].money));
        buf[n++] = p[i].earned; buf[n++] = p[i].passed; buf[n++] = p[i].gremlins;
    }
    buf[n++] = round;
    buf[n++] = turn;
    buf[n++] = uint8_t(phase == Phase::Spinning ? Phase::Ready : phase);   // a spin in progress starts again
    buf[n++] = uint8_t(last_player);
    buf[n++] = uint8_t(last.square);
    const uint16_t lv = pack(last.slot);
    buf[n++] = uint8_t(lv); buf[n++] = uint8_t(lv >> 8);
    buf[n++] = passed_to;
    put32(buf, n, rng);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "PYC1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    for (int q = 0; q < kSquares; ++q)
        for (int k = 0; k < kSlots; ++k) {
            const uint16_t v = uint16_t(buf[n] | buf[n + 1] << 8);
            n += 2;
            if (!unpack(v, g.board[q][k])) return false;
        }
    for (int i = 0; i < kPlayers; ++i) {
        g.p[i].money = int32_t(get32(buf, n));
        g.p[i].earned = buf[n++]; g.p[i].passed = buf[n++]; g.p[i].gremlins = buf[n++];
        if (g.p[i].gremlins > kOut || g.p[i].money < 0 || g.p[i].spins() > 40) return false;
    }
    g.round = buf[n++];
    g.turn = buf[n++];
    if (g.round >= kRounds || g.turn >= kPlayers || buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.last_player = int8_t(buf[n++]);
    g.last.square = int8_t(buf[n++]);
    const uint16_t lv = uint16_t(buf[n] | buf[n + 1] << 8);
    n += 2;
    if (!unpack(lv, g.last.slot)) return false;
    g.passed_to = buf[n++];
    g.rng = get32(buf, n);
    if (g.last_player < -1 || g.last_player >= kPlayers || g.last.square < -1 || g.last.square >= kSquares) return false;
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Money,Seconds,Time";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%ld,%lu,%s\n", unsigned(r.place), long(r.money), (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    long money = 0;
    unsigned place = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%ld,%lu,%15[^,\r\n]", &seq, &place, &money, &secs, tm) != 5) return false;
    out.place = uint8_t(place);
    out.money = int32_t(money);
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

} // namespace presscyd
