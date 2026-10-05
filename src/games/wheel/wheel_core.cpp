// Wheel of CYD rules and computer players. See wheel_core.h.
#include "wheel_core.h"

#include <cstring>

namespace wheel {

const int16_t kWheel[kWedges] = {500, 300, 700, kSkip, 400, 600, 350, 900,
                                 kBust, 450, 800, 300, 550, 400, 1000, 650};

static_assert(kRows * kCols <= 127, "tile positions fit int8_t");

bool is_vowel(char c) { return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U'; }

int wrap(const char* text, int8_t* pos, int cap)
{
    const int n = int(strlen(text));
    if (n > cap) return 0;
    // Rows: [start, end) character ranges, greedy at spaces
    int rs[kRows + 1], re[kRows + 1], rows = 0;
    int i = 0;
    while (i < n) {
        if (rows == kRows) return 0;
        int end = i, last_fit = -1;
        // extend word by word
        for (int j = i; j <= n; ++j) {
            if (j == n || text[j] == ' ') {
                if (j - i <= kCols) last_fit = j; else break;
            }
        }
        if (last_fit < 0) return 0;                       // a word longer than a row
        end = last_fit;
        rs[rows] = i;
        re[rows] = end;
        ++rows;
        i = end + (end < n ? 1 : 0);
    }
    for (int k = 0; k < n; ++k) pos[k] = -1;
    const int top = (kRows - rows) / 2;
    for (int r = 0; r < rows; ++r) {
        const int len = re[r] - rs[r], left = (kCols - len) / 2;
        for (int k = rs[r]; k < re[r]; ++k)
            if (text[k] != ' ') pos[k] = int8_t((top + r) * kCols + left + (k - rs[r]));
    }
    return rows;
}

// ---- Game -------------------------------------------------------------------------------------

void Game::pick_puzzle(Rng& rng)
{
    const int n = kPhraseCount < kMaxPhrases ? kPhraseCount : kMaxPhrases;
    int left = 0;
    for (int i = 0; i < n; ++i) left += !((played[i / 8] >> (i % 8)) & 1);
    if (!left) { memset(played, 0, sizeof played); left = n; }
    int k = rng.below(left);
    for (int i = 0; i < n; ++i) {
        if ((played[i / 8] >> (i % 8)) & 1) continue;
        if (k-- == 0) { puzzle = int16_t(i); break; }
    }
    played[puzzle / 8] |= uint8_t(1 << (puzzle % 8));
}

void Game::start(int n_players, Rng& rng)
{
    uint8_t keep[sizeof played];
    memcpy(keep, played, sizeof keep);
    *this = Game{};
    memcpy(played, keep, sizeof keep);
    players = uint8_t(n_players < 2 ? 2 : n_players > kMaxPlayers ? kMaxPlayers : n_players);
    pick_puzzle(rng);
}

int Game::count(char c) const
{
    int n = 0;
    for (const char* p = text(); *p; ++p) n += *p == c;
    return n;
}

int Game::letters() const
{
    int n = 0;
    for (const char* p = text(); *p; ++p) n += is_letter(*p);
    return n;
}

int Game::hidden() const
{
    int n = 0;
    for (const char* p = text(); *p; ++p) n += is_letter(*p) && !called_letter(*p);
    return n;
}

bool Game::consonants_left() const
{
    for (const char* p = text(); *p; ++p)
        if (is_letter(*p) && !is_vowel(*p) && !called_letter(*p)) return true;
    return false;
}

bool Game::vowels_left() const
{
    for (const char* p = text(); *p; ++p)
        if (is_vowel(*p) && !called_letter(*p)) return true;
    return false;
}

bool Game::can_call(char c) const
{
    return phase == Phase::Consonant && is_letter(c) && !is_vowel(c) && !called_letter(c);
}

bool Game::can_buy_letter(char c) const
{
    return phase == Phase::Choose && is_vowel(c) && !called_letter(c) && money[turn] >= kVowelCost;
}

void Game::pass()
{
    turn = uint8_t((turn + 1) % players);
    ++turns;
    phase = Phase::Choose;
}

void Game::win_round()
{
    round_winner = int8_t(turn);
    bank[turn] += money[turn] > kSolveMin ? money[turn] : kSolveMin;
    ++turns;
    phase = Phase::RoundOver;
}

int Game::spin(Rng& rng)
{
    if (!can_spin()) return -1;
    wedge = int8_t(rng.below(kWedges));
    const int16_t v = kWheel[wedge];
    if (v == kBust) { money[turn] = 0; pass(); }
    else if (v == kSkip) pass();
    else { value = v; phase = Phase::Consonant; }
    return wedge;
}

int Game::call(char c)
{
    if (!can_call(c)) return -1;
    called |= 1u << (c - 'A');
    const int n = count(c);
    money[turn] += int32_t(n) * value;
    value = 0;
    phase = Phase::Choose;
    if (!n) pass();
    else if (!hidden()) win_round();
    return n;
}

int Game::buy(char c)
{
    if (!can_buy_letter(c) || !vowels_left()) return -1;
    called |= 1u << (c - 'A');
    money[turn] -= kVowelCost;
    const int n = count(c);
    if (!n) pass();
    else if (!hidden()) win_round();
    return n;
}

bool Game::solve(const char* guess)
{
    if (!can_solve()) return false;
    const char* p = text();
    const char* g = guess;
    bool right = true;
    for (; *p; ++p) {
        if (!is_letter(*p) || called_letter(*p)) continue;
        if (*g != *p) { right = false; break; }
        ++g;
    }
    if (right && *g) right = false;                    // extra letters
    if (right) win_round(); else pass();
    return right;
}

void Game::next_round(Rng& rng)
{
    if (phase != Phase::RoundOver) return;
    if (round + 1 >= kRounds) { phase = Phase::Over; return; }
    ++round;
    for (int i = 0; i < kMaxPlayers; ++i) money[i] = 0;
    called = 0;
    value = 0;
    round_winner = -1;
    turn = uint8_t(round % players);
    phase = Phase::Choose;
    pick_puzzle(rng);
}

int Game::leader() const
{
    int best = 0, ties = 0;
    for (int i = 1; i < players; ++i) {
        if (bank[i] > bank[best]) { best = i; ties = 0; }
        else if (bank[i] == bank[best]) ++ties;
    }
    return ties ? -1 : best;
}

// ---- Save -------------------------------------------------------------------------------------

namespace {
void put32(uint8_t*& p, uint32_t v) { for (int b = 0; b < 4; ++b) *p++ = uint8_t(v >> (8 * b)); }
uint32_t get32(const uint8_t*& p) { uint32_t v = 0; for (int b = 0; b < 4; ++b) v |= uint32_t(*p++) << (8 * b); return v; }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "WHL1", 4);
    p += 4;
    *p++ = players; *p++ = round; *p++ = turn; *p++ = uint8_t(phase);
    *p++ = uint8_t(puzzle); *p++ = uint8_t(puzzle >> 8);
    put32(p, called);
    for (int i = 0; i < kMaxPlayers; ++i) put32(p, uint32_t(money[i]));
    for (int i = 0; i < kMaxPlayers; ++i) put32(p, uint32_t(bank[i]));
    *p++ = uint8_t(value); *p++ = uint8_t(uint16_t(value) >> 8);
    *p++ = uint8_t(wedge);
    *p++ = uint8_t(round_winner);
    *p++ = uint8_t(turns); *p++ = uint8_t(turns >> 8);
    memcpy(p, played, sizeof played);
    p += sizeof played;
    return size_t(p - buf);
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "WHL1", 4) != 0) return false;
    Game g;
    const uint8_t* p = buf + 4;
    g.players = *p++; g.round = *p++; g.turn = *p++;
    const uint8_t ph = *p++;
    g.puzzle = int16_t(p[0] | p[1] << 8);
    p += 2;
    g.called = get32(p);
    for (int i = 0; i < kMaxPlayers; ++i) g.money[i] = int32_t(get32(p));
    for (int i = 0; i < kMaxPlayers; ++i) g.bank[i] = int32_t(get32(p));
    g.value = int16_t(p[0] | p[1] << 8);
    p += 2;
    g.wedge = int8_t(*p++);
    g.round_winner = int8_t(*p++);
    g.turns = uint16_t(p[0] | p[1] << 8);
    p += 2;
    memcpy(g.played, p, sizeof g.played);
    if (g.players < 2 || g.players > kMaxPlayers || g.round >= kRounds || g.turn >= g.players || ph > 3) return false;
    if (g.puzzle < 0 || g.puzzle >= kPhraseCount || g.called >> 26 || g.wedge < 0 || g.wedge >= kWedges) return false;
    if (g.round_winner < -1 || g.round_winner >= g.players) return false;
    g.phase = Phase(ph);
    if (g.phase == Phase::Consonant && g.value <= 0) return false;
    for (int i = 0; i < kMaxPlayers; ++i) if (g.money[i] < 0 || g.bank[i] < 0) return false;
    *this = g;
    return true;
}

