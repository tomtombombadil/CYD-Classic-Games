#include "rpgdice_core.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace rpgdice {

namespace {

const char* const kDieNames[kDieTypes] = {"Coin", "d4", "d6", "d8", "d10", "d12", "d20", "d100"};
const char* const kLabelNames[kLabels] = {"Roll", "Hit", "Damage", "Save", "Check", "Init", "Heal"};

// Appends printf-style text, never past cap
void add(char* out, size_t cap, size_t& n, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void add(char* out, size_t cap, size_t& n, const char* fmt, ...)
{
    if (n >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    const int w = vsnprintf(out + n, cap - n, fmt, ap);
    va_end(ap);
    if (w > 0) n = n + size_t(w) < cap ? n + size_t(w) : cap - 1;
}

// "2d6+1d8+3" (compact) or "2d6 + 1d8 + 3"
size_t pool_text(const Pool& p, char* out, size_t cap, bool compact)
{
    size_t n = 0;
    if (cap) out[0] = 0;
    const char* plus = compact ? "+" : " + ";
    bool first = true;
    for (int d = 0; d < kDieTypes; ++d) {
        if (!p.count[d]) continue;
        if (!first) add(out, cap, n, "%s", plus);
        if (d == Coin) add(out, cap, n, p.count[d] > 1 ? "%u Coins" : "Coin", unsigned(p.count[d]));
        else           add(out, cap, n, "%u%s", unsigned(p.count[d]), kDieNames[d]);
        first = false;
    }
    if (first) add(out, cap, n, "%s", p.mod ? "" : "No dice");
    if (p.mod) {
        if (first) add(out, cap, n, "%d", int(p.mod));
        else add(out, cap, n, compact ? "%+d" : (p.mod > 0 ? " + %d" : " - %d"),
                 compact ? int(p.mod) : (p.mod > 0 ? int(p.mod) : -int(p.mod)));
    }
    return n;
}

void put16(uint8_t*& p, int v) { *p++ = uint8_t(v); *p++ = uint8_t(v >> 8); }
int16_t get16(const uint8_t*& p) { const int v = p[0] | (p[1] << 8); p += 2; return int16_t(v); }

} // namespace

const char* die_name(int die)     { return die >= 0 && die < kDieTypes ? kDieNames[die] : "?"; }
const char* label_name(int label) { return label >= 0 && label < kLabels ? kLabelNames[label] : "Roll"; }

int Pool::dice() const
{
    int n = 0;
    for (int d = 0; d < kDieTypes; ++d) n += count[d];
    return n;
}

bool Pool::add(int die)
{
    if (die < 0 || die >= kDieTypes || dice() >= kMaxDice) return false;
    ++count[die];
    return true;
}

void Pool::bump_mod(int d)
{
    const int m = mod + d;
    mod = int8_t(m > kMaxMod ? kMaxMod : m < -kMaxMod ? -kMaxMod : m);
}

size_t Pool::format(char* out, size_t cap) const { return pool_text(*this, out, cap, false); }

int Rng::below(int n)
{
    if (n <= 1) return 0;
    const uint32_t limit = 0xFFFFFFFFu - 0xFFFFFFFFu % uint32_t(n);   // reject the uneven tail
    uint32_t v;
    do v = next(); while (v >= limit);
    return int(v % uint32_t(n));
}

void roll(const Pool& p, Rng& rng, Rolled& out)
{
    out = Rolled{};
    out.mod = p.mod;
    out.label = p.label;
    int total = p.mod;
    for (int d = 0; d < kDieTypes; ++d)
        for (int k = 0; k < p.count[d] && out.n < kMaxDice; ++k) {
            const int v = 1 + rng.below(kSides[d]);
            out.die[out.n] = uint8_t(d);
            out.value[out.n] = uint8_t(v);
            ++out.n;
            total += v;
        }
    out.total = int16_t(total);
}

size_t format_rolled(const Rolled& r, char* out, size_t cap, bool with_label)
{
    size_t n = 0;
    if (cap) out[0] = 0;
    if (with_label) add(out, cap, n, "%s ", label_name(r.label));
    // The pool it came from, compact
    Pool p;
    for (int k = 0; k < r.n; ++k) ++p.count[r.die[k]];
    p.mod = r.mod;
    char pt[48];
    pool_text(p, pt, sizeof pt, true);
    add(out, cap, n, "%s:", pt);
    const bool coin_only = r.n == 1 && r.die[0] == Coin && !r.mod;
    if (coin_only) { add(out, cap, n, " %s", r.value[0] == 2 ? "Heads" : "Tails"); return n; }
    for (int k = 0; k < r.n; ++k) add(out, cap, n, " %u", unsigned(r.value[k]));
    if (r.mod) add(out, cap, n, " %+d", int(r.mod));
    if (r.n > 1 || r.mod) add(out, cap, n, " = %d", int(r.total));
    return n;
}

void State::roll_pool(Rng& rng)
{
    preset = -1;
    shown = 1;
    roll(pool, rng, last[0]);
    char t[kHistoryText];
    format_rolled(last[0], t, sizeof t, false);
    hist_head = uint8_t((hist_head + kHistory - 1) % kHistory);
    memcpy(hist[hist_head], t, sizeof t);
    if (hist_n < kHistory) ++hist_n;
}

void State::roll_preset(int index, Rng& rng)
{
    if (index < 0 || index >= kPresets || !presets[index].lines) return;
    const Preset& pr = presets[index];
    preset = int8_t(index);
    shown = pr.lines;
    char t[kHistoryText];
    size_t n = 0;
    t[0] = 0;
    add(t, sizeof t, n, "%s:", pr.name[0] ? pr.name : "Preset");
    for (int l = 0; l < pr.lines; ++l) {
        roll(pr.pool[l], rng, last[l]);
        add(t, sizeof t, n, "%s %s %d", l ? "," : "", label_name(last[l].label), int(last[l].total));
        // the dice behind it, when there are few: "Hit 21 (14)"
        if (last[l].n && last[l].n <= 3) {
            add(t, sizeof t, n, " (");
            for (int k = 0; k < last[l].n; ++k) add(t, sizeof t, n, k ? " %u" : "%u", unsigned(last[l].value[k]));
            add(t, sizeof t, n, ")");
        }
    }
    hist_head = uint8_t((hist_head + kHistory - 1) % kHistory);
    memcpy(hist[hist_head], t, sizeof t);
    if (hist_n < kHistory) ++hist_n;
}

const char* State::history(int k) const
{
    if (k < 0 || k >= hist_n) return nullptr;
    return hist[(hist_head + k) % kHistory];
}

// ---- Save: "RPD1", then fields in order ---------------------------------------------------
size_t State::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "RPD1", 4); p += 4;
    auto put_pool = [&](const Pool& q) {
        memcpy(p, q.count, kDieTypes); p += kDieTypes;
        *p++ = uint8_t(q.mod);
        *p++ = q.label;
    };
    put_pool(pool);
    *p++ = uint8_t(preset);
    *p++ = shown;
    for (const Rolled& r : last) {
        *p++ = r.n;
        memcpy(p, r.die, kMaxDice); p += kMaxDice;
        memcpy(p, r.value, kMaxDice); p += kMaxDice;
        *p++ = uint8_t(r.mod);
        *p++ = r.label;
        put16(p, r.total);
    }
    for (const Preset& pr : presets) {
        memcpy(p, pr.name, kNameLen); p += kNameLen;
        *p++ = pr.lines;
        for (const Pool& q : pr.pool) put_pool(q);
    }
    *p++ = hist_n;
    *p++ = hist_head;
    memcpy(p, hist, sizeof hist); p += sizeof hist;
    return size_t(p - buf);
}

