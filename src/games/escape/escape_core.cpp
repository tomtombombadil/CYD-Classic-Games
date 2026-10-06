#include "escape_core.h"

#include <cstdio>
#include <cstring>

namespace escape {

namespace {

constexpr int kCentre = 5 * kCols + 4;                 // col 4, row 5
const int8_t kSafeHexes[4] = {0, 8, 90, 98};             // the four corners
const int8_t kSerpentStart[2] = {4, 94};                // the middles of the top and bottom edges

void cube(int h, int* x, int* z)
{
    const int c = h % kCols, r = h / kCols;
    *x = c - (r - (r & 1)) / 2;
    *z = r;
}

int iabs(int v) { return v < 0 ? -v : v; }

void fmt_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

} // namespace

int hex_at(int c, int r) { return (c < 0 || c >= kCols || r < 0 || r >= kRows) ? -1 : r * kCols + c; }
int col_of(int h) { return h % kCols; }
int row_of(int h) { return h / kCols; }

int neighbours(int h, int8_t* out)
{
    const int c = h % kCols, r = h / kCols;
    static const int8_t even[6][2] = {{-1, 0}, {1, 0}, {-1, -1}, {0, -1}, {-1, 1}, {0, 1}};
    static const int8_t odd[6][2]  = {{-1, 0}, {1, 0}, {0, -1}, {1, -1}, {0, 1}, {1, 1}};
    const int8_t (*d)[2] = (r & 1) ? odd : even;
    int n = 0;
    for (int k = 0; k < 6; ++k) {
        const int t = hex_at(c + d[k][0], r + d[k][1]);
        if (t >= 0) out[n++] = int8_t(t);
    }
    return n;
}

int distance(int a, int b)
{
    int ax, az, bx, bz;
    cube(a, &ax, &az);
    cube(b, &bx, &bz);
    const int dx = ax - bx, dz = az - bz, dy = -dx - dz;
    int m = iabs(dx);
    if (iabs(dy) > m) m = iabs(dy);
    if (iabs(dz) > m) m = iabs(dz);
    return m;
}

const char* kind_name(Kind k) { return k == kShark ? "Shark" : k == kWhale ? "Whale" : "Sea Serpent"; }
int kind_range(Kind k) { return k == kShark ? 2 : k == kWhale ? 3 : 1; }

Game::Game()
{
    for (auto& t : terrain) t = kSea;
    for (auto& u : under) u = kNothing;
    for (auto& b : boat) b = -1;
}

uint32_t Game::rand_next()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

void Game::start(uint32_t seed)
{
    *this = Game{};
    rng = seed ? seed : 1;
    for (int k = 0; k < 3; ++k) rand_next();
    // The island: rings round the centre
    int8_t rings[4][24];
    int rn[4] = {};
    for (int h = 0; h < kHexes; ++h) {
        const int d = distance(h, kCentre);
        if (d <= 3) {
            terrain[h] = d == 3 ? kBeach : d == 2 ? kForest : kMountain;
            const int ring = d == 3 ? 0 : d == 2 ? 1 : 2;
            rings[ring][rn[ring]++] = int8_t(h);
        }
    }
    for (int8_t h : kSafeHexes) terrain[h] = kSafe;
    // Undersides, shuffled within each kind of tile
    static const uint8_t beach[18] = {kSharkTile, kSharkTile, kSharkTile, kSharkTile, kWhaleTile, kWhaleTile, kWhaleTile,
                                      kBoatTile, kBoatTile, kBoatTile, kBoatTile, kWhirlpool, 0, 0, 0, 0, 0, 0};
    static const uint8_t forest[12] = {kSharkTile, kSharkTile, kSharkTile, kWhaleTile, kWhaleTile, kWhaleTile,
                                       kBoatTile, kBoatTile, kBoatTile, kWhirlpool, 0, 0};
    static const uint8_t mountain[7] = {kVolcano, kWhirlpool, kWhirlpool, 0, 0, 0, 0};
    const uint8_t* sets[3] = {beach, forest, mountain};
    for (int ring = 0; ring < 3; ++ring) {
        uint8_t e[24];
        memcpy(e, sets[ring], size_t(rn[ring]));
        for (int i = rn[ring] - 1; i > 0; --i) {
            const int j = int(rand_next() % uint32_t(i + 1));
            const uint8_t t = e[i]; e[i] = e[j]; e[j] = t;
        }
        for (int i = 0; i < rn[ring]; ++i) under[rings[ring][i]] = Effect(e[i]);
    }
    // Explorers: colour c's k-th is index c * kPer + k
    for (int c = 0; c < kColors; ++c)
        for (int k = 0; k < kPer; ++k) {
            Explorer& x = ex[c * kPer + k];
            x.color = uint8_t(c);
            x.value = kValues[k];
        }
    // Eight boats round the island, sea serpents in the middles of the edges
    static const int8_t kBoatStart[8] = {12, 15, 28, 34, 64, 70, 84, 87};
    for (int8_t h : kBoatStart) boat[boats++] = h;
    for (int8_t h : kSerpentStart) { cr[creatures].kind = kSerpent; cr[creatures].hex = h; ++creatures; }
    phase = Phase::Place;
    turn = 0;
}

int Game::land_count(int h) const
{
    int n = 0;
    for (const Explorer& x : ex) n += x.hex == h && x.where == kLand;
    return n;
}

int Game::swim_count(int h) const
{
    int n = 0;
    for (const Explorer& x : ex) n += x.hex == h && x.where == kSwim;
    return n;
}

int Game::boat_at(int h) const
{
    for (int b = 0; b < boats; ++b) if (boat[b] == h) return b;
    return -1;
}

int Game::aboard(int b) const
{
    int n = 0;
    for (const Explorer& x : ex) n += x.where == kAboard && x.boat == b;
    return n;
}

int Game::creature_at(int h) const
{
    for (int k = 0; k < creatures; ++k) if (cr[k].hex == h) return k;
    return -1;
}

bool Game::controls(int c, int b) const
{
    int n[kColors] = {};
    for (const Explorer& x : ex) if (x.where == kAboard && x.boat == b) ++n[x.color];
    int most = 0;
    for (int k = 0; k < kColors; ++k) if (n[k] > most) most = n[k];
    return most == 0 || n[c] == most;
}

int Game::next_to_place() const
{
    if (phase != Phase::Place || placed >= kExplorers) return -1;
    // round by round: everyone's 5, then everyone's 4, ...
    const int c = placed % kColors, k = placed / kColors;
    return c * kPer + k;
}

int Game::score(int c) const
{
    int s = 0;
    for (const Explorer& x : ex) if (x.color == c && x.where == kSaved) s += x.value;
    return s;
}

int Game::on_board(int c) const
{
    int n = 0;
    for (const Explorer& x : ex) n += x.color == c && (x.where == kLand || x.where == kSwim || x.where == kAboard);
    return n;
}

int Game::winner() const
{
    if (phase != Phase::Over) return -1;
    int best = -1, who = -1;
    bool tie = false;
    for (int c = 0; c < kColors; ++c) {
        const int s = score(c);
        if (s > best) { best = s; who = c; tie = false; }
        else if (s == best) tie = true;
    }
    return tie ? -1 : who;
}

int Game::sinkable(int8_t* out) const
{
    int lowest = 99;
    for (int h = 0; h < kHexes; ++h) if (island(h) && terrain[h] < lowest) lowest = terrain[h];
    int n = 0;
    for (int h = 0; h < kHexes; ++h) {
        if (terrain[h] != lowest) continue;
        int8_t nb[6];
        const int k = neighbours(h, nb);
        bool sea = k < 6;
        for (int i = 0; i < k && !sea; ++i) sea = terrain[nb[i]] == kSea;
        if (sea) out[n++] = int8_t(h);
    }
    return n;
}

// Sea hexes creature k can stop on (its range, through the sea, no other creature)
int Game::creature_targets(int k, int8_t* out) const
{
    const int start = cr[k].hex;
    if (start < 0) return 0;
    int8_t dist[kHexes];
    memset(dist, -1, sizeof dist);
    int8_t q[kHexes];
    int qh = 0, qt = 0, n = 0;
    dist[start] = 0;
    q[qt++] = int8_t(start);
    const int range = kind_range(cr[k].kind);
    while (qh < qt) {
        const int h = q[qh++];
        if (dist[h] >= range) continue;
        int8_t nb[6];
        const int m = neighbours(h, nb);
        for (int i = 0; i < m; ++i) {
            const int t = nb[i];
            if (dist[t] >= 0 || terrain[t] != kSea || creature_at(t) >= 0) continue;
            dist[t] = int8_t(dist[h] + 1);
            q[qt++] = int8_t(t);
            out[n++] = int8_t(t);
        }
    }
    return n;
}

int Game::actions(Action* out, int cap) const
{
    int n = 0;
    auto add = [&](Act a, int who, int to) { if (n < cap) { out[n].act = a; out[n].who = int8_t(who); out[n].to = int8_t(to); ++n; } };
    switch (phase) {
        case Phase::Place: {
            if (next_to_place() < 0) break;
            for (int h = 0; h < kHexes; ++h) if (island(h) && land_count(h) == 0) add(kPlace, next_to_place(), h);
            break;
        }
        case Phase::Move: {
            add(kEndMoves, -1, -1);
            if (moves_left == 0) break;
            for (int e = 0; e < kExplorers; ++e) {
                const Explorer& x = ex[e];
                if (x.color != turn) continue;
                if (x.where != kLand && x.where != kSwim && x.where != kAboard) continue;
                if (x.where == kSwim && (swam >> e & 1)) continue;
                int8_t nb[6];
                const int m = neighbours(x.hex, nb);
                for (int i = 0; i < m; ++i) {
                    Action a; a.act = kStepExplorer; a.who = int8_t(e); a.to = nb[i];
                    if (can(a)) add(kStepExplorer, e, nb[i]);
                }
            }
            for (int b = 0; b < boats; ++b) {
                if (boat[b] < 0 || !controls(turn, b)) continue;
                int8_t nb[6];
                const int m = neighbours(boat[b], nb);
                for (int i = 0; i < m; ++i) {
                    Action a; a.act = kStepBoat; a.who = int8_t(b); a.to = nb[i];
                    if (can(a)) add(kStepBoat, b, nb[i]);
                }
            }
            break;
        }
        case Phase::Sink: {
            int8_t s[kHexes];
            const int m = sinkable(s);
            for (int i = 0; i < m; ++i) add(kSinkTile, -1, s[i]);
            break;
        }
        case Phase::Creature: {
            add(kSkipCreature, -1, -1);
            for (int k = 0; k < creatures; ++k) {
                if (cr[k].hex < 0 || cr[k].kind != die) continue;
                int8_t t[kHexes];
                const int m = creature_targets(k, t);
                for (int i = 0; i < m; ++i) add(kMoveCreature, k, t[i]);
            }
            break;
        }
        case Phase::Over: break;
    }
    return n;
}

bool Game::can(const Action& a) const
{
    switch (a.act) {
        case kPlace:
            return phase == Phase::Place && a.who == next_to_place() && a.to >= 0 && a.to < kHexes &&
                   island(a.to) && land_count(a.to) == 0;
        case kEndMoves: return phase == Phase::Move;
        case kStepExplorer: {
            if (phase != Phase::Move || moves_left == 0 || a.who < 0 || a.who >= kExplorers) return false;
            const Explorer& x = ex[a.who];
            if (x.color != turn || a.to < 0 || a.to >= kHexes || distance(x.hex, a.to) != 1) return false;
            if (x.where == kSwim && (swam >> a.who & 1)) return false;
            if (x.where != kLand && x.where != kSwim && x.where != kAboard) return false;
            const Terrain t = terrain[a.to];
            if (t == kSafe) return true;
            if (t == kSea) {
                if (creature_at(a.to) >= 0) return false;
                const int b = boat_at(a.to);
                if (x.where == kAboard && b == x.boat) return false;
                if (b >= 0 && aboard(b) < kSeats) return true;
                return swim_count(a.to) < kStack;
            }
            return x.where == kLand && land_count(a.to) < kStack;     // land: only from land
        }
        case kStepBoat: {
            if (phase != Phase::Move || moves_left == 0 || a.who < 0 || a.who >= boats) return false;
            const int from = boat[a.who];
            if (from < 0 || !controls(turn, a.who) || a.to < 0 || a.to >= kHexes || distance(from, a.to) != 1) return false;
            return terrain[a.to] == kSea && boat_at(a.to) < 0 && creature_at(a.to) < 0;
        }
        case kSinkTile: {
            if (phase != Phase::Sink) return false;
            int8_t s[kHexes];
            const int m = sinkable(s);
            for (int i = 0; i < m; ++i) if (s[i] == a.to) return true;
            return false;
        }
        case kSkipCreature: return phase == Phase::Creature;
        case kMoveCreature: {
            if (phase != Phase::Creature || a.who < 0 || a.who >= creatures || cr[a.who].kind != die) return false;
            int8_t t[kHexes];
            const int m = creature_targets(a.who, t);
            for (int i = 0; i < m; ++i) if (t[i] == a.to) return true;
            return false;
        }
    }
    return false;
}

void Game::lose(int e, uint8_t* count)
{
    ex[e].where = kLost;
    ex[e].hex = -1;
    ex[e].boat = -1;
    ++*count;
}

void Game::check_over()
{
    bool any = false;
    for (const Explorer& x : ex) any = any || x.where == kLand || x.where == kSwim || x.where == kAboard;
    if (!any) phase = Phase::Over;
}

void Game::sink(int h)
{
    terrain[h] = kSea;
    news = News{};
    news.color = int8_t(turn);
    news.sunk = int8_t(h);
    news.effect = under[h];
    for (Explorer& x : ex) if (x.hex == h && x.where == kLand) x.where = kSwim;
    switch (under[h]) {
        case kSharkTile:
        case kWhaleTile: {
            if (creatures >= kMaxCreatures || creature_at(h) >= 0) break;
            cr[creatures].kind = under[h] == kSharkTile ? kShark : kWhale;
            cr[creatures].hex = int8_t(h);
            news.creature = int8_t(creatures);
            ++creatures;
            if (under[h] == kSharkTile)
                for (int e = 0; e < kExplorers; ++e) if (ex[e].hex == h && ex[e].where == kSwim) lose(e, &news.lost);
            break;
        }
        case kBoatTile: {
            if (boats >= kMaxBoats || boat_at(h) >= 0) break;
            const int b = boats++;
            boat[b] = int8_t(h);
            int seats = kSeats;
            for (Explorer& x : ex)
                if (x.hex == h && x.where == kSwim && seats > 0) { x.where = kAboard; x.boat = int8_t(b); --seats; }
            break;
        }
        case kWhirlpool: {
            int8_t nb[6];
            const int m = neighbours(h, nb);
            for (int i = -1; i < m; ++i) {
                const int t = i < 0 ? h : nb[i];
                if (terrain[t] != kSea) continue;
                const int b = boat_at(t);
                if (b >= 0) { boat[b] = -1; ++news.tipped; }
                for (int e = 0; e < kExplorers; ++e)
                    if (ex[e].hex == t && (ex[e].where == kSwim || (ex[e].where == kAboard && ex[e].boat == b))) lose(e, &news.lost);
            }
            break;
        }
        case kVolcano: phase = Phase::Over; break;
        default: break;
    }
    under[h] = kNothing;
}

void Game::creature_effect(int k)
{
    const int h = cr[k].hex;
    const int b = boat_at(h);
    switch (cr[k].kind) {
        case kShark:
            for (int e = 0; e < kExplorers; ++e) if (ex[e].hex == h && ex[e].where == kSwim) lose(e, &news.c_lost);
            break;
        case kWhale:
            if (b >= 0) {
                for (Explorer& x : ex) if (x.where == kAboard && x.boat == b) { x.where = kSwim; x.boat = -1; }
                boat[b] = -1;
                ++news.c_tipped;
            }
            break;
        case kSerpent:
            for (int e = 0; e < kExplorers; ++e)
                if (ex[e].hex == h && (ex[e].where == kSwim || ex[e].where == kAboard)) lose(e, &news.c_lost);
            if (b >= 0) { boat[b] = -1; ++news.c_tipped; }
            break;
    }
}

void Game::roll()
{
    if (phase != Phase::Creature) return;
    die = uint8_t(rand_next() % 3);
}

void Game::end_turn()
{
    check_over();
    if (phase == Phase::Over) return;
    turn = uint8_t((turn + 1) % kColors);
    ++turns;
    phase = Phase::Move;
    moves_left = kMoves;
    swam = 0;
}

bool Game::apply(const Action& a)
{
    if (!can(a)) return false;
    switch (a.act) {
        case kPlace: {
            Explorer& x = ex[a.who];
            x.hex = a.to;
            x.where = kLand;
            ++placed;
            turn = uint8_t(placed % kColors);
            if (placed >= kExplorers) { phase = Phase::Move; turn = 0; moves_left = kMoves; swam = 0; }
            return true;
        }
        case kEndMoves:
            phase = Phase::Sink;
            moves_left = 0;
            return true;
        case kStepExplorer: {
            Explorer& x = ex[a.who];
            const Terrain t = terrain[a.to];
            x.boat = -1;
            if (t == kSafe) { x.where = kSaved; x.hex = a.to; }
            else if (t == kSea) {
                const int b = boat_at(a.to);
                x.hex = a.to;
                if (b >= 0 && aboard(b) < kSeats) { x.where = kAboard; x.boat = int8_t(b); }
                else x.where = kSwim;
                if (x.where == kSwim) swam |= uint64_t(1) << a.who;   // a swimmer's one step this turn is used
            } else { x.hex = a.to; x.where = kLand; }
            --moves_left;
            if (moves_left == 0) phase = Phase::Sink;
            check_over();
            return true;
        }
        case kStepBoat: {
            const int b = a.who;
            boat[b] = a.to;
            for (Explorer& x : ex) if (x.where == kAboard && x.boat == b) x.hex = a.to;
            int seats = kSeats - aboard(b);
            for (Explorer& x : ex)
                if (x.hex == a.to && x.where == kSwim && seats > 0) { x.where = kAboard; x.boat = int8_t(b); --seats; }
            --moves_left;
            if (moves_left == 0) phase = Phase::Sink;
            return true;
        }
        case kSinkTile:
            sink(a.to);
            if (phase == Phase::Over) return true;
            check_over();
            if (phase == Phase::Over) return true;
            phase = Phase::Creature;
            roll();
            return true;
        case kSkipCreature:
            news.creature = -1;
            end_turn();
            return true;
        case kMoveCreature:
            cr[a.who].hex = a.to;
            news.creature = a.who;
            creature_effect(a.who);
            end_turn();
            return true;
    }
    return false;
}

// ---- Computer -------------------------------------------------------------------------------------
namespace {

int safe_dist(int h)
{
    int best = 99;
    for (int8_t s : kSafeHexes) { const int d = distance(h, s); if (d < best) best = d; }
    return best;
}

// The chance an explorer gets away, roughly (higher = better)
float survive(const Game& g, int e, bool danger)
{
    const Explorer& x = g.ex[e];
    switch (x.where) {
        case kSaved: return 1.0f;
        case kLost: case kUnplaced: return 0.0f;
        default: break;
    }
    const int d = safe_dist(x.hex);
    float p;
    if (x.where == kAboard) p = 0.92f - 0.05f * float(d) - (g.controls(x.color, x.boat) ? 0.0f : 0.08f);
    else if (x.where == kSwim) p = 0.80f - 0.09f * float(d);
    else {
        const Terrain t = g.terrain[x.hex];
        p = (t == kBeach ? 0.55f : t == kForest ? 0.62f : 0.66f) - 0.06f * float(d);
    }
    if (danger && x.where != kLand) {
        for (int k = 0; k < g.creatures; ++k) {
            if (g.cr[k].hex < 0) continue;
            const int dd = distance(g.cr[k].hex, x.hex);
            const Kind kd = g.cr[k].kind;
            if (x.where == kSwim && kd == kShark && dd <= 2) p -= 0.25f;
            if (kd == kSerpent && dd <= 1) p -= 0.25f;
            if (x.where == kAboard && kd == kWhale && dd <= 3) p -= 0.10f;
        }
    }
    return p < 0.02f ? 0.02f : p;
}

float eval(const Game& g, int c, int level)
{
    const bool danger = level >= 1;
    const float others = level >= 1 ? 0.35f : 0.0f;
    float s = 0.0f;
    for (int e = 0; e < kExplorers; ++e) {
        const Explorer& x = g.ex[e];
        if (x.color == c) s += float(x.value) * survive(g, e, danger);
        else if (others > 0.0f) s -= others * 2.4f * survive(g, e, danger);   // their values are hidden
    }
    return s;
}

} // namespace

Action Game::ai(int level)
{
    Action list[160];
    const int n = actions(list, 160);
    Action pick;
    if (n == 0) return pick;
    const int c = turn;
    if (phase == Phase::Place) {
        // the best explorers on the tiles nearest a safe island, mountains a little safer
        float best = -1e9f;
        int ties = 0;
        for (int i = 0; i < n; ++i) {
            const int h = list[i].to;
            float s = -float(safe_dist(h)) + (terrain[h] == kMountain ? 0.6f : terrain[h] == kForest ? 0.3f : 0.0f);
            if (level == 0) s = float(rand_next() % 100);
            if (s > best + 0.01f) { best = s; pick = list[i]; ties = 1; }
            else if (s > best - 0.01f && rand_next() % uint32_t(++ties) == 0) pick = list[i];
        }
        return pick;
    }
    if (phase == Phase::Sink && level == 0) return list[rand_next() % uint32_t(n)];
    float best = -1e9f;
    int ties = 0;
    for (int i = 0; i < n; ++i) {
        Game g = *this;
        g.apply(list[i]);
        float s;
        if (phase == Phase::Move && level >= 2 && list[i].act != kEndMoves && g.phase == Phase::Move) {
            // two moves ahead: the best follow-up
            Action l2[160];
            const int n2 = g.actions(l2, 160);
            s = -1e9f;
            for (int j = 0; j < n2; ++j) {
                Game h = g;
                h.apply(l2[j]);
                const float v = eval(h, c, level);
                if (v > s) s = v;
            }
        } else {
            s = eval(g, c, level);
        }
        if (list[i].act == kEndMoves) s -= 0.05f;                 // use the moves when they help
        if (s > best + 0.001f) { best = s; pick = list[i]; ties = 1; }
        else if (s > best - 0.001f && rand_next() % uint32_t(++ties) == 0) pick = list[i];
    }
    return pick;
}

// ---- Save ---------------------------------------------------------------------------------------
size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    size_t n = 0;
    memcpy(buf, "ESC1", 4); n = 4;
    for (Terrain t : terrain) buf[n++] = uint8_t(t);
    for (Effect u : under) buf[n++] = uint8_t(u);
    for (const Explorer& x : ex) {
        buf[n++] = x.color; buf[n++] = x.value; buf[n++] = uint8_t(x.hex);
        buf[n++] = uint8_t(x.where); buf[n++] = uint8_t(x.boat);
    }
    for (int8_t b : boat) buf[n++] = uint8_t(b);
    buf[n++] = boats;
    for (const Creature& k : cr) { buf[n++] = uint8_t(k.kind); buf[n++] = uint8_t(k.hex); }
    buf[n++] = creatures;
    buf[n++] = uint8_t(phase);
    buf[n++] = turn;
    buf[n++] = moves_left;
    buf[n++] = die;
    buf[n++] = placed;
    for (int k = 0; k < 8; ++k) buf[n++] = uint8_t(swam >> (8 * k));
    buf[n++] = uint8_t(turns); buf[n++] = uint8_t(turns >> 8);
    for (int k = 0; k < 4; ++k) buf[n++] = uint8_t(rng >> (8 * k));
    buf[n++] = uint8_t(news.color); buf[n++] = uint8_t(news.sunk); buf[n++] = uint8_t(news.effect);
    buf[n++] = uint8_t(news.creature); buf[n++] = news.lost; buf[n++] = news.tipped;
    buf[n++] = news.c_lost; buf[n++] = news.c_tipped;
    return n;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "ESC1", 4) != 0) return false;
    Game g;
    size_t n = 4;
    for (Terrain& t : g.terrain) { if (buf[n] > kSafe) return false; t = Terrain(buf[n++]); }
    for (Effect& u : g.under) { if (buf[n] > kVolcano) return false; u = Effect(buf[n++]); }
    for (Explorer& x : g.ex) {
        x.color = buf[n++]; x.value = buf[n++]; x.hex = int8_t(buf[n++]);
        if (buf[n] > kLost) return false;
        x.where = Where(buf[n++]); x.boat = int8_t(buf[n++]);
        if (x.color >= kColors || x.value < 1 || x.value > 6 || x.hex < -1 || x.hex >= kHexes || x.boat < -1 || x.boat >= kMaxBoats) return false;
        if ((x.where == kLand || x.where == kSwim || x.where == kAboard) && x.hex < 0) return false;
        if (x.where == kAboard && x.boat < 0) return false;
    }
    for (int8_t& b : g.boat) { b = int8_t(buf[n++]); if (b < -1 || b >= kHexes) return false; }
    g.boats = buf[n++];
    if (g.boats > kMaxBoats) return false;
    for (Creature& k : g.cr) {
        if (buf[n] > kSerpent) return false;
        k.kind = Kind(buf[n++]); k.hex = int8_t(buf[n++]);
        if (k.hex < -1 || k.hex >= kHexes) return false;
    }
    g.creatures = buf[n++];
    if (g.creatures > kMaxCreatures) return false;
    if (buf[n] > uint8_t(Phase::Over)) return false;
    g.phase = Phase(buf[n++]);
    g.turn = buf[n++];
    g.moves_left = buf[n++];
    g.die = buf[n++];
    g.placed = buf[n++];
    if (g.turn >= kColors || g.moves_left > kMoves || g.die > kSerpent || g.placed > kExplorers) return false;
    g.swam = 0;
    for (int k = 0; k < 8; ++k) g.swam |= uint64_t(buf[n++]) << (8 * k);
    g.turns = uint16_t(buf[n] | buf[n + 1] << 8); n += 2;
    g.rng = 0;
    for (int k = 0; k < 4; ++k) g.rng |= uint32_t(buf[n++]) << (8 * k);
    if (!g.rng) g.rng = 1;
    g.news.color = int8_t(buf[n++]); g.news.sunk = int8_t(buf[n++]);
    if (buf[n] > kVolcano) return false;
    g.news.effect = Effect(buf[n++]);
    g.news.creature = int8_t(buf[n++]); g.news.lost = buf[n++]; g.news.tipped = buf[n++];
    g.news.c_lost = buf[n++]; g.news.c_tipped = buf[n++];
    if (g.news.sunk < -1 || g.news.sunk >= kHexes || g.news.creature < -1 || g.news.creature >= kMaxCreatures) return false;
    for (const Explorer& x : g.ex)
        if (x.where == kAboard && (x.boat >= g.boats || g.boat[x.boat] != x.hex)) return false;
    *this = g;
    return true;
}

