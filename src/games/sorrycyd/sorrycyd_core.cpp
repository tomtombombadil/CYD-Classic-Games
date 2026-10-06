#include "sorrycyd_core.h"

#include <cstdio>
#include <cstring>

namespace sorry {

namespace {

constexpr uint8_t kCards[11] = {1, 2, 3, 4, 5, 7, 8, 10, 11, 12, kSorry};

int progress_of(int c, int square)
{
    const int r = (square - 15 * c + 2 * kTrack) % kTrack;
    return r == 3 ? -1 : (r - 4 + kTrack) % kTrack;
}

float card_p(int k)
{
    if (k == 1) return 5.0f / kDeck;
    if (k == 6 || k == 9 || k < 1 || k > kSorry) return 0.0f;
    return 4.0f / kDeck;
}

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

} // namespace

bool is_track(int p) { return p >= -1 && p <= 58; }

int track_square(int c, int p)
{
    if (!is_track(p)) return -1;
    return (15 * c + 4 + p + kTrack) % kTrack;
}

int slide_len(int square, int c)
{
    const int side = square / 15, r = square % 15;
    if (side == c) return 0;
    if (r == 1) return 3;      // r 1 -> 4
    if (r == 9) return 4;      // r 9 -> 13
    return 0;
}

const char* card_text(uint8_t card)
{
    switch (card) {
        case 1:  return "Start a pawn or move 1";
        case 2:  return "Start a pawn or move 2; draw again";
        case 3:  return "Move 3";
        case 4:  return "Move 4 back";
        case 5:  return "Move 5";
        case 7:  return "Move 7, or split it over two pawns";
        case 8:  return "Move 8";
        case 10: return "Move 10, or 1 back";
        case 11: return "Move 11, or switch places";
        case 12: return "Move 12";
        case kSorry: return "From Start onto another pawn";
    }
    return "";
}

Game::Game()
{
    for (auto& c : pos) for (auto& p : c) p = kStart;
}

uint32_t Game::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Game::shuffle()
{
    deck_n = 0;
    for (uint8_t k : kCards)
        for (int i = 0; i < (k == 1 ? 5 : 4); ++i) deck[deck_n++] = k;
    for (int i = deck_n - 1; i > 0; --i) {
        const int j = int(rand_next() % uint32_t(i + 1));
        const uint8_t t = deck[i]; deck[i] = deck[j]; deck[j] = t;
    }
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
    shuffle();
}

uint8_t Game::draw()
{
    if (phase != Phase::Draw) return card;
    if (deck_n == 0) shuffle();
    card = deck[--deck_n];
    phase = Phase::Play;
    return card;
}

int Game::home_count(int c) const
{
    int n = 0;
    for (int p = 0; p < kPawns; ++p) n += pos[c][p] == kHome;
    return n;
}

int Game::progress_sum(int c) const
{
    int s = 0;
    for (int p = 0; p < kPawns; ++p) s += pos[c][p] == kStart ? -1 : pos[c][p] + 1;
    return s;
}

bool Game::occupied(int c, int p, int* oc, int* op) const
{
    const int sq = track_square(c, pos[c][p]);
    if (sq < 0) return false;
    for (int c2 = 0; c2 < kColors; ++c2)
        for (int p2 = 0; p2 < kPawns; ++p2)
            if ((c2 != c || p2 != p) && track_square(c2, pos[c2][p2]) == sq) { *oc = c2; *op = p2; return true; }
    return false;
}

// Move pawn p of colour c to progress `to` (bumps, slides); false = not allowed
bool Game::step(int c, int p, int to, Last* l)
{
    if (to == kHome) { pos[c][p] = kHome; if (l) l->home = 1; return true; }
    if (to > kHome || to < -1) return false;
    for (int q = 0; q < kPawns; ++q)                         // never onto your own pawn
        if (q != p && pos[c][q] == to && to != kHome) return false;
    if (to >= kSafe0) { pos[c][p] = int8_t(to); return true; }
    const int sq = track_square(c, to);
    for (int q = 0; q < kPawns; ++q)
        if (q != p && track_square(c, pos[c][q]) == sq) return false;
    auto bump_at = [&](int s) {
        for (int c2 = 0; c2 < kColors; ++c2)
            for (int p2 = 0; p2 < kPawns; ++p2)
                if ((c2 != c || p2 != p) && track_square(c2, pos[c2][p2]) == s) {
                    pos[c2][p2] = kStart;
                    if (l) l->bumped |= uint16_t(1u << (c2 * 4 + p2));
                }
    };
    bump_at(sq);
    pos[c][p] = int8_t(to);
    const int len = slide_len(sq, c);
    if (len) {
        for (int k = 1; k <= len; ++k) bump_at((sq + k) % kTrack);
        pos[c][p] = int8_t(progress_of(c, (sq + len) % kTrack));
        if (l) l->slid = 1;
    }
    return true;
}

bool Game::apply(int c, const Move& m, Last* l)
{
    switch (m.kind) {
        case Kind::Pass: return true;
        case Kind::Move: return step(c, m.pawn, m.to, l);
        case Kind::Split: {
            if (m.pawn == m.pawn2) return false;
            if (!step(c, m.pawn, m.to, l)) return false;
            const int from = pos[c][m.pawn2];
            if (from == kStart || from == kHome || m.to2 <= from) return false;
            return step(c, m.pawn2, m.to2, l);
        }
        case Kind::Switch: {
            const int a = track_square(c, pos[c][m.pawn]), b = track_square(m.oc, pos[m.oc][m.op]);
            if (a < 0 || b < 0 || m.oc == c) return false;
            pos[m.oc][m.op] = int8_t(progress_of(m.oc, a));
            pos[c][m.pawn] = int8_t(progress_of(c, b));
            const int len = slide_len(b, c);
            if (len) {                                        // switched onto a slide: slide
                pos[c][m.pawn] = kStart;                      // off the board while it moves
                for (int k = 1; k <= len; ++k)
                    for (int c2 = 0; c2 < kColors; ++c2)
                        for (int p2 = 0; p2 < kPawns; ++p2)
                            if (track_square(c2, pos[c2][p2]) == (b + k) % kTrack) {
                                pos[c2][p2] = kStart;
                                if (l) l->bumped |= uint16_t(1u << (c2 * 4 + p2));
                            }
                pos[c][m.pawn] = int8_t(progress_of(c, (b + len) % kTrack));
                if (l) l->slid = 1;
            }
            return true;
        }
        case Kind::Sorry: {
            if (pos[c][m.pawn] != kStart || m.oc == c) return false;
            const int b = track_square(m.oc, pos[m.oc][m.op]);
            if (b < 0) return false;
            pos[m.oc][m.op] = kStart;
            if (l) l->bumped |= uint16_t(1u << (m.oc * 4 + m.op));
            return step(c, m.pawn, progress_of(c, b), l);
        }
    }
    return false;
}

int Game::moves(Move* out, int cap) const
{
    if (phase != Phase::Play || !card) return 0;
    const int c = turn;
    int n = 0;
    auto add = [&](const Move& m) {
        if (n >= cap) return;
        Game g = *this;
        if (g.apply(c, m, nullptr)) out[n++] = m;
    };
    auto forward = [&](int k) {
        for (int p = 0; p < kPawns; ++p) {
            const int f = pos[c][p];
            if (f == kStart || f == kHome || f + k > kHome) continue;
            Move m; m.kind = Kind::Move; m.pawn = int8_t(p); m.to = int8_t(f + k);
            add(m);
        }
    };
    auto backward = [&](int k) {
        for (int p = 0; p < kPawns; ++p) {
            const int f = pos[c][p];
            if (f == kStart || f == kHome) continue;
            int to;
            if (f >= kSafe0) to = f - k;
            else to = progress_of(c, (track_square(c, f) - k + kTrack) % kTrack);
            Move m; m.kind = Kind::Move; m.pawn = int8_t(p); m.to = int8_t(to);
            add(m);
        }
    };
    auto start_one = [&]() {
        for (int p = 0; p < kPawns; ++p)
            if (pos[c][p] == kStart) { Move m; m.kind = Kind::Move; m.pawn = int8_t(p); m.to = 0; add(m); return; }
    };
    switch (card) {
        case 1: case 2: start_one(); forward(card); break;
        case 4: backward(4); break;
        case 10: forward(10); backward(1); break;
        case 7:
            forward(7);
            for (int a = 0; a < kPawns; ++a)
                for (int b = 0; b < kPawns; ++b) {
                    if (a == b) continue;
                    const int fa = pos[c][a], fb = pos[c][b];
                    if (fa == kStart || fa == kHome || fb == kStart || fb == kHome) continue;
                    for (int k = 1; k <= 6; ++k) {
                        if (fa + k > kHome || fb + 7 - k > kHome) continue;
                        Move m; m.kind = Kind::Split; m.pawn = int8_t(a); m.to = int8_t(fa + k);
                        m.pawn2 = int8_t(b); m.to2 = int8_t(fb + 7 - k);
                        add(m);
                    }
                }
            break;
        case 11:
            forward(11);
            for (int p = 0; p < kPawns; ++p) {
                if (!is_track(pos[c][p])) continue;
                for (int oc = 0; oc < kColors; ++oc) {
                    if (oc == c) continue;
                    for (int op = 0; op < kPawns; ++op)
                        if (is_track(pos[oc][op])) {
                            Move m; m.kind = Kind::Switch; m.pawn = int8_t(p); m.oc = int8_t(oc); m.op = int8_t(op);
                            add(m);
                        }
                }
            }
            break;
        case kSorry: {
            int p = -1;
            for (int q = 0; q < kPawns && p < 0; ++q) if (pos[c][q] == kStart) p = q;
            if (p < 0) break;
            for (int oc = 0; oc < kColors; ++oc) {
                if (oc == c) continue;
                for (int op = 0; op < kPawns; ++op)
                    if (is_track(pos[oc][op])) {
                        Move m; m.kind = Kind::Sorry; m.pawn = int8_t(p); m.oc = int8_t(oc); m.op = int8_t(op);
                        add(m);
                    }
            }
            break;
        }
        default: forward(card); break;
    }
    return n;
}

bool Game::may_pass() const
{
    if (phase != Phase::Play || card != 11) return false;
    Move ms[96];
    const int n = moves(ms, 96);
    for (int i = 0; i < n; ++i) if (ms[i].kind == Kind::Move) return false;
    return true;
}

void Game::next_turn()
{
    turn = uint8_t((turn + 1) % kColors);
    ++turns;
    phase = Phase::Draw;
    card = 0;
}

void Game::lose_turn()
{
    if (phase != Phase::Play) return;
    last = Last{};
    last.color = int8_t(turn);
    last.card = card;
    last.kind = Kind::Pass;
    if (card == 2) { phase = Phase::Draw; card = 0; return; }
    next_turn();
}

bool Game::play(const Move& m)
{
    if (phase != Phase::Play) return false;
    Move ms[96];
    const int n = moves(ms, 96);
    if (m.kind == Kind::Pass) {
        if (n > 0 && !may_pass()) return false;
        lose_turn();
        return true;
    }
    bool ok = false;
    for (int i = 0; i < n && !ok; ++i)
        ok = ms[i].kind == m.kind && ms[i].pawn == m.pawn && ms[i].to == m.to && ms[i].pawn2 == m.pawn2 &&
             ms[i].to2 == m.to2 && ms[i].oc == m.oc && ms[i].op == m.op;
    if (!ok) return false;
    Last l;
    l.color = int8_t(turn);
    l.card = card;
    l.kind = m.kind;
    l.pawn = m.pawn;
    l.pawn2 = m.kind == Kind::Split ? m.pawn2 : int8_t(-1);
    if (!apply(turn, m, &l)) return false;
    last = l;
    if (home_count(turn) == kPawns) { winner = int8_t(turn); phase = Phase::Over; card = 0; return true; }
    if (card == 2) { phase = Phase::Draw; card = 0; return true; }
    next_turn();
    return true;
}

// ---- Computer -----------------------------------------------------------------------------------
namespace {

float value(int p)
{
    if (p == kStart) return 0.0f;
    if (p == kHome) return 90.0f;
    if (p >= kSafe0) return 72.0f + float(p - kSafe0) * 2.0f;
    return 6.0f + float(p + 1);
}

// The chance one of the other colours sends this pawn back on their next card
float danger(const Game& g, int c, int p)
{
    const int sq = track_square(c, g.pos[c][p]);
    if (sq < 0) return 0.0f;
    float d = 0.0f;
    for (int oc = 0; oc < kColors; ++oc) {
        if (oc == c) continue;
        bool in_start = false;
        for (int op = 0; op < kPawns; ++op) {
            const int f = g.pos[oc][op];
            if (f == kStart) { in_start = true; continue; }
            const int s = track_square(oc, f);
            if (s < 0) continue;
            const int ahead = (sq - s + kTrack) % kTrack;     // steps forward to us
            if (ahead > 0 && ahead <= 12 && f + ahead <= 58) d += card_p(ahead) * (ahead == 7 ? 1.3f : 1.0f);
            const int behind = (s - sq + kTrack) % kTrack;    // they are this far ahead
            if (behind == 4) d += card_p(4);
            if (behind == 1) d += card_p(10) * 0.5f;
        }
        // From their Start: onto their start square, or a Sorry!
        if (in_start) {
            if (sq == track_square(oc, 0)) d += card_p(1) + card_p(2);
            d += card_p(kSorry) * 0.3f;
        }
    }
    return d > 0.9f ? 0.9f : d;
}

float score(const Game& g, int c, int level)
{
    float mine = 0.0f, opp = 0.0f;
    for (int p = 0; p < kPawns; ++p) {
        mine += value(g.pos[c][p]);
        if (level >= 1) mine -= value(g.pos[c][p]) * danger(g, c, p);
    }
    if (g.home_count(c) == kPawns) mine += 1000.0f;
    if (level == 0) return mine;
    for (int oc = 0; oc < kColors; ++oc) {
        if (oc == c) continue;
        float v = 0.0f;
        for (int p = 0; p < kPawns; ++p) v += value(g.pos[oc][p]);
        opp += v;
    }
    return mine - 0.3f * opp;
}

// The best score this colour can reach with its next card (the card drawn now)
float best_after(const Game& g0, int c, int level)
{
    Move ms[96];
    const int n = g0.moves(ms, 96);
    float best = score(g0, c, level);                         // no move = stay
    for (int i = 0; i < n; ++i) {
        Game g = g0;
        g.apply(c, ms[i], nullptr);
        const float s = score(g, c, level);
        if (s > best || i == 0) best = s;
    }
    return best;
}

float lookahead(const Game& g0, int c, int level)
{
    if (g0.home_count(c) == kPawns) return score(g0, c, level);
    float e = 0.0f;
    for (uint8_t k : kCards) {
        Game g = g0;
        g.turn = uint8_t(c); g.phase = Phase::Play; g.card = k;
        e += card_p(k) * best_after(g, c, level);
    }
    return e;
}

} // namespace

Move Game::ai_move(int level)
{
    Move ms[96];
    int n = moves(ms, 96);
    Move pass;
    if (n == 0) return pass;
    const bool can_pass = may_pass();
    float best = -1e9f;
    Move pick = ms[0];
    int ties = 0;
    for (int i = 0; i <= n; ++i) {
        if (i == n && !can_pass) break;
        const Move& m = i < n ? ms[i] : pass;
        Game g = *this;
        g.apply(turn, m, nullptr);
        const float s = level >= 2 ? lookahead(g, turn, 1) : score(g, turn, level);
        if (s > best + 0.01f) { best = s; pick = m; ties = 1; }
        else if (s > best - 0.01f && rand_next() % uint32_t(++ties) == 0) pick = m;
    }
    return pick;
}

// ---- Save ---------------------------------------------------------------------------------------
size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "SRY1", 4); n = 4;
    for (const auto& c : pos) for (int8_t p : c) buf[n++] = uint8_t(p);
    memcpy(buf + n, deck, kDeck); n += kDeck;
    buf[n++] = deck_n;
    buf[n++] = card;
    buf[n++] = uint8_t(phase);
    buf[n++] = turn;
    buf[n++] = uint8_t(winner);
    buf[n++] = uint8_t(turns); buf[n++] = uint8_t(turns >> 8);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    buf[n++] = uint8_t(last.color);
    buf[n++] = last.card;
    buf[n++] = uint8_t(last.kind);
    buf[n++] = uint8_t(last.pawn);
    buf[n++] = uint8_t(last.pawn2);
    buf[n++] = uint8_t(last.bumped); buf[n++] = uint8_t(last.bumped >> 8);
    buf[n++] = last.slid;
    buf[n++] = last.home;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "SRY1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    for (auto& c : g.pos)
        for (int8_t& p : c) {
            p = int8_t(buf[n++]);
            if (p != kStart && (p < -1 || p > kHome)) return false;
        }
    memcpy(g.deck, buf + n, kDeck); n += kDeck;
    g.deck_n = buf[n++];
    if (g.deck_n > kDeck) return false;
    for (int i = 0; i < g.deck_n; ++i) {
        const uint8_t k = g.deck[i];
        if (k < 1 || k > kSorry || k == 6 || k == 9) return false;
    }
    g.card = buf[n++];
    if (g.card > kSorry || g.card == 6 || g.card == 9) return false;
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.turn = buf[n++];
    g.winner = int8_t(buf[n++]);
    if (g.turn >= kColors || g.winner < -1 || g.winner >= kColors) return false;
    if (g.phase == Phase::Play && g.card == 0) return false;
    g.turns = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (!g.rng) g.rng = 1;
    g.last.color = int8_t(buf[n++]);
    g.last.card = buf[n++];
    if (buf[n] > uint8_t(Kind::Pass)) return false;
    g.last.kind = Kind(buf[n++]);
    g.last.pawn = int8_t(buf[n++]);
    g.last.pawn2 = int8_t(buf[n++]);
    g.last.bumped = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.last.slid = buf[n++];
    g.last.home = buf[n++];
    if (g.last.color < -1 || g.last.color >= kColors || g.last.pawn < -1 || g.last.pawn >= kPawns ||
        g.last.pawn2 < -1 || g.last.pawn2 >= kPawns) return false;
    // no two pawns on one square
    for (int c = 0; c < kColors; ++c)
        for (int p = 0; p < kPawns; ++p) {
            int oc, op;
            if (g.occupied(c, p, &oc, &op)) return false;
            if (g.pos[c][p] >= kSafe0 && g.pos[c][p] < kHome)
                for (int q = p + 1; q < kPawns; ++q) if (g.pos[c][q] == g.pos[c][p]) return false;
        }
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Home,Level,Seconds,Time";

namespace {
const char* const kLevelNames[3] = {"Easy", "Medium", "Hard"};
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%u,%s,%lu,%s\n", unsigned(r.place), unsigned(r.home),
                           kLevelNames[r.level < 3 ? r.level : 0], (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    unsigned place = 0, home = 0;
    char lv[12] = {}, tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%u,%11[^,],%lu,%15[^,\r\n]", &seq, &place, &home, lv, &secs, tm) != 6) return false;
    out.place = uint8_t(place);
    out.home = uint8_t(home);
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

} // namespace sorry
