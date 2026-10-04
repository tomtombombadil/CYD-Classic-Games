// Host tests for src/net/wireless.*: boards finding each other, offers and
// their answers, and games kept in step over a fake radio that loses,
// repeats and reorders packets.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include "../../src/net/wireless.h"

using namespace net;

namespace {

int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

// ---- The air --------------------------------------------------------------------------------
struct Packet {
    int      from;
    uint32_t at;
    std::vector<uint8_t> data;
};

struct AirSim {
    std::vector<Packet> flying;
    std::mt19937 rng{7};
    double loss = 0, dup = 0;
    uint32_t max_delay = 0;
    bool blocked = false;
    uint32_t now = 0;
};
AirSim air;

struct Ctx { int node; };

void send_cb(const uint8_t* d, size_t n, void* ctx)
{
    assert(n <= kPacketMax);
    if (air.blocked) return;
    std::uniform_real_distribution<double> u(0, 1);
    const int copies = u(air.rng) < air.dup ? 2 : 1;
    for (int c = 0; c < copies; ++c) {
        if (u(air.rng) < air.loss) continue;
        const uint32_t delay = air.max_delay ? air.rng() % (air.max_delay + 1) : 0;
        air.flying.push_back(Packet{static_cast<Ctx*>(ctx)->node, air.now + delay, std::vector<uint8_t>(d, d + n)});
    }
}

// ---- A toy game with extra turns (like Mancala) -----------------------------------------------
// Moves 1..3 add to a total; 3 = go again; first to reach 40 wins.
struct Toy {
    int total = 0, side = 0, moves = 0, winner = -1;
    bool legal(int m) const { return winner < 0 && m >= 1 && m <= 3; }
    void play(int m)
    {
        total += m;
        ++moves;
        if (total >= 40) { winner = side; return; }
        if (m != 3) side ^= 1;
    }
};

// ---- A board: presence always on, a link while in a game (like the app) -----------------------
struct Board {
    int      id = 0;
    Mac      mac;
    Ctx      ctx;
    Air      a;
    Presence p;
    bool     on = false;               // radio on
    bool     in_game = false;
    Link     link;
    Toy      g;
    char     name[16] = "";
    uint16_t games = 0xFFFF;
    bool     available = true;
    int      played_here = 0;
    bool     players = true;           // makes moves by itself
    std::mt19937 rng;