// ---- Computer ---------------------------------------------------------------------------------

namespace {

int kEasySolve = 30, kMediumSolve = 38, kHardSolve = 45, kHardSure = 55;   // % of the letters hidden

const char kConsonants[] = "TNSRHLDCMGPBFYWKVXZJQ";   // most common first
const char kVowels[] = "EAOIU";

// Does phrase `t` fit the board of `g` (same shape, the shown letters, no called letter hidden)?
bool fits(const Game& g, const char* t)
{
    const char* a = g.text();
    for (; *a && *t; ++a, ++t) {
        if (!is_letter(*a)) { if (*t != *a) return false; continue; }
        if (!is_letter(*t)) return false;
        if (g.called_letter(*a)) { if (*t != *a) return false; }
        else if (g.called_letter(*t)) return false;
    }
    return !*a && !*t;
}

// How many of the fitting puzzles have each letter hidden in them
void candidate_counts(const Game& g, int counts[26])
{
    memset(counts, 0, 26 * sizeof(int));
    const uint8_t cat = kPhrases[g.puzzle].cat;
    for (int i = 0; i < kPhraseCount; ++i) {
        if (kPhrases[i].cat != cat || !fits(g, kPhrases[i].text)) continue;
        uint32_t seen = 0;
        for (const char* t = kPhrases[i].text; *t; ++t)
            if (is_letter(*t) && !g.called_letter(*t)) seen |= 1u << (*t - 'A');
        for (int k = 0; k < 26; ++k) counts[k] += (seen >> k) & 1;
    }
}

char by_counts(const Game& g, const char* order)
{
    int counts[26];
    candidate_counts(g, counts);
    char best = 0;
    int most = 0;
    for (const char* c = order; *c; ++c)
        if (!g.called_letter(*c) && counts[*c - 'A'] > most) { most = counts[*c - 'A']; best = *c; }
    return best;
}

char first_uncalled(const Game& g, const char* order, int among, uint32_t seed)
{
    char pool[26];
    int n = 0;
    for (const char* c = order; *c && n < among; ++c)
        if (!g.called_letter(*c)) pool[n++] = *c;
    if (!n) return 0;
    return pool[(seed >> 8) % uint32_t(n)];
}

} // namespace

