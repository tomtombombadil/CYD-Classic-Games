#include "jeoparcyd_core.h"

#include <cstdio>
#include <cstring>
#include "../common/trivia_bank.h"

namespace jcyd {

namespace {

const int kRowDiff[kRows] = {trivia::kEasy, trivia::kEasy, trivia::kMedium, trivia::kHard, trivia::kHard};

// The chance a computer knows a clue: [level][difficulty] in percent
const int kKnows[3][3] = {{55, 35, 20}, {72, 52, 36}, {86, 68, 52}};

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

int bank_n() { return trivia::count() < kBankMax ? trivia::count() : kBankMax; }

} // namespace

uint32_t Game::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

// An unplayed multiple-choice question of this category, as near the
// difficulty as there is; -1 when the category has none left at all
int Game::take_question(int c, int d)
{
    const int n = bank_n();
    static const int kTry[3][3] = {{0, 1, 2}, {1, 0, 2}, {2, 1, 0}};
    for (int pass = 0; pass < 2; ++pass) {
        for (int t = 0; t < 3; ++t) {
            const int want = kTry[d][t];
            int count = 0;
            for (int i = 0; i < n; ++i)
                if (trivia::category(i) == c && trivia::difficulty(i) == want && !trivia::true_false(i) && !(played[i / 8] >> (i % 8) & 1)) ++count;
            if (!count) continue;
            int k = int(rand_next() % uint32_t(count));
            for (int i = 0; i < n; ++i) {
                if (trivia::category(i) != c || trivia::difficulty(i) != want || trivia::true_false(i) || (played[i / 8] >> (i % 8) & 1)) continue;
                if (k-- == 0) { played[i / 8] |= uint8_t(1 << (i % 8)); return i; }
            }
        }
        // all of this category played: start it over
        for (int i = 0; i < n; ++i) if (trivia::category(i) == c) played[i / 8] &= uint8_t(~(1 << (i % 8)));
    }
    return -1;
}

bool Game::deal_round()
{
    // six different categories
    int pool[trivia::kCategories];
    for (int i = 0; i < trivia::kCategories; ++i) pool[i] = i;
    for (int i = trivia::kCategories - 1; i > 0; --i) {
        const int j = int(rand_next() % uint32_t(i + 1));
        const int t = pool[i]; pool[i] = pool[j]; pool[j] = t;
    }
    for (int c = 0; c < kCats; ++c) {
        cat[c] = uint8_t(pool[c]);
        for (int r = 0; r < kRows; ++r) {
            cell[c][r] = Cell{};
            cell[c][r].q = int16_t(take_question(cat[c], kRowDiff[r]));
            if (cell[c][r].q < 0) return false;
        }
    }
    // Daily Doubles: one in round 1, two in round 2, never on the top row
    for (int k = 0; k < (round == 0 ? 1 : 2);) {
        const int c = int(rand_next() % kCats), r = 1 + int(rand_next() % (kRows - 1));
        if (cell[c][r].daily) continue;
        cell[c][r].daily = 1;
        ++k;
    }
    phase = Phase::Board;
    return true;
}

void Game::start(uint32_t seed, int lv)
{
    uint8_t keep[sizeof played];
    memcpy(keep, played, sizeof keep);
    *this = Game{};
    memcpy(played, keep, sizeof keep);
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
    level = uint8_t(lv < 0 ? 0 : lv > 2 ? 2 : lv);
    chooser = 0;
    deal_round();
}

int32_t Game::max_wager(int p) const
{
    const int32_t top = round == 1 ? 2000 : 1000;
    return score[p] > top ? score[p] : top;
}

int Game::right_slot() const
{
    for (int s = 0; s < 4; ++s) if (order[s] == 0) return s;
    return 0;
}

int Game::clues_left() const
{
    int n = 0;
    for (auto& col : cell) for (const Cell& x : col) n += !x.used;
    return n;
}

void Game::shuffle_answers()
{
    for (int s = 0; s < 4; ++s) order[s] = uint8_t(s);
    for (int s = 3; s > 0; --s) {
        const int j = int(rand_next() % uint32_t(s + 1));
        const uint8_t t = order[s]; order[s] = order[j]; order[j] = t;
    }
}

void Game::plan_clue()
{
    const int d = q >= 0 ? trivia::difficulty(q) : 1;
    for (int p = 1; p < kPlayers; ++p) {
        plan[p] = Plan{};
        if (tried >> p & 1) continue;
        const bool knows = int(rand_next() % 100) < kKnows[level][d];
        const bool guesses = !knows && rand_next() % 100 < 15;
        if (!knows && !guesses) continue;
        const int fast = level == 2 ? 1500 : level == 1 ? 2200 : 3000;
        plan[p].buzz_ms = uint16_t(fast + rand_next() % 3500);
        if (knows) plan[p].slot = int8_t(right_slot());
        else {
            int s;
            do s = int(rand_next() % 4); while (s == right_slot() || (wrong_slots >> s & 1));
            plan[p].slot = int8_t(s);
        }
    }
}

bool Game::pick(int c, int r)
{
    if (phase != Phase::Board || c < 0 || c >= kCats || r < 0 || r >= kRows || cell[c][r].used) return false;
    cur_c = int8_t(c); cur_r = int8_t(r);
    cell[c][r].used = 1;
    q = cell[c][r].q;
    shuffle_answers();
    tried = wrong_slots = 0;
    answerer = answer_slot = -1;
    last_right = false;
    wager = 0;
    for (Plan& p : plan) p = Plan{};
    if (cell[c][r].daily) { phase = Phase::Wager; return true; }
    phase = Phase::Clue;
    plan_clue();
    return true;
}

bool Game::set_wager(int32_t w)
{
    if (phase != Phase::Wager) return false;
    if (w < 5) w = 5;
    if (w > max_wager(chooser)) w = max_wager(chooser);
    wager = w;
    phase = Phase::Clue;
    for (Plan& p : plan) p = Plan{};                 // a Daily Double is for the picker alone
    if (chooser != 0) {
        const int d = trivia::difficulty(q);
        const bool knows = int(rand_next() % 100) < kKnows[level][d];
        plan[chooser].buzz_ms = uint16_t(2500 + rand_next() % 2000);
        if (knows) plan[chooser].slot = int8_t(right_slot());
        else { int s; do s = int(rand_next() % 4); while (s == right_slot()); plan[chooser].slot = int8_t(s); }
    }
    return true;
}

bool Game::answer(int p, int slot)
{
    if (phase != Phase::Clue || p < 0 || p >= kPlayers || slot < 0 || slot > 3) return false;
    if ((tried >> p & 1) || (wrong_slots >> slot & 1)) return false;
    if (wager && p != chooser) return false;
    const int32_t v = wager ? wager : value(cur_r);
    answerer = int8_t(p);
    answer_slot = int8_t(slot);
    plan[p] = Plan{};
    if (slot == right_slot()) {
        score[p] += v;
        chooser = uint8_t(p);
        last_right = true;
        phase = Phase::Reveal;
        return true;
    }
    score[p] -= v;
    tried |= uint8_t(1 << p);
    wrong_slots |= uint8_t(1 << slot);
    last_right = false;
    if (wager || tried == (1 << kPlayers) - 1) phase = Phase::Reveal;
    return true;
}

bool Game::time_up()
{
    if (phase != Phase::Clue) return false;
    if (wager && !(tried >> chooser & 1)) {             // a Daily Double not answered: counts as wrong
        score[chooser] -= wager;
        tried |= uint8_t(1 << chooser);
    }
    answerer = -1;
    phase = Phase::Reveal;
    return true;
}

bool Game::done_revealing()
{
    if (phase != Phase::Reveal) return false;
    if (clues_left() > 0) { phase = Phase::Board; return true; }
    if (round == 0) {
        round = 1;
        // the player with the least money picks first in round 2
        int low = 0;
        for (int p = 1; p < kPlayers; ++p) if (score[p] < score[low]) low = p;
        chooser = uint8_t(low);
        deal_round();
        return true;
    }
    // Final: one hard clue from a random category
    round = 2;
    final_cat = uint8_t(rand_next() % trivia::kCategories);
    q = int16_t(take_question(final_cat, trivia::kHard));
    shuffle_answers();
    for (int p = 0; p < kPlayers; ++p) { final_wager[p] = 0; final_slot[p] = -1; }
    bool anyone = false;
    for (int p = 0; p < kPlayers; ++p) anyone = anyone || in_final(p);
    phase = anyone && q >= 0 ? Phase::FinalWager : Phase::Over;
    return true;
}

bool Game::final_bet(int p, int32_t w)
{
    if (phase != Phase::FinalWager || !in_final(p)) return false;
    if (w < 0) w = 0;
    if (w > score[p]) w = score[p];
    final_wager[p] = w;
    // everyone bets before the clue shows (the screen collects the computers' at once)
    return true;
}

bool Game::final_done() const
{
    for (int p = 0; p < kPlayers; ++p) if (in_final(p) && final_slot[p] < 0) return false;
    return true;
}

bool Game::final_answer(int p, int slot)
{
    if (phase == Phase::FinalWager) phase = Phase::FinalClue;
    if (phase != Phase::FinalClue || !in_final(p) || final_slot[p] >= 0 || slot < 0 || slot > 3) return false;
    final_slot[p] = int8_t(slot);
    if (final_done()) {
        for (int k = 0; k < kPlayers; ++k)
            if (in_final(k)) score[k] += final_slot[k] == right_slot() ? final_wager[k] : -final_wager[k];
        phase = Phase::Over;
    }
    return true;
}

int Game::place(int p) const
{
    int pl = 1;
    for (int k = 0; k < kPlayers; ++k) if (k != p && score[k] > score[p]) ++pl;
    return pl;
}

// ---- Computers ------------------------------------------------------------------------------------
void Game::pick_cell(int lv, int* c, int* r)
{
    *c = *r = -1;
    int open[kCats * kRows], n = 0;
    for (int cc = 0; cc < kCats; ++cc) for (int rr = 0; rr < kRows; ++rr) if (!cell[cc][rr].used) open[n++] = cc * kRows + rr;
    if (!n) return;
    int pick = open[rand_next() % uint32_t(n)];
    if (lv >= 1) {
        // the top clue left in a category it picked from before (or a random one)
        const int cc = cur_c >= 0 && !cell[cur_c][kRows - 1].used ? cur_c : pick / kRows;
        for (int rr = 0; rr < kRows; ++rr) if (!cell[cc][rr].used) { pick = cc * kRows + rr; break; }
        // Hard hunts the Daily Doubles among the big values once it leads
        if (lv >= 2 && score[chooser] > 0 && rand_next() % 2) {
            for (int k = 0; k < n; ++k) if (open[k] % kRows >= 3) { pick = open[k]; break; }
        }
    }
    *c = pick / kRows;
    *r = pick % kRows;
}

int32_t Game::cpu_wager(int p) const
{
    const int32_t top = max_wager(p);
    int32_t w;
    if (level == 0) w = top / 4;
    else if (level == 1) w = top / 2;
    else {
        int32_t best_other = 0;
        for (int k = 0; k < kPlayers; ++k) if (k != p && score[k] > best_other) best_other = score[k];
        w = score[p] < best_other ? top : top * 2 / 3;
    }
    w = w / 100 * 100;
    return w < 5 ? 5 : w;
}

int32_t Game::cpu_final_wager(int p) const
{
    if (!in_final(p)) return 0;
    int32_t first = 0, second = 0;
    for (int k = 0; k < kPlayers; ++k) {
        if (k == p) continue;
        if (score[k] > first) { second = first; first = score[k]; }
        else if (score[k] > second) second = score[k];
    }
    int32_t w;
    if (score[p] > first) {                       // leading: cover the next player doubling up
        w = 2 * first - score[p] + 100;
        if (w < 0) w = 0;
    } else {
        w = level == 0 ? score[p] / 3 : score[p];
    }
    if (w > score[p]) w = score[p];
    return w;
}

int Game::cpu_final_slot(int p)
{
    (void)p;
    const bool knows = int(rand_next() % 100) < kKnows[level][trivia::kHard];
    if (knows) return right_slot();
    int s;
    do s = int(rand_next() % 4); while (s == right_slot());
    return s;
}

// ---- Save ---------------------------------------------------------------------------------------
namespace {
void put32(uint8_t* b, size_t& n, int32_t v) { for (int k = 0; k < 4; ++k) b[n++] = uint8_t(uint32_t(v) >> (8 * k)); }
int32_t get32(const uint8_t* b, size_t& n) { uint32_t v = 0; for (int k = 0; k < 4; ++k) v |= uint32_t(b[n++]) << (8 * k); return int32_t(v); }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "JCY1", 4); n = 4;
    buf[n++] = round;
    buf[n++] = level;
    memcpy(buf + n, cat, kCats); n += kCats;
    for (auto& col : cell)
        for (const Cell& x : col) { buf[n++] = uint8_t(x.q); buf[n++] = uint8_t(uint16_t(x.q) >> 8); buf[n++] = x.used; buf[n++] = x.daily; }
    for (int32_t s : score) put32(buf, n, s);
    buf[n++] = chooser;
    buf[n++] = uint8_t(phase);
    buf[n++] = uint8_t(cur_c);
    buf[n++] = uint8_t(cur_r);
    buf[n++] = uint8_t(q); buf[n++] = uint8_t(uint16_t(q) >> 8);
    memcpy(buf + n, order, 4); n += 4;
    put32(buf, n, wager);
    buf[n++] = tried;
    buf[n++] = wrong_slots;
    buf[n++] = uint8_t(answerer);
    buf[n++] = uint8_t(answer_slot);
    buf[n++] = last_right ? 1 : 0;
    for (const Plan& p : plan) { buf[n++] = uint8_t(p.buzz_ms); buf[n++] = uint8_t(p.buzz_ms >> 8); buf[n++] = uint8_t(p.slot); }
    buf[n++] = final_cat;
    for (int32_t w : final_wager) put32(buf, n, w);
    for (int8_t s : final_slot) buf[n++] = uint8_t(s);
    put32(buf, n, int32_t(rng));
    memcpy(buf + n, played, sizeof played); n += sizeof played;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "JCY1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.round = buf[n++];
    g.level = buf[n++];
    memcpy(g.cat, buf + n, kCats); n += kCats;
    for (auto& col : g.cell)
        for (Cell& x : col) {
            x.q = int16_t(buf[n] | buf[n + 1] << 8); n += 2;
            x.used = buf[n++]; x.daily = buf[n++];
            if (x.q < -1 || x.q >= trivia::count() || x.used > 1 || x.daily > 1) return false;
        }
    for (int32_t& s : g.score) s = get32(buf, n);
    g.chooser = buf[n++];
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.cur_c = int8_t(buf[n++]);
    g.cur_r = int8_t(buf[n++]);
    g.q = int16_t(buf[n] | buf[n + 1] << 8); n += 2;
    memcpy(g.order, buf + n, 4); n += 4;
    g.wager = get32(buf, n);
    g.tried = buf[n++];
    g.wrong_slots = buf[n++];
    g.answerer = int8_t(buf[n++]);
    g.answer_slot = int8_t(buf[n++]);
    g.last_right = buf[n++] != 0;
    for (Plan& p : g.plan) { p.buzz_ms = uint16_t(buf[n] | buf[n + 1] << 8); n += 2; p.slot = int8_t(buf[n++]); }
    g.final_cat = buf[n++];
    for (int32_t& w : g.final_wager) w = get32(buf, n);
    for (int8_t& s : g.final_slot) s = int8_t(buf[n++]);
    g.rng = uint32_t(get32(buf, n));
    if (!g.rng) g.rng = 1;
    memcpy(g.played, buf + n, sizeof g.played);
    if (g.round > 2 || g.level > 2 || g.chooser >= kPlayers || g.final_cat >= trivia::kCategories) return false;
    for (uint8_t c : g.cat) if (c >= trivia::kCategories) return false;
    if (g.cur_c < -1 || g.cur_c >= kCats || g.cur_r < -1 || g.cur_r >= kRows || g.q < -1 || g.q >= trivia::count()) return false;
    bool seen[4] = {};
    for (uint8_t o : g.order) { if (o > 3 || seen[o]) return false; seen[o] = true; }
    if (g.answerer < -1 || g.answerer >= kPlayers || g.answer_slot < -1 || g.answer_slot > 3) return false;
    for (const Plan& p : g.plan) if (p.slot < -1 || p.slot > 3) return false;
    for (int8_t s : g.final_slot) if (s < -1 || s > 3) return false;
    if ((g.phase == Phase::Clue || g.phase == Phase::Wager || g.phase == Phase::Reveal || g.phase == Phase::FinalClue ||
         g.phase == Phase::FinalWager) && g.q < 0) return false;
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Score,Level,Seconds,Time";

namespace {
const char* const kLevelNames[3] = {"Easy", "Medium", "Hard"};
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%ld,%s,%lu,%s\n", unsigned(r.place), long(r.score),
                           kLevelNames[r.level < 3 ? r.level : 0], (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    long score = 0;
    unsigned place = 0;
    char lv[12] = {}, tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%ld,%11[^,],%lu,%15[^,\r\n]", &seq, &place, &score, lv, &secs, tm) != 6) return false;
    out.place = uint8_t(place);
    out.score = int32_t(score);
    out.level = 0;
    for (int i = 0; i < 3; ++i) if (!strcmp(lv, kLevelNames[i])) out.level = uint8_t(i);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    wins += r.place == 1;
    if (r.score > best) best = r.score;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace jcyd