    void setup(int i, const char* nm, const char* fw, uint16_t mask = 0xFFFF)
    {
        id = i;
        mac.b[0] = 0x24; mac.b[5] = uint8_t(i + 1);
        ctx.node = i;
        a.send = send_cb;
        a.ctx = &ctx;
        rng.seed(100 + i);
        snprintf(name, sizeof name, "%s", nm);
        games = mask;
        p.begin(mac, fw, a, air.now);
        profile();
        on = true;
    }
    void profile() { p.set_profile(name, available, games, in_game && !link.ended() ? 0 : -1, false, air.now); }
    void start()
    {
        link.begin(mac, p.partner(), p.partner_name(), p.session(), p.inviter(), a, air.now);
        in_game = true;
        g = Toy{};
        profile();
    }
    int index_of(const Board& other) const
    {
        for (int i = 0; i < p.count(); ++i) if (p.at(i).mac == other.mac) return i;
        return -1;
    }
};

std::vector<Board*> boards;

void deliver()
{
    std::vector<Packet> due, later;
    for (auto& pk : air.flying) (int32_t(air.now - pk.at) >= 0 ? due : later).push_back(pk);
    air.flying = later;
    for (auto& pk : due)
        for (Board* b : boards) {
            if (b->id == pk.from || !b->on) continue;
            const Mac& from = boards[pk.from]->mac;
            const uint32_t s = status_session(pk.data.data(), pk.data.size());
            if (b->in_game && s && s == b->link.session())
                b->link.receive(from, pk.data.data(), pk.data.size(), air.now);
            else
                b->p.receive(from, pk.data.data(), pk.data.size(), air.now);
        }
}

// One 10 ms step: packets, ticks, and the players (who move when they may)
void step(bool players = true)
{
    air.now += 10;
    deliver();
    for (Board* b : boards) {
        if (!b->on) continue;
        b->p.tick(air.now);
        if (!b->in_game) continue;
        b->link.tick(air.now);
        if (b->link.started_next()) b->g = Toy{};
        if (b->link.ended()) continue;
        for (int m; (m = b->link.next_move()) >= 0;) {
            if (!b->g.legal(m) || b->g.side == b->link.my_side()) { b->link.disagree(air.now); break; }
            b->g.play(m);
            b->link.played(m, air.now);
        }
        if (!players || !b->players || b->link.ended()) continue;
        if (b->g.winner < 0 && b->g.side == b->link.my_side() && b->link.up(air.now) && !b->link.peer_away()
            && b->link.next_move() < 0 && b->rng() % 20 == 0) {
            const int m = 1 + int(b->rng() % 3);
            b->g.play(m);
            b->link.played(m, air.now);
            ++b->played_here;
        }
    }
}

void run(int ms, bool players = true) { for (int t = 0; t < ms; t += 10) step(players); }

int wait_seen(Board& A, const Board& B)
{
    int i = -1;
    for (int t = 0; t < 1000 && i < 0; ++t) { step(false); i = A.index_of(B); }
    return i;
}

Presence::Event wait_event(Board& A, int max_ms)
{
    Presence::Event e = Presence::Event::None;
    for (int t = 0; t < max_ms && e == Presence::Event::None; t += 10) { step(false); e = A.p.poll(); }
    return e;
}

// Two boards in a game together (A offered `game` to B)
void pair_up(Board& A, Board& B, int game = 2)
{
    const int bi = wait_seen(A, B);
    CHECK(bi >= 0);
    CHECK(A.p.can_offer(bi, game));
    A.p.offer(bi, game, 0xABC00000u + A.id, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    CHECK(B.p.asked());
    CHECK(B.p.asked_game() == game);
    B.p.accept(air.now);
    CHECK(B.p.poll() == Presence::Event::Started);
    B.start();
    CHECK(wait_event(A, 5000) == Presence::Event::Started);
    CHECK(A.p.game() == game && B.p.game() == game);
    A.start();
    CHECK(A.link.session() == B.link.session());
    CHECK(A.link.my_side() == 0 && B.link.my_side() == 1);
}

bool finished_together(Board& A, Board& B, int max_ms)
{
    for (int t = 0; t < max_ms; t += 10) {
        step();
        if (A.g.winner >= 0 && B.g.winner >= 0 && A.g.moves == B.g.moves) break;
    }
    return A.g.winner >= 0 && A.g.winner == B.g.winner && A.g.total == B.g.total && A.g.moves == B.g.moves;
}

void reset_air(double loss, double dup, uint32_t delay)
{
    air = AirSim{};
    air.loss = loss; air.dup = dup; air.max_delay = delay;
}

// ---- Tests ------------------------------------------------------------------------------------
void test_names()
{
    char out[32];
    Mac m; m.b[4] = 0x3F; m.b[5] = 0x2a;
    default_name(m, out, sizeof out);
    CHECK(strcmp(out, "CYD-3F2A") == 0);
    clean_name("   A very long player name  ", out, sizeof out);
    CHECK(strlen(out) <= kNameMax && strncmp(out, "A very long", 11) == 0);
    clean_name(" Tom ", out, sizeof out);
    CHECK(strcmp(out, "Tom") == 0);
}

void test_presence_list()
{
    reset_air(0.2, 0.1, 80);
    Board A, B, C, D, E;
    boards = {&A, &B, &C, &D, &E};
    A.setup(0, "Ann", "v1.0.0");
    B.setup(1, "Bob", "v1.0.0", 0x0005);     // games 0 and 2
    C.setup(2, "Cy", "v1.0.0");
    C.available = false; C.profile();        // hidden: not listed
    D.setup(3, "Di", "v0.9.9");              // another version
    E.setup(4, "Ed", "v1.0.0");
    E.in_game = true; E.profile();           // busy in a game
    run(2500, false);
    CHECK(A.p.count() == 3);
    CHECK(A.index_of(C) < 0);
    const int b = A.index_of(B), d = A.index_of(D), e = A.index_of(E);
    CHECK(b >= 0 && d >= 0 && e >= 0);
    if (b >= 0) {
        CHECK(strcmp(A.p.at(b).name, "Bob") == 0);
        CHECK(A.p.at(b).games == 0x0005);
        CHECK(A.p.can_offer(b, 0) && A.p.can_offer(b, 2) && !A.p.can_offer(b, 1));
    }
    if (d >= 0) { CHECK(!A.p.same_version(d)); CHECK(!A.p.can_offer(d, 0)); }
    if (e >= 0) { CHECK(A.p.at(e).busy && A.p.at(e).busy_game == 0); CHECK(!A.p.can_offer(e, 0)); }
    // B goes hidden: off the list at once (its next beacon says so)
    B.available = false; B.profile();
    run(1500, false);
    CHECK(A.index_of(B) < 0);
    // D switches off: forgotten after a while
    D.on = false;
    run(kForgetMs + 1500, false);
    CHECK(A.index_of(D) < 0);
    // Another protocol version shows by name only
    uint8_t pkt[48] = {'C', 'Y', uint8_t(kProto + 1), 1, 'Z', 'o', 'e'};
    Mac z; z.b[5] = 99;
    A.p.receive(z, pkt, sizeof pkt, air.now);
    const Nearby* zn = A.p.find(z);
    CHECK(zn && strcmp(zn->name, "Zoe") == 0 && !zn->proto_ok);
}

void test_answers()
{
    reset_air(0.3, 0.1, 100);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1", 0x0003);
    const int bi = wait_seen(A, B);
    // Not Now
    A.p.offer(bi, 1, 77, air.now);
    CHECK(A.p.offering() && A.p.offer_game() == 1);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    CHECK(B.p.asked() && strcmp(B.p.asker_name(), "Ann") == 0 && B.p.asked_game() == 1);
    B.p.decline(Reason::NotNow, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Declined);
    CHECK(A.p.reason() == Reason::NotNow);
    CHECK(!A.p.offering());
    run(3500, false);
    // Another Game
    A.p.offer(A.index_of(B), 0, 78, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    B.p.decline(Reason::OtherGame, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Declined);
    CHECK(A.p.reason() == Reason::OtherGame);
    run(3500, false);
    // A game B turned off since A saw its list: B's board answers by itself
    air.blocked = true;
    B.games = 0x0001; B.profile();                 // that beacon is lost
    air.blocked = false;
    CHECK(A.p.can_offer(A.index_of(B), 1));        // A doesn't know yet
    A.p.offer(A.index_of(B), 1, 79, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Declined);
    CHECK(A.p.reason() == Reason::GameOff);
    CHECK(!B.p.asked());
    CHECK(!A.p.can_offer(A.index_of(B), 1));
    // An offer nobody answers runs out
    Board C;
    boards = {&A, &B, &C};
    C.setup(2, "Cy", "v1");
    const int ci = wait_seen(A, C);
    A.p.offer(ci, 2, 80, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    CHECK(C.p.asked());
    CHECK(wait_event(A, kOfferMs + 2000) == Presence::Event::NoAnswer);
    run(3000, false);
    CHECK(!C.p.asked());                           // the question went away there too
    // Cancelled offers go away on the other board
    A.p.offer(A.index_of(C), 2, 81, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    A.p.cancel(air.now);
    run(2000, false);
    CHECK(!C.p.asked());
    // Play after the asker gave up: the game there ends at once ("gone")
    A.p.offer(A.index_of(C), 2, 82, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    CHECK(C.p.asked());
    A.p.cancel(air.now);
    C.p.accept(air.now);
    CHECK(C.p.poll() == Presence::Event::Started);
    C.start();
    for (int t = 0; t < 10000 && !C.link.ended(); t += 10) step(false);
    CHECK(C.link.ended() && C.link.end_reason() == Link::End::PeerGone);
}

void test_busy_refusal()
{
    reset_air(0, 0, 0);
    Board A, B, C;
    boards = {&A, &B, &C};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    C.setup(2, "Cy", "v1");
    pair_up(A, B);
    run(1500, false);
    const int bi = C.index_of(B);
    CHECK(bi >= 0 && C.p.at(bi).busy && !C.p.can_offer(bi, 2));
    // An offer that crosses with B starting a game is refused with Busy
    // (hand-made: B's own presence answers it)
    Board D;
    boards = {&A, &B, &C, &D};
    D.setup(3, "Di", "v1");
    const int di = wait_seen(D, C);
    D.p.offer(di, 2, 90, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    C.in_game = true; C.profile();                 // C got busy before answering
    CHECK(!C.p.asked());
    CHECK(wait_event(D, 4000) == Presence::Event::Declined);
    CHECK(D.p.reason() == Reason::Busy);
}

void test_game_lossy(double loss, double dup, uint32_t delay, int seed)
{
    reset_air(loss, dup, delay);
    air.rng.seed(seed);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    pair_up(A, B);
    CHECK(finished_together(A, B, 120000));
    CHECK(!A.link.ended() && !B.link.ended());
    CHECK(A.played_here > 0 && B.played_here > 0);
    // Play again: both ask, the next game starts on both, sides swapped
    A.link.want_again(air.now);
    for (int t = 0; t < 1000 && !B.link.peer_again(); ++t) step();
    CHECK(B.link.peer_again());
    CHECK(A.link.game_no() == 0);          // not before both asked
    B.link.want_again(air.now);
    run(2000);
    CHECK(A.link.game_no() == 1 && B.link.game_no() == 1);
    CHECK(A.link.my_side() == 1 && B.link.my_side() == 0);
    CHECK(finished_together(A, B, 120000));
    // No more: A is done, B hears it
    A.link.done(air.now);
    run(3000);
    CHECK(A.link.end_reason() == Link::End::YouDone);
    CHECK(B.link.end_reason() == Link::End::PeerDone);
}

void test_link_down_and_reboot()
{
    reset_air(0.2, 0.05, 60);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    pair_up(A, B);
    run(4000);
    // The link drops: no moves are made while it's down
    air.blocked = true;
    run(3500);
    CHECK(!A.link.up(air.now) && !B.link.up(air.now));
    const int a_moves = A.g.moves, b_moves = B.g.moves;
    run(3000);
    CHECK(A.g.moves == a_moves && B.g.moves == b_moves);
    air.blocked = false;
    run(2000);
    run(2000, false);                      // nobody moving: both in step
    CHECK(A.link.up(air.now) && B.link.up(air.now));
    CHECK(A.g.moves == B.g.moves);

    // B's game screen closes (paused): A sees it and waits
    B.link.set_away(true, air.now);
    run(1500);
    CHECK(A.link.peer_away());
    const int moves_then = A.g.moves;
    run(3000);
    CHECK(A.g.moves == moves_then);
    B.link.set_away(false, air.now);
    run(1500);
    CHECK(!A.link.peer_away());

    // B "turns off" right after A moved (B saved before hearing it), then comes back
    for (int t = 0; t < 20000 && !(A.g.side == A.link.my_side() && A.g.winner < 0); t += 10) step();
    uint8_t saved[Link::kSaveBytes];
    CHECK(B.link.save(saved, sizeof saved) == Link::kSaveBytes);
    const Toy b_game = B.g;
    B.on = false;
    run(1500);                             // A moves (maybe twice); B hears nothing
    Link fresh;
    CHECK(fresh.load(saved, sizeof saved, B.a, air.now));
    B.link = fresh;
    B.g = b_game;
    B.on = true;
    CHECK(finished_together(A, B, 120000));
    CHECK(!A.link.ended() && !B.link.ended());
}

void test_forfeit()
{
    for (double loss : {0.0, 0.4}) {
        reset_air(loss, 0.1, 80);
        Board A, B;
        boards = {&A, &B};
        A.setup(0, "Ann", "v1");
        B.setup(1, "Bob", "v1");
        pair_up(A, B);
        run(3000);
        A.link.forfeit(air.now);
        CHECK(A.link.end_reason() == Link::End::YouForfeited);
        // A keeps saying so for a while, then may switch off
        for (int t = 0; t < 6000 && !A.link.linger_over(air.now); t += 10) step();
        CHECK(A.link.linger_over(air.now));
        CHECK(B.link.end_reason() == Link::End::PeerForfeited);
    }
    // B was away while A forfeited: B hears it when it comes back (A's board still has the radio on)
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    pair_up(A, B);
    run(2000);
    B.on = false;
    A.link.forfeit(air.now);
    run(10000);
    B.on = true;
    run(2000);
    CHECK(B.link.end_reason() == Link::End::PeerForfeited);
}

void test_disagree()
{
    reset_air(0, 0, 0);
    Board E, F;
    boards = {&E, &F};
    E.setup(0, "Ed", "v1");
    F.setup(1, "Flo", "v1");
    pair_up(E, F);
    for (int t = 0; t < 20000 && E.g.moves < 3; t += 10) step();
    run(1000, false);                      // in step, nobody moving
    // Both boards play a different move at the same point
    Board& mover = E.g.side == E.link.my_side() ? E : F;
    Board& other = &mover == &E ? F : E;
    mover.g.play(1);
    mover.link.played(1, air.now);
    other.link.played(2, air.now);
    run(1500, false);
    CHECK(E.link.ended() && F.link.ended());
    CHECK(E.link.end_reason() == Link::End::OutOfStep || E.link.end_reason() == Link::End::PeerGone);
    CHECK(F.link.end_reason() == Link::End::OutOfStep || F.link.end_reason() == Link::End::PeerGone);
}

void test_save_round_trip()
{
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    pair_up(A, B);
    run(5000);
    uint8_t buf[Link::kSaveBytes];
    CHECK(A.link.save(buf, sizeof buf) == Link::kSaveBytes);
    Link l;
    CHECK(l.load(buf, sizeof buf, A.a, air.now));
    CHECK(l.session() == A.link.session() && l.ply() == A.link.ply() && l.my_side() == A.link.my_side());
    CHECK(strcmp(l.peer_name(), "Bob") == 0);
    A.link.forfeit(air.now);
    CHECK(A.link.save(buf, sizeof buf) == Link::kSaveBytes);
    CHECK(l.load(buf, sizeof buf, A.a, air.now) && l.end_reason() == Link::End::YouForfeited);
    buf[0] = 'X';
    CHECK(!l.load(buf, sizeof buf, A.a, air.now));
    CHECK(!l.load(buf, sizeof buf - 1, A.a, air.now));
}

} // namespace

int main()
{
    test_names();
    test_presence_list();
    test_answers();
    test_busy_refusal();
    test_game_lossy(0, 0, 0, 1);
    test_game_lossy(0.3, 0.1, 150, 2);
    test_game_lossy(0.5, 0.2, 400, 3);
    test_link_down_and_reboot();
    test_forfeit();
    test_disagree();
    test_save_round_trip();
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("net: all passed\n");
    return 0;
}
