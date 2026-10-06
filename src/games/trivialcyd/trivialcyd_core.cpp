#include "trivialcyd_core.h"

#include <cstdio>
#include <cstring>
#include "../common/trivia_bank.h"

namespace tcyd {

const char* const kColorNames[kColors] = {"Geography", "Entertainment", "History", "Arts & Literature",
                                          "Science & Nature", "Sports & Leisure"};

namespace {
// Bank categories (trivia_bank.cpp order) -> colour
const uint8_t kColorOf[trivia::kCategories] = {
    5,  // General Knowledge -> Sports & Leisure
    3,  // Books
    1,  // Film
    1,  // Music
    1,  // Musicals & Theatre
    1,  // Television
    1,  // Video Games
    1,  // Board Games
    4,  // Science & Nature
    4,  // Computers
    4,  // Mathematics
    2,  // Mythology
    5,  // Sports
    0,  // Geography
    2,  // History
    2,  // Politics
    3,  // Art
    1,  // Celebrities
    4,  // Animals
    5,  // Vehicles
    3,  // Comics
    4,  // Gadgets
    1,  // Anime & Manga
    1,  // Cartoons
};

// The chance a computer knows an answer: [level][difficulty] in percent
const int kKnows[3][3] = {{45, 30, 20}, {65, 48, 32}, {80, 62, 45}};

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}
} // namespace

int  color_of(int c) { return (c >= 0 && c < trivia::kCategories) ? kColorOf[c] : 5; }
bool is_hq(int sq) { return sq % 6 == 0; }

int square_color(int sq)
{
    const int k = sq / 6, j = sq % 6;
    if (j == 0) return k;
    if (j == 3) return kRollAgain;
    return (k + j) % kColors;                 // j = 1, 2, 4, 5: the other colours
}

uint32_t Game::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Game::start(uint32_t seed, int np, int pe, int lv)
{
    uint8_t keep[sizeof played];
    memcpy(keep, played, sizeof keep);
    *this = Game{};
    memcpy(played, keep, sizeof keep);
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
    players = uint8_t(np < 2 ? 2 : np > kMaxPlayers ? kMaxPlayers : np);
    people = uint8_t(pe < 1 ? 1 : pe > players ? players : pe);
    level = uint8_t(lv < 0 ? 0 : lv > 2 ? 2 : lv);
    for (int p = 0; p < kMaxPlayers; ++p) pos[p] = 3;          // everyone starts on a Roll Again square
    phase = Phase::Roll;
}

int Game::wedge_count(int p) const
{
    int n = 0;
    for (int c = 0; c < kColors; ++c) n += wedges[p] >> c & 1;
    return n;
}

int Game::right_slot() const
{
    for (int s = 0; s < answers; ++s) if (order[s] == 0) return s;
    return 0;
}

// A fresh question in this colour (any kind, mostly easy and medium)
void Game::ask(int color)
{
    const int n = trivia::count() < kBankMax ? trivia::count() : kBankMax;
    const uint32_t r = rand_next() % 20;
    const int want = r < 8 ? trivia::kEasy : r < 17 ? trivia::kMedium : trivia::kHard;
    q = -1;
    for (int pass = 0; pass < 2 && q < 0; ++pass) {
        for (int d = 0; d < 3 && q < 0; ++d) {
            const int diff = (want + d) % 3;
            int count = 0;
            for (int i = 0; i < n; ++i)
                if (color_of(trivia::category(i)) == color && trivia::difficulty(i) == diff && !(played[i / 8] >> (i % 8) & 1)) ++count;
            if (!count) continue;
            int k = int(rand_next() % uint32_t(count));
            for (int i = 0; i < n; ++i) {
                if (color_of(trivia::category(i)) != color || trivia::difficulty(i) != diff || (played[i / 8] >> (i % 8) & 1)) continue;
                if (k-- == 0) { q = int16_t(i); played[i / 8] |= uint8_t(1 << (i % 8)); break; }
            }
        }
        if (q < 0)                                               // the colour ran out: start it over
            for (int i = 0; i < n; ++i) if (color_of(trivia::category(i)) == color) played[i / 8] &= uint8_t(~(1 << (i % 8)));
    }
    q_color = uint8_t(color);
    answers = uint8_t(q >= 0 && trivia::true_false(q) ? 2 : 4);
    for (int s = 0; s < 4; ++s) order[s] = uint8_t(s);
    for (int s = answers - 1; s > 0; --s) {
        const int j = int(rand_next() % uint32_t(s + 1));
        const uint8_t t = order[s]; order[s] = order[j]; order[j] = t;
    }
    answered = -1;
    right = won_wedge = false;
    phase = Phase::Ask;
}

bool Game::roll()
{
    if (phase != Phase::Roll || winner >= 0) return false;
    if (wedge_count(turn) == kColors) {                          // the winning question
        final_q = true;
        ask(int(rand_next() % kColors));
        return true;
    }
    final_q = false;
    die = uint8_t(1 + rand_next() % 6);
    dest[0] = int8_t((pos[turn] + die) % kSquares);
    dest[1] = int8_t((pos[turn] + kSquares - die) % kSquares);
    phase = Phase::Move;
    return true;
}

bool Game::move(int sq)
{
    if (phase != Phase::Move || (sq != dest[0] && sq != dest[1])) return false;
    pos[turn] = uint8_t(sq);
    const int c = square_color(sq);
    if (c == kRollAgain) { phase = Phase::Roll; ++turns; return true; }
    ask(c);
    return true;
}

