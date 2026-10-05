// Host tests for src/net/wireless.*: boards finding each other, requests and
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

// ---- A board: presence in 2P, a link while in a session (like the app) ------------------------
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
    Profile  me;
    int      played_here = 0;
    bool     players = true;           // makes moves by itself
    std::mt19937 rng;

    // games: bit k = game key k+1 (version 1)
    void setup(int i, uint16_t name_a, const char* fw, uint16_t mask = 0x7F, uint16_t timer = 30)
    {
        id = i;
        mac.b[0] = 0x24; mac.b[5] = uint8_t(i + 1);
        ctx.node = i;
        a.send = send_cb;
        a.ctx = &ctx;
        rng.seed(100 + i);
        me = Profile{};
        me.name_a = name_a;
        me.name_b = uint16_t(i);
        me.fw = Version::parse(fw);
        me.available = true;
        me.move_timer = timer;
        set_games(mask);
        p.begin(mac, a, air.now);
        profile();
        on = true;
    }
    void set_games(uint16_t mask, int version = 1)
    {
        me.n_games = 0;
        for (int k = 0; k < 16; ++k)
            if ((mask >> k) & 1) { me.games[me.n_games].key = uint8_t(k + 1); me.games[me.n_games].version = uint8_t(version); ++me.n_games; }
    }
    void profile() { me.busy = in_game && !link.ended(); p.set_profile(me, air.now); }
    void start()
    {
        link.begin(mac, p.partner(), p.partner_name_a(), p.partner_name_b(), p.session(), p.inviter(), p.game_key(),
                   p.timer(), a, air.now);
        in_game = true;
        g = Toy{};
        profile();
    }
    const Nearby* seen(const Board& other) const { return p.find(other.mac); }
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
        if (b->link.ended() || b->link.suspended()) continue;
        for (uint32_t m; b->link.next_move(&m);) {
            if (!b->g.legal(int(m)) || b->g.side == b->link.my_side()) { b->link.disagree(air.now); break; }
            b->g.play(int(m));
            b->link.played(m, air.now);
        }
        if (!players || !b->players || b->link.ended() || b->link.voided()) continue;
        uint32_t dummy;
        if (b->g.winner < 0 && b->g.side == b->link.my_side() && b->link.up(air.now)
            && !b->link.next_move(&dummy) && b->rng() % 20 == 0) {
            const int m = 1 + int(b->rng() % 3);
            b->g.play(m);
            b->link.played(uint32_t(m), air.now);
            ++b->played_here;
        }
    }
}

void run(int ms, bool players = true) { for (int t = 0; t < ms; t += 10) step(players); }

// A looks for players until it hears B
const Nearby* wait_seen(Board& A, const Board& B)
{
    A.p.search(true, air.now);
    for (int t = 0; t < 1000 && !A.seen(B); ++t) step(false);
    return A.seen(B);
}

Presence::Event wait_event(Board& A, int max_ms)
{
    Presence::Event e = Presence::Event::None;
    for (int t = 0; t < max_ms && e == Presence::Event::None; t += 10) { step(false); e = A.p.poll(); }
    return e;
}