// ---- Stats ----------------------------------------------------------------------------------------
const char* const kCsvHeader = "#,Place,Saved,Level,Seconds,Time";

namespace {
const char* const kLevelNames[3] = {"Easy", "Medium", "Hard"};
}

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    fmt_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%u,%u,%s,%lu,%s\n", unsigned(r.place), unsigned(r.saved),
                           kLevelNames[r.level < 3 ? r.level : 0], (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out)
{
    unsigned long seq = 0, secs = 0;
    unsigned place = 0, saved = 0;
    char lv[12] = {}, tm[16] = {};
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%u,%u,%11[^,],%lu,%15[^,\r\n]", &seq, &place, &saved, lv, &secs, tm) != 6) return false;
    out.place = uint8_t(place);
    out.saved = uint8_t(saved);
    out.level = 0;
    for (int i = 0; i < 3; ++i) if (!strcmp(lv, kLevelNames[i])) out.level = uint8_t(i);
    out.seconds = uint32_t(secs);
    return true;
}

void Summary::add(const Record& r)
{
    ++games;
    wins += r.place == 1;
    if (r.saved > best) best = r.saved;
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % 6;
    if (recent_n < 6) ++recent_n;
}

const Record& Summary::newest(int i) const { return recent[(recent_head - 1 - i + 12) % 6]; }

} // namespace escape
