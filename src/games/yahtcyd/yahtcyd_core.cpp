#include "yahtcyd_core.h"

#include <cstdio>
#include <cstring>

namespace yahtcyd {

namespace {

void counts(const uint8_t d[5], int c[7])
{
    for (int k = 0; k < 7; ++k) c[k] = 0;
    for (int k = 0; k < 5; ++k) ++c[d[k]];
}

int sum(const uint8_t d[5]) { int s = 0; for (int k = 0; k < 5; ++k) s += d[k]; return s; }

bool run(const int c[7], int len)
{
    for (int start = 1; start + len - 1 <= 6; ++start) {
        bool ok = true;
        for (int v = start; v < start + len; ++v) ok &= c[v] > 0;
        if (ok) return true;
    }
    return false;
}

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

} // namespace

const char* box_name(int box)
{
    static const char* const names[kBoxes] = {
        "Ones", "Twos", "Threes", "Fours", "Fives", "Sixes",
        "3 of a Kind", "4 of a Kind", "Full House", "Run of 4", "Run of 5", "Yaht-CYD", "Chance",
    };
    return box >= 0 && box < kBoxes ? names[box] : "";
}

bool Game::over() const
{
    for (int b = 0; b < kBoxes; ++b) if (score[b] < 0) return false;
    return true;
}

int Game::turn() const
{
    int filled = 0;
    for (int b = 0; b < kBoxes; ++b) filled += score[b] >= 0;
    return filled < kBoxes ? filled + 1 : kBoxes;
}

void Game::roll(Rng& rng)
{
    if (!can_roll()) return;
    if (rolls == 0) held = 0;
    for (int k = 0; k < 5; ++k)
        if (!((held >> k) & 1)) dice[k] = static_cast<uint8_t>(1 + rng.next() % 6);
    ++rolls;
}

void Game::toggle_hold(int die)
{
    if (die < 0 || die > 4 || rolls == 0 || rolls >= 3 || over()) return;
    held ^= static_cast<uint8_t>(1u << die);
}

bool Game::is_yaht() const
{
    for (int k = 1; k < 5; ++k) if (dice[k] != dice[0]) return false;
    return true;
}

bool Game::can_score(int box) const
{
    if (box < 0 || box >= kBoxes || score[box] >= 0 || rolls == 0) return false;
    // Joker: an extra Yaht-CYD must go in its number's upper box if that is empty
    if (is_yaht() && score[YahtCyd] >= 0) {
        const int up = dice[0] - 1;
        if (score[up] < 0) return box == up;
    }
    return true;
}

int Game::potential(int box) const
{
    int c[7];
    counts(dice, c);
    const bool joker = is_yaht() && score[YahtCyd] >= 0;
    if (box <= Sixes) return c[box + 1] * (box + 1);
    int most = 0;
    for (int v = 1; v <= 6; ++v) if (c[v] > most) most = c[v];
    switch (box) {
        case ThreeKind: return most >= 3 ? sum(dice) : 0;
        case FourKind:  return most >= 4 ? sum(dice) : 0;
        case FullHouse: {
            bool three = false, two = false;
            for (int v = 1; v <= 6; ++v) { three |= c[v] == 3; two |= c[v] == 2; }
            return (three && two) || joker ? 25 : 0;
        }
        case SmallStraight: return run(c, 4) || joker ? 30 : 0;
        case LargeStraight: return run(c, 5) || joker ? 40 : 0;
        case YahtCyd:       return most == 5 ? 50 : 0;
        case Chance:        return sum(dice);
    }
    return 0;
}

bool Game::score_box(int box)
{
    if (!can_score(box)) return false;
    if (is_yaht() && score[YahtCyd] == 50) ++extra;   // bonus Yaht-CYD
    score[box] = static_cast<int16_t>(potential(box));
    rolls = 0;
    held = 0;
    return true;
}

int Game::upper() const
{
    int s = 0;
    for (int b = Ones; b <= Sixes; ++b) if (score[b] > 0) s += score[b];
    return s;
}

int Game::total() const
{
    int s = bonus() + 100 * extra;
    for (int b = 0; b < kBoxes; ++b) if (score[b] > 0) s += score[b];
    return s;
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "YAH1", 4); p += 4;
    memcpy(p, dice, 5); p += 5;
    *p++ = held;
    *p++ = rolls;
    for (int b = 0; b < kBoxes; ++b) { *p++ = uint8_t(score[b] & 0xFF); *p++ = uint8_t((score[b] >> 8) & 0xFF); }
    *p++ = extra;
    return kSaveBytes;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "YAH1", 4) != 0) return false;
    Game t;
    const uint8_t* p = buf + 4;
    memcpy(t.dice, p, 5); p += 5;
    t.held = *p++;
    t.rolls = *p++;
    for (int b = 0; b < kBoxes; ++b) { t.score[b] = static_cast<int16_t>(p[0] | (p[1] << 8)); p += 2; }
    t.extra = *p++;
    for (int k = 0; k < 5; ++k) if (t.dice[k] < 1 || t.dice[k] > 6) return false;
    if (t.rolls > 3 || t.held > 31) return false;
    for (int b = 0; b < kBoxes; ++b) if (t.score[b] < -1 || t.score[b] > 50) return false;
    *this = t;
    return true;
}

// ---- History -------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Score,Upper,Bonus,YahtCYDs,Seconds,Time\n";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%u,%u,%u,%lu,%s\n", (unsigned)r.score, (unsigned)r.upper,
                           (unsigned)r.bonus, (unsigned)r.yahts, (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    unsigned score = 0, upper = 0, bonus = 0, yahts = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%u,%u,%u,%lu,%15[^,\r\n]", &seq, &score, &upper, &bonus, &yahts, &secs, tm) != 7)
        return false;
    out.score = static_cast<uint16_t>(score);
    out.upper = static_cast<uint16_t>(upper);
    out.bonus = static_cast<uint16_t>(bonus);
    out.yahts = static_cast<uint8_t>(yahts);
    out.seconds = static_cast<uint32_t>(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    total += r.score;
    if (r.score > best) best = r.score;
    bonuses += r.bonus > 0;
    yahts += r.yahts;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace yahtcyd
