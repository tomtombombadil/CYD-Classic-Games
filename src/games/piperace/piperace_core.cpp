#include "piperace_core.h"

#include <cstdio>
#include <cstring>

namespace piperace {

namespace {

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

// The square next to c on `side`, or -1 off the grid
int neighbour(int c, uint8_t side)
{
    const int r = c / kCols, k = c % kCols;
    switch (side) {
        case kN: return r > 0 ? c - kCols : -1;
        case kS: return r < kRows - 1 ? c + kCols : -1;
        case kE: return k < kCols - 1 ? c + 1 : -1;
        case kW: return k > 0 ? c - 1 : -1;
    }
    return -1;
}

constexpr int kPointsPipe = 100, kPointsCross = 400, kPenalty = 50;

} // namespace

uint8_t sides(uint8_t p)
{
    switch (p) {
        case kAcross: return kE | kW;
        case kUpDown: return kN | kS;
        case kNE:     return kN | kE;
        case kES:     return kE | kS;
        case kSW:     return kS | kW;
        case kWN:     return kW | kN;
        case kCross:  return kN | kE | kS | kW;
        case kStartN: return kN;
        case kStartE: return kE;
        case kStartS: return kS;
        case kStartW: return kW;
    }
    return 0;
}

uint32_t Game::rand_next()
{
    uint32_t s = rng ? rng : 0x9E3779B9u;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    rng = s;
    return s;
}

// Bends and straights alike, crosses rarer: 2 each, cross 1
uint8_t Game::random_piece()
{
    static const uint8_t kBag[13] = {kAcross, kAcross, kUpDown, kUpDown, kNE, kNE, kES, kES,
                                     kSW, kSW, kWN, kWN, kCross};
    return kBag[rand_next() % 13];
}

int Game::goal() const { const int g = 8 + 2 * level; return g > 26 ? 26 : g; }

uint32_t Game::wait_total() const
{
    const int t = 25000 - 1500 * (level - 1);
    return uint32_t(t < 10000 ? 10000 : t);
}

uint32_t Game::cell_ms() const
{
    float t = 3200.0f;
    for (int i = 1; i < level && t > 700.0f; ++i) t *= 0.88f;
    return uint32_t(t < 700.0f ? 700.0f : t);
}

uint32_t Game::head_total() const
{
    uint32_t t = cell_ms();
    if (head >= 0 && is_start(cell[head])) t /= 2;       // from the middle of the tank to its outlet
    if (fast) { t /= 8; if (t < 40) t = 40; }
    return t;
}

int Game::start_cell() const
{
    for (int i = 0; i < kCells; ++i) if (is_start(cell[i])) return i;
    return -1;
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 4; ++k) rand_next();
    level = 0;                     // level 1 is laid out like every later one
    next_level();
}

void Game::next_level()
{
    ++level;
    memset(cell, 0, sizeof cell);
    memset(fill, 0, sizeof fill);
    phase = Phase::Waiting;
    pipes = 0;
    fast = false;
    head = -1;
    in = 0;
    head_ms = 0;
    wait_ms = wait_total();
    // The tank: away from the edges, its outlet pointing anywhere
    const int r = 1 + int(rand_next() % (kRows - 2)), k = 1 + int(rand_next() % (kCols - 2));
    const int s = r * kCols + k;
    const uint8_t out = uint8_t(kStartN + rand_next() % 4);
    cell[s] = out;
    const int front = neighbour(s, sides(out));
    // Rocks from level 3: one more each level, up to 8, never in front of the outlet
    int rocks = level >= 3 ? level - 2 : 0;
    if (rocks > 8) rocks = 8;
    for (int guard = 0; rocks > 0 && guard < 200; ++guard) {
        const int c = int(rand_next() % kCells);
        if (cell[c] != kEmpty || c == front) continue;
        cell[c] = kRock;
        --rocks;
    }
    for (int i = 0; i < kQueue; ++i) queue[i] = random_piece();
    head = int8_t(s);
}

bool Game::can_tap(int c) const
{
    if (c < 0 || c >= kCells) return false;
    if (phase != Phase::Waiting && phase != Phase::Flowing) return false;
    const uint8_t p = cell[c];
    if (p == kEmpty) return true;
    if (!is_pipe(p)) return false;
    if (fill[c]) return false;                                       // water has been through
    if (phase == Phase::Flowing && c == head) return false;          // water is in it
    return true;
}

int Game::tap(int c)
{
    if (!can_tap(c)) return 0;
    const bool swap = cell[c] != kEmpty;
    cell[c] = queue[0];
    for (int i = 0; i + 1 < kQueue; ++i) queue[i] = queue[i + 1];
    queue[kQueue - 1] = random_piece();
    if (swap) { score -= kPenalty; if (score < 0) score = 0; }
    return swap ? 2 : 1;
}

void Game::go()
{
    if (phase == Phase::Waiting) wait_ms = 0;
    else if (phase == Phase::Flowing) fast = true;
}

void Game::end_level()
{
    // Pipes laid but never reached cost points
    for (int i = 0; i < kCells; ++i)
        if (is_pipe(cell[i]) && !fill[i]) score -= kPenalty;
    if (score < 0) score = 0;
    phase = pipes >= goal() ? Phase::Passed : Phase::Over;
}

