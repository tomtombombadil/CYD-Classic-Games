#include "dealcyd_core.h"

#include <cstdio>
#include <cstring>

namespace dealcyd {

const uint32_t kValues[kCases] = {
    1, 100, 500, 1000, 2500, 5000, 7500, 10000, 20000, 30000, 40000, 50000, 75000,
    100000, 500000, 1000000, 2500000, 5000000, 7500000, 10000000, 20000000, 30000000,
    40000000, 50000000, 75000000, 100000000,
};
const uint8_t kOpenPerRound[kRounds] = {6, 5, 4, 3, 2, 1, 1, 1, 1};

namespace {
const uint8_t kShare[kRounds] = {12, 22, 32, 42, 52, 62, 72, 82, 92};   // offer = this % of the average

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
}

void money(char* buf, size_t cap, uint32_t cents, bool short_form)
{
    const uint32_t d = cents / 100;
    if (cents < 100) { snprintf(buf, cap, "$0.%02u", unsigned(cents)); return; }
    if (short_form && d >= 1000000) { snprintf(buf, cap, "$%luM", (unsigned long)(d / 1000000)); return; }
    if (short_form && d >= 1000) { snprintf(buf, cap, "$%luK", (unsigned long)(d / 1000)); return; }
    if (d >= 1000000) snprintf(buf, cap, "$%lu,%03lu,%03lu", (unsigned long)(d / 1000000), (unsigned long)(d / 1000 % 1000), (unsigned long)(d % 1000));
    else if (d >= 1000) snprintf(buf, cap, "$%lu,%03lu", (unsigned long)(d / 1000), (unsigned long)(d % 1000));
    else snprintf(buf, cap, "$%lu", (unsigned long)d);
}

uint32_t Game::rand_next()
{
    uint32_t s = rng ? rng : 0x9E3779B9u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    rng = s;
    return s;
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 4; ++k) rand_next();
    for (int i = 0; i < kCases; ++i) value_of[i] = uint8_t(i);
    for (int i = kCases - 1; i > 0; --i) {
        const int j = int(rand_next() % uint32_t(i + 1));
        const uint8_t t = value_of[i]; value_of[i] = value_of[j]; value_of[j] = t;
    }
}

bool Game::pick(int c)
{
    if (phase != Phase::Pick || c < 0 || c >= kCases) return false;
    mine = int8_t(c);
    round = 0;
    to_open = kOpenPerRound[0];
    phase = Phase::Open;
    return true;
}

bool Game::open(int c)
{
    if (phase != Phase::Open || c < 0 || c >= kCases || c == mine || opened[c]) return false;
    opened[c] = true;
    last_opened = int8_t(c);
    if (--to_open == 0) make_offer();
    return true;
}

bool Game::in_play(int v) const
{
    for (int i = 0; i < kCases; ++i) if (value_of[i] == v) return !opened[i];
    return false;
}

int Game::left() const
{
    int n = 0;
    for (int i = 0; i < kCases; ++i) n += !opened[i];
    return n;
}

uint32_t Game::average() const
{
    uint64_t sum = 0;
    int n = 0;
    for (int i = 0; i < kCases; ++i) if (!opened[i]) { sum += kValues[value_of[i]]; ++n; }
    return n ? uint32_t(sum / uint64_t(n)) : 0;
}

void Game::make_offer()
{
    // A share of the average, a little up or down, rounded to a tidy figure
    const int jitter = int(rand_next() % 11) - 5;                 // -5 .. +5 %
    uint64_t o = uint64_t(average()) * uint64_t(kShare[round] + jitter) / 100;
    uint64_t step = o < 100000 ? 1000 : o < 1000000 ? 10000 : 100000;   // $10 / $100 / $1,000
    o = (o + step / 2) / step * step;
    if (o < 100) o = 100;
    offer = uint32_t(o);
    offers[round] = offer;
    phase = Phase::Offer;
}

