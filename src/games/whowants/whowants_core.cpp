#include "whowants_core.h"

#include <cstdio>
#include <cstring>
#include "../common/trivia_bank.h"

namespace whowants {

const int32_t kPrize[kSteps] = {100, 200, 300, 500, 1000, 2000, 4000, 8000, 16000, 32000,
                                64000, 125000, 250000, 500000, 1000000};

int32_t safe_amount(int step)
{
    if (step >= 10) return kPrize[9];
    if (step >= 5) return kPrize[4];
    return 0;
}

namespace {

int level_of(int step) { return step < 5 ? trivia::kEasy : step < 10 ? trivia::kMedium : trivia::kHard; }

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

} // namespace

uint32_t Game::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

// A question of this level not played yet (when all were, that level starts over)
bool Game::pick(int level, bool (*fits)(int))
{
    const int n = trivia::count() < kBankMax ? trivia::count() : kBankMax;
    for (int pass = 0; pass < 2; ++pass) {
        int candidates = 0;
        for (int i = 0; i < n; ++i)
            if (!trivia::true_false(i) && trivia::difficulty(i) == level && !(played[i / 8] >> (i % 8) & 1)) ++candidates;
        if (candidates > 0) {
            // walk from a random start; take the first that fits
            int at = int(rand_next() % uint32_t(n));
            for (int k = 0; k < n; ++k, at = (at + 1) % n) {
                if (trivia::true_false(at) || trivia::difficulty(at) != level || (played[at / 8] >> (at % 8) & 1)) continue;
                played[at / 8] |= uint8_t(1 << (at % 8));
                if (fits && !fits(at)) continue;          // too long for this screen: skip it
                q = int16_t(at);
                // shuffle the answers
                for (int s = 0; s < 4; ++s) order[s] = uint8_t(s);
                for (int s = 3; s > 0; --s) {
                    const int j = int(rand_next() % uint32_t(s + 1));
                    const uint8_t t = order[s]; order[s] = order[j]; order[j] = t;
                }
                hidden = shown = 0;
                chosen = friend_pick = -1;
                friend_sure = 0;
                memset(poll, 0, sizeof poll);
                phase = Phase::Asking;
                return true;
            }
        }
        // every question of this level played: start that level over
        for (int i = 0; i < n; ++i)
            if (!trivia::true_false(i) && trivia::difficulty(i) == level) played[i / 8] &= uint8_t(~(1 << (i % 8)));
    }
    return false;
}

void Game::start(uint32_t seed, bool (*fits)(int))
{
    uint8_t keep[sizeof played];
    memcpy(keep, played, sizeof keep);                    // played questions carry over between games
    *this = Game{};
    memcpy(played, keep, sizeof keep);
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
    pick(level_of(0), fits);
}

bool Game::next_question(bool (*fits)(int))
{
    if (phase != Phase::Right || step + 1 >= kSteps) return false;
    ++step;
    return pick(level_of(step), fits);
}

int Game::right_slot() const
{
    for (int s = 0; s < 4; ++s) if (order[s] == 0) return s;
    return 0;
}

bool Game::lock(int slot)
{
    if (phase != Phase::Asking || slot < 0 || slot > 3 || (hidden >> slot & 1)) return false;
    chosen = int8_t(slot);
    phase = Phase::Locked;
    return true;
}

bool Game::reveal()
{
    if (phase != Phase::Locked) return false;
    if (chosen == right_slot()) {
        if (step + 1 >= kSteps) { won = kPrize[kSteps - 1]; phase = Phase::Over; }
        else phase = Phase::Right;
    } else {
        won = safe_amount(step);
        phase = Phase::Over;
    }
    return true;
}

bool Game::walk_away()
{
    if (phase != Phase::Asking) return false;
    won = banked();
    walked = true;
    phase = Phase::Over;
    return true;
}

bool Game::use(Lifeline l)
{
    if (phase != Phase::Asking || (used & l)) return false;
    used |= l;
    shown |= l;
    const int right = right_slot();
    const int lv = level_of(step);
    if (l == kFifty) {
        int gone = 0;
        while (gone < 2) {
            const int s = int(rand_next() % 4);
            if (s == right || (hidden >> s & 1)) continue;
            hidden |= uint8_t(1 << s);
            ++gone;
        }
    } else if (l == kAudience) {
        // the right answer gets a share that falls with the level; the rest spread at random
        static const int kRight[3] = {62, 47, 36};
        int open[4], n = 0;
        for (int s = 0; s < 4; ++s) if (s != right && !(hidden >> s & 1)) open[n++] = s;
        // fewer answers left (after a 50:50) = a clearer vote
        int share = kRight[lv] + 10 * (3 - n) + int(rand_next() % 21) - 10;
        memset(poll, 0, sizeof poll);
        if (n == 0) share = 100;
        poll[right] = uint8_t(share);
        // the rest spread at random over the wrong answers still showing
        int w[4] = {}, wsum = 0;
        for (int k = 0; k < n; ++k) { w[k] = 1 + int(rand_next() % 10); wsum += w[k]; }
        int left = 100 - share;
        for (int k = 0; k < n; ++k) {
            const int v = k + 1 == n ? left : (100 - share) * w[k] / wsum;
            poll[open[k]] = uint8_t(v);
            left -= v;
        }
    } else {
        static const int kKnows[3] = {85, 68, 50};
        const bool knows = int(rand_next() % 100) < kKnows[lv];
        if (knows) {
            friend_pick = int8_t(right);
            friend_sure = uint8_t(lv == 0 ? 2 : lv == 1 ? 1 + rand_next() % 2 : 1);
        } else {
            int s;
            do s = int(rand_next() % 4); while (hidden >> s & 1);
            friend_pick = int8_t(s);
            friend_sure = 0;
        }
    }
    return true;
}

// ---- Save ---------------------------------------------------------------------------------------
size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "WWC1", 4); n = 4;
    buf[n++] = step;
    buf[n++] = uint8_t(q); buf[n++] = uint8_t(uint16_t(q) >> 8);
    memcpy(buf + n, order, 4); n += 4;
    buf[n++] = hidden;
    buf[n++] = used;
    buf[n++] = shown;
    memcpy(buf + n, poll, 4); n += 4;
    buf[n++] = uint8_t(friend_pick);
    buf[n++] = friend_sure;
    buf[n++] = uint8_t(chosen);
    buf[n++] = uint8_t(phase);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(uint32_t(won) >> (8 * k));
    buf[n++] = walked ? 1 : 0;
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    memcpy(buf + n, played, sizeof played); n += sizeof played;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "WWC1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.step = buf[n++];
    g.q = int16_t(buf[n] | buf[n + 1] << 8); n += 2;
    memcpy(g.order, buf + n, 4); n += 4;
    g.hidden = buf[n++];
    g.used = buf[n++];
    g.shown = buf[n++];
    memcpy(g.poll, buf + n, 4); n += 4;
    g.friend_pick = int8_t(buf[n++]);
    g.friend_sure = buf[n++];
    g.chosen = int8_t(buf[n++]);
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    uint32_t w = 0;
    for (int k = 0; k < 4; ++k) w |= uint32_t(buf[n++]) << (8 * k);
    g.won = int32_t(w);
    g.walked = buf[n++] != 0;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (!g.rng) g.rng = 1;
    memcpy(g.played, buf + n, sizeof g.played);
    if (g.step >= kSteps || g.q < 0 || g.q >= trivia::count() || trivia::true_false(g.q)) return false;
    bool seen[4] = {};
    for (uint8_t o : g.order) { if (o > 3 || seen[o]) return false; seen[o] = true; }
    if (g.hidden > 15 || g.used > 7 || g.friend_pick < -1 || g.friend_pick > 3 || g.chosen < -1 || g.chosen > 3) return false;
    if (g.won < 0 || g.won > 1000000) return false;
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Won,Reached,Seconds,Time";

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%ld,%u,%lu,%s\n", long(r.won), unsigned(r.reached), (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    long won = 0;
    unsigned reached = 0;
    char tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%ld,%u,%lu,%15[^,\r\n]", &seq, &won, &reached, &secs, tm) != 5) return false;
    out.won = int32_t(won);
    out.reached = uint8_t(reached);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    millions += r.won >= 1000000;
    if (r.won > best) best = r.won;
    total += r.won;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace whowants