int fitting(const Game& g, uint32_t seed, int* pick)
{
    const uint8_t cat = kPhrases[g.puzzle].cat;
    int n = 0;
    for (int i = 0; i < kPhraseCount; ++i) {
        if (kPhrases[i].cat != cat || !fits(g, kPhrases[i].text)) continue;
        ++n;
        seed = seed * 1103515245u + 12345u;
        if (pick && (seed >> 8) % uint32_t(n) == 0) *pick = i;     // one of them, evenly
    }
    return n;
}

void guess_letters(const Game& g, uint32_t seed, char* out, size_t cap)
{
    int pick = g.puzzle;
    fitting(g, seed, &pick);
    size_t n = 0;
    for (const char* t = kPhrases[pick].text, *a = g.text(); *t && *a && n + 1 < cap; ++t, ++a)
        if (is_letter(*a) && !g.called_letter(*a)) out[n++] = *t;
    out[n] = 0;
}

Act decide(const Game& g, int level, uint32_t seed)
{
    const int hidden = g.hidden(), total = g.letters();
    const bool spin_ok = g.can_spin(), buy_ok = g.can_buy();
    if (!spin_ok && !buy_ok) return Act::Solve;
    // Solve once enough shows: Easy with 30 % hidden, Medium 38 %, Hard 45 %
    // - or 60 % when only one puzzle it knows fits
    // (give or take 10 %, so a computer isn't too predictable)
    const int limit = (level == 0 ? kEasySolve : level == 1 ? kMediumSolve : kHardSolve) + int((seed >> 8) % 21) - 10;
    if (hidden * 100 <= total * limit) return Act::Solve;
    if (level >= 2 && hidden * 100 <= total * kHardSure && fitting(g, seed) == 1) return Act::Solve;
    if (!spin_ok) return Act::Buy;
    // Buy a vowel with money to spare (Easy only when nothing else is left)
    const int32_t m = g.money[g.turn];
    if (buy_ok && level >= 1 && m >= (level == 1 ? 750 : 500)) return Act::Buy;
    return Act::Spin;
}

char pick_consonant(const Game& g, int level, uint32_t seed)
{
    char c = 0;
    if (level >= 2) c = by_counts(g, kConsonants);
    if (!c) c = first_uncalled(g, kConsonants, level == 0 ? 8 : 1, seed);
    return c;
}

char pick_vowel(const Game& g, int level, uint32_t seed)
{
    char c = 0;
    if (level >= 2) c = by_counts(g, kVowels);
    if (!c) c = first_uncalled(g, kVowels, level == 0 ? 3 : 1, seed);
    return c;
}

} // namespace wheel