bool Game::deal()
{
    if (phase != Phase::Offer) return false;
    won = offer;
    dealt = int8_t(round);
    phase = Phase::Done;
    return true;
}

bool Game::no_deal()
{
    if (phase != Phase::Offer) return false;
    if (round + 1 >= kRounds) { phase = Phase::Swap; return true; }
    ++round;
    to_open = kOpenPerRound[round];
    phase = Phase::Open;
    return true;
}

int Game::last_case() const
{
    for (int i = 0; i < kCases; ++i) if (!opened[i] && i != mine) return i;
    return -1;
}

bool Game::keep_or_swap(bool swap)
{
    if (phase != Phase::Swap) return false;
    const int other = last_case();
    if (other < 0) return false;
    swapped = swap;
    const int held = swap ? other : mine;
    won = kValues[value_of[held]];
    phase = Phase::Done;
    return true;
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
    memcpy(buf, "DNC1", 4); n = 4;
    memcpy(buf + n, value_of, kCases); n += kCases;
    uint32_t bits = 0;
    for (int i = 0; i < kCases; ++i) if (opened[i]) bits |= 1u << i;
    put32(buf, n, bits);
    buf[n++] = uint8_t(mine);
    buf[n++] = round;
    buf[n++] = to_open;
    buf[n++] = uint8_t(phase);
    put32(buf, n, offer);
    put32(buf, n, won);
    buf[n++] = uint8_t(dealt);
    buf[n++] = swapped ? 1 : 0;
    buf[n++] = uint8_t(last_opened);
    for (int r = 0; r < kRounds; ++r) put32(buf, n, offers[r]);
    put32(buf, n, rng);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "DNC1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    bool seen[kCases] = {};
    for (int i = 0; i < kCases; ++i) {
        const uint8_t v = buf[n + i];
        if (v >= kCases || seen[v]) return false;
        seen[v] = true;
        g.value_of[i] = v;
    }
    n += kCases;
    const uint32_t bits = get32(buf, n);
    for (int i = 0; i < kCases; ++i) g.opened[i] = bits >> i & 1;
    g.mine = int8_t(buf[n++]);
    g.round = buf[n++];
    g.to_open = buf[n++];
    if (buf[n] > uint8_t(Phase::Done)) return false;
    g.phase = Phase(buf[n++]);
    g.offer = get32(buf, n);
    g.won = get32(buf, n);
    g.dealt = int8_t(buf[n++]);
    g.swapped = buf[n++] != 0;
    g.last_opened = int8_t(buf[n++]);
    for (int r = 0; r < kRounds; ++r) g.offers[r] = get32(buf, n);
    g.rng = get32(buf, n);
    if (g.mine < -1 || g.mine >= kCases || g.round >= kRounds || g.to_open > 6) return false;
    if (g.phase != Phase::Pick && (g.mine < 0 || g.opened[g.mine])) return false;
    if (g.last_opened < -1 || g.last_opened >= kCases || g.dealt < -1 || g.dealt >= kRounds) return false;
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Won,Deal Round,Case Held,Seconds,Time";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%lu.%02lu,%d,%lu.%02lu,%lu,%s\n", (unsigned long)(r.won / 100), (unsigned long)(r.won % 100),
                           r.dealt >= 0 ? r.dealt + 1 : 0, (unsigned long)(r.held / 100), (unsigned long)(r.held % 100),
                           (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, w1 = 0, w2 = 0, h1 = 0, h2 = 0, secs = 0;
    int dealt = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%lu.%lu,%d,%lu.%lu,%lu,%15[^,\r\n]", &seq, &w1, &w2, &dealt, &h1, &h2, &secs, tm) != 8) return false;
    out.won = uint32_t(w1 * 100 + w2);
    out.dealt = int8_t(dealt > 0 ? dealt - 1 : -1);
    out.held = uint32_t(h1 * 100 + h2);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    deals += r.dealt >= 0;
    total += r.won;
    if (r.won > best) best = r.won;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace dealcyd