uint32_t Game::advance(uint32_t ms)
{
    uint32_t ev = 0;
    while (ms > 0 && (phase == Phase::Waiting || phase == Phase::Flowing)) {
        if (phase == Phase::Waiting) {
            if (ms < wait_ms) { wait_ms -= ms; return ev; }
            ms -= wait_ms;
            wait_ms = 0;
            phase = Phase::Flowing;
            head_ms = 0;
            ev |= kEvFlow;
            continue;
        }
        const uint32_t t = head_total();
        if (head_ms + ms < t) { head_ms = uint16_t(head_ms + ms); return ev; }
        ms -= (t - head_ms);
        head_ms = 0;
        // The water leaves the head square
        const uint8_t p = cell[head];
        uint8_t out;
        if (is_start(p)) out = sides(p);
        else {
            if (p == kCross) {
                fill[head] |= (in & (kE | kW)) ? 1 : 2;
                const uint8_t both = 3;
                if ((fill[head] & both) == both) { score += kPointsCross; ev |= kEvCrossBonus; }
            } else {
                fill[head] |= 1;
            }
            score += fast ? 2 * kPointsPipe : kPointsPipe;
            ++pipes;
            ++total;
            ev |= kEvFilled;
            out = p == kCross ? opposite(in) : uint8_t(sides(p) & ~in);
        }
        const int n = neighbour(head, out);
        const uint8_t back = opposite(out);
        bool ok = n >= 0 && is_pipe(cell[n]) && (sides(cell[n]) & back);
        if (ok) {
            if (cell[n] == kCross) ok = !(fill[n] & ((back & (kE | kW)) ? 1 : 2));
            else ok = !fill[n];
        }
        if (!ok) {
            end_level();
            ev |= phase == Phase::Passed ? kEvPassed : kEvOver;
            return ev;
        }
        head = int8_t(n);
        in = back;
    }
    return ev;
}

// ---- Save ----------------------------------------------------------------------------------
namespace {
void put32(uint8_t* b, size_t& n, uint32_t v) { for (int k = 0; k < 4; ++k) b[n++] = uint8_t(v >> (8 * k)); }
void put16(uint8_t* b, size_t& n, uint16_t v) { b[n++] = uint8_t(v); b[n++] = uint8_t(v >> 8); }
uint32_t get32(const uint8_t* b, size_t& n) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(b[n++]) << (8 * k); return v; }
uint16_t get16(const uint8_t* b, size_t& n) { const uint16_t v = uint16_t(b[n] | (b[n + 1] << 8)); n += 2; return v; }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "PRC1", 4); n = 4;
    memcpy(buf + n, cell, kCells); n += kCells;
    memcpy(buf + n, fill, kCells); n += kCells;
    memcpy(buf + n, queue, kQueue); n += kQueue;
    buf[n++] = level;
    buf[n++] = uint8_t(phase);
    put32(buf, n, uint32_t(score));
    put16(buf, n, pipes);
    put16(buf, n, total);
    put32(buf, n, wait_ms);
    buf[n++] = uint8_t(head);
    buf[n++] = in;
    put16(buf, n, head_ms);
    buf[n++] = fast ? 1 : 0;
    put32(buf, n, rng);
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "PRC1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    int starts = 0;
    for (int i = 0; i < kCells; ++i) {
        if (buf[n + i] >= kPieces) return false;
        g.cell[i] = buf[n + i];
        starts += is_start(g.cell[i]);
    }
    if (starts != 1) return false;
    n += kCells;
    for (int i = 0; i < kCells; ++i) { if (buf[n + i] > 3) return false; g.fill[i] = buf[n + i]; }
    n += kCells;
    for (int i = 0; i < kQueue; ++i) { if (!is_pipe(buf[n + i])) return false; g.queue[i] = buf[n + i]; }
    n += kQueue;
    g.level = buf[n++];
    if (g.level < 1) return false;
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.score = int32_t(get32(buf, n));
    g.pipes = get16(buf, n);
    g.total = get16(buf, n);
    g.wait_ms = get32(buf, n);
    g.head = int8_t(buf[n++]);
    g.in = buf[n++];
    g.head_ms = get16(buf, n);
    g.fast = buf[n++] != 0;
    g.rng = get32(buf, n);
    if (g.head < 0 || g.head >= kCells) return false;
    if (!is_start(g.cell[g.head]) && !is_pipe(g.cell[g.head])) return false;
    if (g.in != 0 && g.in != kN && g.in != kE && g.in != kS && g.in != kW) return false;
    if (g.wait_ms > 60000) return false;
    *this = g;
    return true;
}

// ---- History ------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Score,Level,Pipes,Seconds,Time";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%ld,%u,%u,%lu,%s\n", (long)r.score, (unsigned)r.level,
                           (unsigned)r.pipes, (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    long score = 0;
    unsigned level = 0, pipes = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%ld,%u,%u,%lu,%15[^,\r\n]", &seq, &score, &level, &pipes, &secs, tm) != 6)
        return false;
    out.score = int32_t(score);
    out.level = uint16_t(level);
    out.pipes = uint16_t(pipes);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    total += r.score;
    if (r.score > best) best = r.score;
    if (r.level > best_level) best_level = r.level;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace piperace