bool Game::answer(int slot)
{
    if (phase != Phase::Ask || slot < 0 || slot >= answers) return false;
    answered = int8_t(slot);
    right = slot == right_slot();
    if (right && final_q) winner = int8_t(turn);
    else if (right && is_hq(pos[turn]) && !(wedges[turn] >> q_color & 1)) {
        wedges[turn] |= uint8_t(1 << q_color);
        won_wedge = true;
    }
    phase = Phase::Reveal;
    return true;
}

bool Game::next()
{
    if (phase != Phase::Reveal) return false;
    ++turns;
    if (winner >= 0) { phase = Phase::Over; return true; }
    if (!right) turn = uint8_t((turn + 1) % players);
    phase = Phase::Roll;
    final_q = false;
    return true;
}

// ---- Computers ------------------------------------------------------------------------------------
int Game::cpu_move() const
{
    int best = dest[0];
    int best_score = -1000;
    for (int k = 0; k < 2; ++k) {
        const int sq = dest[k];
        const int c = square_color(sq);
        int s = 0;
        if (c == kRollAgain) s = 3;
        else if (is_hq(sq) && !(wedges[turn] >> c & 1)) s = level == 0 ? 4 : 10;
        else s = 1;
        if (s > best_score) { best_score = s; best = sq; }
    }
    return best;
}

int Game::cpu_answer()
{
    const int d = q >= 0 ? trivia::difficulty(q) : 1;
    if (int(rand_next() % 100) < kKnows[level][d]) return right_slot();
    return int(rand_next() % answers);                           // a guess (sometimes lucky)
}

// ---- Save ---------------------------------------------------------------------------------------
size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "TCY1", 4); n = 4;
    buf[n++] = players;
    buf[n++] = people;
    buf[n++] = level;
    memcpy(buf + n, pos, kMaxPlayers); n += kMaxPlayers;
    memcpy(buf + n, wedges, kMaxPlayers); n += kMaxPlayers;
    buf[n++] = turn;
    buf[n++] = uint8_t(phase);
    buf[n++] = die;
    buf[n++] = uint8_t(dest[0]); buf[n++] = uint8_t(dest[1]);
    buf[n++] = uint8_t(q); buf[n++] = uint8_t(uint16_t(q) >> 8);
    buf[n++] = q_color;
    memcpy(buf + n, order, 4); n += 4;
    buf[n++] = answers;
    buf[n++] = final_q ? 1 : 0;
    buf[n++] = uint8_t(answered);
    buf[n++] = right ? 1 : 0;
    buf[n++] = won_wedge ? 1 : 0;
    buf[n++] = uint8_t(winner);
    buf[n++] = uint8_t(turns); buf[n++] = uint8_t(turns >> 8);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    memcpy(buf + n, played, sizeof played); n += sizeof played;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "TCY1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    g.players = buf[n++];
    g.people = buf[n++];
    g.level = buf[n++];
    memcpy(g.pos, buf + n, kMaxPlayers); n += kMaxPlayers;
    memcpy(g.wedges, buf + n, kMaxPlayers); n += kMaxPlayers;
    g.turn = buf[n++];
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.die = buf[n++];
    g.dest[0] = int8_t(buf[n++]); g.dest[1] = int8_t(buf[n++]);
    g.q = int16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.q_color = buf[n++];
    memcpy(g.order, buf + n, 4); n += 4;
    g.answers = buf[n++];
    g.final_q = buf[n++] != 0;
    g.answered = int8_t(buf[n++]);
    g.right = buf[n++] != 0;
    g.won_wedge = buf[n++] != 0;
    g.winner = int8_t(buf[n++]);
    g.turns = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (!g.rng) g.rng = 1;
    memcpy(g.played, buf + n, sizeof g.played);
    if (g.players < 2 || g.players > kMaxPlayers || g.people < 1 || g.people > g.players || g.level > 2) return false;
    for (int p = 0; p < kMaxPlayers; ++p) if (g.pos[p] >= kSquares || g.wedges[p] > 63) return false;
    if (g.turn >= g.players || g.die > 6 || g.q < -1 || g.q >= trivia::count() || g.q_color >= kColors) return false;
    if (g.dest[0] < -1 || g.dest[0] >= kSquares || g.dest[1] < -1 || g.dest[1] >= kSquares) return false;
    if (g.answers != 2 && g.answers != 4) return false;
    bool seen[4] = {};
    for (int s = 0; s < g.answers; ++s) { if (g.order[s] >= g.answers || seen[g.order[s]]) return false; seen[g.order[s]] = true; }
    if (g.answered < -1 || g.answered >= g.answers || g.winner < -1 || g.winner >= g.players) return false;
    if ((g.phase == Phase::Ask || g.phase == Phase::Reveal) && g.q < 0) return false;
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Wedges,Level,Seconds,Time";

namespace {
const char* const kLevelNames[3] = {"Easy", "Medium", "Hard"};
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%u,%s,%lu,%s\n", unsigned(r.place), unsigned(r.wedges),
                           kLevelNames[r.level < 3 ? r.level : 0], (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    unsigned place = 0, wedges = 0;
    char lv[12] = {}, tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%u,%11[^,],%lu,%15[^,\r\n]", &seq, &place, &wedges, lv, &secs, tm) != 6) return false;
    out.place = uint8_t(place);
    out.wedges = uint8_t(wedges);
    out.level = 0;
    for (int i = 0; i < 3; ++i) if (!strcmp(lv, kLevelNames[i])) out.level = uint8_t(i);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    wins += r.place == 1;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace tcyd