bool State::deserialize(const uint8_t* buf, size_t len)
{
    if (len < 4 || memcmp(buf, "RPD1", 4) != 0) return false;
    const size_t need = 4 + 10 + 2 + kPresetLines * (1 + 2 * kMaxDice + 4) +
                        kPresets * (kNameLen + 1 + kPresetLines * 10) + 2 + sizeof hist;
    if (len < need) return false;
    // Filled in place (a State is ~5 KB: too big for a copy on the UI task's stack)
    State& s = *this;
    const uint8_t* p = buf + 4;
    auto get_pool = [&](Pool& q) {
        memcpy(q.count, p, kDieTypes); p += kDieTypes;
        q.mod = int8_t(*p++);
        q.label = *p++;
        if (q.label >= kLabels) q.label = 0;
        int n = 0;
        for (int d = 0; d < kDieTypes; ++d) { if (n + q.count[d] > kMaxDice) q.count[d] = uint8_t(kMaxDice - n); n += q.count[d]; }
        if (q.mod > kMaxMod || q.mod < -kMaxMod) q.mod = 0;
    };
    get_pool(s.pool);
    s.preset = int8_t(*p++);
    s.shown = *p++;
    for (Rolled& r : s.last) {
        r.n = *p++;
        memcpy(r.die, p, kMaxDice); p += kMaxDice;
        memcpy(r.value, p, kMaxDice); p += kMaxDice;
        r.mod = int8_t(*p++);
        r.label = *p++;
        r.total = get16(p);
        if (r.n > kMaxDice) r.n = 0;
        for (int k = 0; k < r.n; ++k) if (r.die[k] >= kDieTypes) r.n = 0;
        if (r.label >= kLabels) r.label = 0;
    }
    for (Preset& pr : s.presets) {
        memcpy(pr.name, p, kNameLen); p += kNameLen;
        pr.name[kNameLen - 1] = 0;
        pr.lines = *p++;
        if (pr.lines > kPresetLines) pr.lines = 0;
        for (Pool& q : pr.pool) get_pool(q);
    }
    s.hist_n = *p++;
    s.hist_head = *p++;
    memcpy(s.hist, p, sizeof s.hist); p += sizeof s.hist;
    if (s.hist_n > kHistory || s.hist_head >= kHistory) s.hist_n = s.hist_head = 0;
    for (auto& h : s.hist) h[kHistoryText - 1] = 0;
    if (s.shown > kPresetLines) s.shown = 0;
    if (s.preset >= kPresets || (s.preset >= 0 && !s.presets[s.preset].lines)) s.preset = -1;
    return true;
}

} // namespace rpgdice