// Two boards in a session together (A asked B for game `key`)
void pair_up(Board& A, Board& B, int key = 2)
{
    const Nearby* nb = wait_seen(A, B);
    CHECK(nb != nullptr);
    if (nb) CHECK(A.p.playable(*nb, key));
    A.p.request(B.mac, B.me.name_a, B.me.name_b, key, 0xABC00000u + A.id, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    CHECK(B.p.asked());
    CHECK(B.p.asked_key() == key);
    B.p.accept(air.now);
    CHECK(B.p.poll() == Presence::Event::Started);
    B.start();
    CHECK(wait_event(A, 5000) == Presence::Event::Started);
    CHECK(A.p.game_key() == key && B.p.game_key() == key);
    A.p.search(false, air.now);
    A.start();
    CHECK(A.link.session() == B.link.session());
    // The asked player moves first
    CHECK(A.link.my_side() == 1 && B.link.my_side() == 0);
    CHECK(A.link.timer() == B.link.timer());
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
void test_versions()
{
    const Version a = Version::parse("v0.21.0"), b = Version::parse("0.20.3"), c = Version::parse("dev-abc");
    CHECK(a.major == 0 && a.minor == 21 && a.patch == 0);
    CHECK(a.cmp(b) > 0 && b.cmp(a) < 0 && a.cmp(a) == 0);
    CHECK(c.major == 0 && c.minor == 0 && c.patch == 0);
}

// Quiet unless someone looks: a board in 2P sends nothing on its own
void test_quiet_listening()
{
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1.0.0");
    B.setup(1, 2, "v1.0.0");
    run(5000, false);
    CHECK(air.flying.empty());
    CHECK(A.p.count() == 0 && B.p.count() == 0);
}

void test_finding()
{
    reset_air(0.2, 0.1, 80);
    Board A, B, C, D, E;
    boards = {&A, &B, &C, &D, &E};
    A.setup(0, 10, "v1.0.0");
    B.setup(1, 11, "v1.0.0", 0x0005);        // games 1 and 3
    C.setup(2, 12, "v1.0.0");
    C.me.available = false; C.profile();     // 1P: doesn't answer
    D.setup(3, 13, "v0.9.9");
    D.set_games(0x7F, 2); D.profile();       // other game versions
    E.setup(4, 14, "v1.0.0");
    E.in_game = true; E.profile();           // busy in a game
    A.p.search(true, air.now);
    run(2500, false);
    CHECK(A.p.count() == 3);
    CHECK(!A.seen(C));
    const Nearby* b = A.seen(B);
    const Nearby* d = A.seen(D);
    const Nearby* e = A.seen(E);
    CHECK(b && d && e);
    if (b) {
        CHECK(b->name_a == 11 && b->name_b == 1);
        CHECK(b->n_games == 2 && b->link == kLink);
        CHECK(A.p.playable(*b, 1) && A.p.playable(*b, 3) && !A.p.playable(*b, 2));
    }
    if (d) { CHECK(d->fw.cmp(A.me.fw) < 0); CHECK(!A.p.playable(*d, 1) && d->version_of(1) == 2); }
    if (e) { CHECK(e->busy); CHECK(!A.p.playable(*e, 1)); }
    // Stop looking: the list empties as boards are forgotten, and the air goes quiet
    A.p.search(false, air.now);
    run(kForgetMs + 1500, false);
    CHECK(A.p.count() == 0);
    air.flying.clear();
    run(3000, false);
    CHECK(air.flying.empty());
    // A board of an older link version (a legacy beacon) is listed without a name
    A.p.search(true, air.now);
    uint8_t pkt[48] = {'C', 'Y', 2, 1, 'Z', 'o', 'e'};
    Mac z; z.b[5] = 99;
    A.p.receive(z, pkt, sizeof pkt, air.now);
    const Nearby* zn = A.p.find(z);
    CHECK(zn && zn->link == 2 && zn->name_a == 0xFFFF);
    // A Hello from a later link version (with more fields at the end) is still read
    uint8_t later[64] = {'C', 'Y', uint8_t(kLink + 1), 0x11, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 1, 2, 3,
                         7, 0, 9, 0, 1, 0, 0, 1, 5, 4};
    Mac y; y.b[5] = 98;
    A.p.receive(y, later, sizeof later, air.now);
    const Nearby* yn = A.p.find(y);
    CHECK(yn && yn->link == kLink + 1 && yn->name_a == 7 && yn->name_b == 9 && yn->fw.minor == 2
          && yn->n_games == 1 && yn->games[0].key == 5 && yn->games[0].version == 4);
    if (yn) CHECK(!A.p.playable(*yn, 5));
}

void test_answers()
{
    reset_air(0.3, 0.1, 100);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1", 0x0003);
    CHECK(wait_seen(A, B));
    // No Thanks
    A.p.request(B.mac, 2, 1, 2, 77, air.now);
    CHECK(A.p.requesting() && A.p.request_key() == 2);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    CHECK(B.p.asked() && B.p.asker_name_a() == 1 && B.p.asked_key() == 2 && B.p.asker_timer() == 30);
    run(1500, false);
    CHECK(A.p.ringing());
    B.p.decline(Answer::NoThanks, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Answered);
    CHECK(A.p.answer() == Answer::NoThanks);
    CHECK(!A.p.requesting());
    run(4500, false);
    // Other Game: B gets A's games, then asks for one itself
    A.p.request(B.mac, 2, 1, 1, 78, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    GameOffer g[kMaxGames];
    CHECK(B.p.asker_games(g, kMaxGames) == 7);
    B.p.decline(Answer::OtherGame, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Answered);
    CHECK(A.p.answer() == Answer::OtherGame);
    B.p.request(A.mac, 1, 0, 2, 79, air.now);
    for (int t = 0; t < 300 && !A.p.asked(); ++t) step(false);
    CHECK(A.p.asked() && A.p.asked_key() == 2);
    A.p.accept(air.now);
    CHECK(A.p.poll() == Presence::Event::Started && !A.p.inviter());
    CHECK(wait_event(B, 4000) == Presence::Event::Started);
    CHECK(B.p.inviter());
    run(5000, false);
    // A game B turned off since A saw its list: B's board answers by itself
    B.set_games(0x0001); B.profile();
    A.p.request(B.mac, 2, 1, 2, 80, air.now);
    CHECK(wait_event(A, 4000) == Presence::Event::Answered);
    CHECK(A.p.answer() == Answer::GameOff);
    CHECK(!B.p.asked());
    run(5000, false);
    // A request nobody answers runs out; the question goes away there too
    Board C;
    boards = {&A, &B, &C};
    C.setup(2, 3, "v1");
    CHECK(wait_seen(A, C));
    A.p.request(C.mac, 3, 2, 2, 81, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    CHECK(C.p.asked());
    CHECK(wait_event(A, kRequestMs + 2000) == Presence::Event::NoAnswer);
    CHECK(wait_event(C, 4000) == Presence::Event::Cancelled);
    CHECK(!C.p.asked());
    run(3000, false);
    // A cancelled request: "Ann cancelled her request."
    A.p.request(C.mac, 3, 2, 2, 82, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    A.p.cancel(air.now);
    CHECK(wait_event(C, 3000) == Presence::Event::Cancelled);
    CHECK(!C.p.asked());
    run(3000, false);
    // The asked board goes out of range while the question is up
    A.p.request(C.mac, 3, 2, 2, 83, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    run(1500, false);
    C.on = false;
    CHECK(wait_event(A, kForgetMs + 3000) == Presence::Event::Gone);
    C.on = true;
    // ... and the asker goes out of range: the asked board drops the question
    run(3000, false);
    A.p.request(C.mac, 3, 2, 2, 84, air.now);
    for (int t = 0; t < 300 && !C.p.asked(); ++t) step(false);
    A.on = false;
    CHECK(wait_event(C, kForgetMs + 3000) == Presence::Event::AskerGone);
    A.on = true;
}

// The first asker has priority; a board in a game or 1P is Busy
void test_priority_and_busy()
{
    reset_air(0, 0, 0);
    Board A, B, C;
    boards = {&A, &B, &C};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
    C.setup(2, 3, "v1");
    A.p.request(B.mac, 2, 1, 2, 90, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    C.p.request(B.mac, 2, 1, 2, 91, air.now);
    CHECK(wait_event(C, 3000) == Presence::Event::Answered);
    CHECK(C.p.answer() == Answer::Busy);
    CHECK(B.p.asked() && B.p.asker() == A.mac);
    B.p.accept(air.now);
    B.p.poll();
    B.start();
    CHECK(wait_event(A, 3000) == Presence::Event::Started);
    A.start();
    run(1000, false);
    // In a game: Busy, and listed busy
    C.p.request(B.mac, 2, 1, 2, 92, air.now);
    CHECK(wait_event(C, 3000) == Presence::Event::Answered);
    CHECK(C.p.answer() == Answer::Busy);
    C.p.search(true, air.now);
    run(1500, false);
    CHECK(C.seen(B) && C.seen(B)->busy);
    // A board asked while it asks someone else: Busy
    Board D;
    boards = {&A, &B, &C, &D};
    D.setup(3, 4, "v1");
    C.p.request(D.mac, 4, 3, 2, 93, air.now);
    run(500, false);
    Board E;
    boards = {&A, &B, &C, &D, &E};
    E.setup(4, 5, "v1");
    E.p.request(C.mac, 3, 2, 2, 94, air.now);
    CHECK(wait_event(E, 3000) == Presence::Event::Answered);
    CHECK(E.p.answer() == Answer::Busy);
}

// Both boards ask each other at once: the lower address keeps asking, the other shows the question
void test_crossed_requests()
{
    reset_air(0.2, 0.1, 50);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
    A.p.request(B.mac, 2, 1, 5, 95, air.now);
    B.p.request(A.mac, 1, 0, 3, 96, air.now);
    for (int t = 0; t < 300 && !B.p.asked(); ++t) step(false);
    CHECK(B.p.asked() && B.p.asked_key() == 5);       // B (higher address) gave way
    CHECK(!B.p.requesting() && A.p.requesting());
    CHECK(!A.p.asked());
    B.p.accept(air.now);
    CHECK(B.p.poll() == Presence::Event::Started);
    CHECK(wait_event(A, 3000) == Presence::Event::Started);
}

// The move timer: the shorter applies, Off = no limit
void test_timer()
{
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1", 0x7F, 120);
    B.setup(1, 2, "v1", 0x7F, 30);
    pair_up(A, B);
    CHECK(A.link.timer() == 30 && B.link.timer() == 30);
    reset_air(0, 0, 0);
    Board C, D;
    boards = {&C, &D};
    C.setup(0, 1, "v1", 0x7F, 0);
    D.setup(1, 2, "v1", 0x7F, 300);
    pair_up(C, D);
    CHECK(C.link.timer() == 300 && D.link.timer() == 300);
    reset_air(0, 0, 0);
    Board E, F;
    boards = {&E, &F};
    E.setup(0, 1, "v1", 0x7F, 0);
    F.setup(1, 2, "v1", 0x7F, 0);
    pair_up(E, F);
    CHECK(E.link.timer() == 0 && F.link.timer() == 0);
}

void test_game_lossy(double loss, double dup, uint32_t delay, int seed)
{
    reset_air(loss, dup, delay);
    air.rng.seed(seed);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
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
    CHECK(A.link.my_side() == 0 && B.link.my_side() == 1);
    CHECK(finished_together(A, B, 120000));
    // Goodbye: A is done, B hears it
    A.link.done(air.now);
    run(3000);
    CHECK(A.link.end_reason() == Link::End::YouDone);
    CHECK(B.link.end_reason() == Link::End::PeerDone);
}

void test_link_down()
{
    reset_air(0.2, 0.05, 60);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
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
    CHECK(!A.link.meet() && !B.link.meet());   // a short drop: just carries on
    CHECK(finished_together(A, B, 120000));
}

// Put away and met again: a restart (the saved link loads suspended), and
// a board that gave up waiting. Continue on both = it goes on; Close = not counted.
void test_suspend_and_meet()
{
    for (double loss : {0.0, 0.3}) {
        reset_air(loss, 0.05, 60);
        Board A, B;
        boards = {&A, &B};
        A.setup(0, 1, "v1");
        B.setup(1, 2, "v1");
        pair_up(A, B);
        run(3000);
        run(1500, false);
        // B restarts: its saved session comes back put away
        uint8_t saved[Link::kSaveBytes];
        CHECK(B.link.save(saved, sizeof saved) == Link::kSaveBytes);
        const Toy b_game = B.g;
        B.on = false;
        run(kLostMs + 1000, false);
        CHECK(!A.link.up(air.now));
        Link fresh;
        CHECK(fresh.load(saved, sizeof saved, B.a, air.now));
        CHECK(fresh.suspended());
        B.link = fresh;
        B.g = b_game;
        B.on = true;
        for (int t = 0; t < 8000 && !(A.link.meet() && B.link.meet()); t += 10) step(false);
        CHECK(A.link.meet() && B.link.meet());
        A.link.agree_continue(air.now);
        run(2000, false);
        CHECK(A.link.suspended() == false && !A.link.resumed());   // waits for B's answer
        B.link.agree_continue(air.now);
        for (int t = 0; t < 5000 && !(A.link.resumed() | !A.link.continue_said()); t += 10) step(false);
        run(2000, false);
        CHECK(!B.link.suspended() && !B.link.meet() && !A.link.meet());
        CHECK(finished_together(A, B, 120000));
    }
    // Gave up waiting (put away by hand), then one player closes when they meet: not counted on both
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
    pair_up(A, B);
    run(2000);
    air.blocked = true;
    run(5000);
    A.link.suspend(air.now);
    B.link.suspend(air.now);
    run(5000);
    air.blocked = false;
    for (int t = 0; t < 8000 && !(A.link.meet() && B.link.meet()); t += 10) step(false);
    CHECK(A.link.meet() && B.link.meet());
    B.link.close(air.now);
    run(3000, false);
    CHECK(B.link.end_reason() == Link::End::Closed);
    CHECK(A.link.end_reason() == Link::End::PeerClosed);
}

void test_forfeit()
{
    for (double loss : {0.0, 0.4}) {
        reset_air(loss, 0.1, 80);
        Board A, B;
        boards = {&A, &B};
        A.setup(0, 1, "v1");
        B.setup(1, 2, "v1");
        pair_up(A, B);
        run(3000);
        A.link.forfeit(air.now, loss > 0);
        CHECK(A.link.end_reason() == Link::End::YouForfeited);
        for (int t = 0; t < 6000 && !A.link.linger_over(air.now); t += 10) step();
        CHECK(A.link.linger_over(air.now));
        CHECK(B.link.end_reason() == Link::End::PeerForfeited);
        CHECK(B.link.by_time() == (loss > 0));
    }
    // B was out of range while A forfeited: B hears it when it comes back
    // (A's ended link answers B's statuses - the app keeps it as a tombstone)
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1");
    B.setup(1, 2, "v1");
    pair_up(A, B);
    run(2000);
    B.on = false;
    A.link.forfeit(air.now);
    run(10000);
    B.on = true;
    run(2000);
    CHECK(B.link.end_reason() == Link::End::PeerForfeited);
    // send_end: the tombstone's answer on its own
    uint8_t buf[kPacketMax];
    size_t n = 0;
    Air cap;
    struct Cap { uint8_t* b; size_t* n; } c{buf, &n};
    cap.ctx = &c;
    cap.send = [](const uint8_t* d, size_t len, void* ctx) { Cap* k = static_cast<Cap*>(ctx); memcpy(k->b, d, len); *k->n = len; };
    send_end(cap, B.mac, 1234, kFlagForfeit);
    CHECK(packet_kind(buf, n) != 0 && status_session(buf, n) == 1234);
}

// Clear 2P Sessions with nobody in range: leave() ends it here, the other board hears "gone"
void test_leave()
{
    for (double loss : {0.0, 0.4}) {
        reset_air(loss, 0.1, 80);
        Board A, B;
        boards = {&A, &B};
        A.setup(0, 1, "v1");
        B.setup(1, 2, "v1");
        pair_up(A, B);
        run(3000);
        A.link.leave(air.now);
        CHECK(A.link.end_reason() == Link::End::YouLeft);
        for (int t = 0; t < 6000 && !A.link.linger_over(air.now); t += 10) step();
        CHECK(B.link.end_reason() == Link::End::PeerGone);
    }
}

// Boards whose games differ: that game is void on both (not counted), the session goes on
void test_disagree()
{
    reset_air(0, 0, 0);
    Board E, F;
    boards = {&E, &F};
    E.setup(0, 1, "v1");
    F.setup(1, 2, "v1");
    pair_up(E, F);
    for (int t = 0; t < 20000 && E.g.moves < 3; t += 10) step();
    run(1000, false);                      // in step, nobody moving
    Board& mover = E.g.side == E.link.my_side() ? E : F;
    Board& other = &mover == &E ? F : E;
    mover.g.play(1);
    mover.link.played(1, air.now);
    other.link.played(2, air.now);
    run(1500, false);
    CHECK(E.link.voided() && F.link.voided());
    CHECK(!E.link.ended() && !F.link.ended());
    uint32_t m;
    CHECK(!E.link.next_move(&m) && !F.link.next_move(&m));
    // Play Again starts a fresh game on both
    E.link.want_again(air.now);
    F.link.want_again(air.now);
    run(2000, false);
    CHECK(E.link.game_no() == 1 && F.link.game_no() == 1 && !E.link.voided() && !F.link.voided());
    CHECK(finished_together(E, F, 120000));
}

void test_save_round_trip()
{
    reset_air(0, 0, 0);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, 1, "v1", 0x7F, 60);
    B.setup(1, 2, "v1", 0x7F, 60);
    pair_up(A, B, 5);
    run(5000);
    uint8_t buf[Link::kSaveBytes];
    CHECK(A.link.save(buf, sizeof buf) == Link::kSaveBytes);
    Link l;
    CHECK(l.load(buf, sizeof buf, A.a, air.now));
    CHECK(l.session() == A.link.session() && l.ply() == A.link.ply() && l.my_side() == A.link.my_side());
    CHECK(l.peer_name_a() == 2 && l.peer_name_b() == 1 && l.game_key() == 5 && l.timer() == 60);
    CHECK(l.suspended());
    A.link.forfeit(air.now, true);
    CHECK(A.link.save(buf, sizeof buf) == Link::kSaveBytes);
    CHECK(l.load(buf, sizeof buf, A.a, air.now) && l.end_reason() == Link::End::YouForfeited && l.by_time());
    CHECK(!l.suspended());
    buf[0] = 'X';
    CHECK(!l.load(buf, sizeof buf, A.a, air.now));
    CHECK(!l.load(buf, sizeof buf - 1, A.a, air.now));
}

} // namespace

int main()
{
    test_versions();
    test_quiet_listening();
    test_finding();
    test_answers();
    test_priority_and_busy();
    test_crossed_requests();
    test_timer();
    test_game_lossy(0, 0, 0, 1);
    test_game_lossy(0.3, 0.1, 150, 2);
    test_game_lossy(0.5, 0.2, 400, 3);
    test_link_down();
    test_suspend_and_meet();
    test_forfeit();
    test_leave();
    test_disagree();
    test_save_round_trip();
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("net: all passed\n");
    return 0;
}
