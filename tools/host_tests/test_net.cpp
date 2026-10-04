// Host tests for src/net/wireless.*: boards finding each other, invites,
// and games kept in step over a fake radio that loses, repeats and
// reorders packets.
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

// ---- A board ----------------------------------------------------------------------------------
struct Board {
    int    id = 0;
    Mac    mac;
    Ctx    ctx;
    Air    a;
    Lobby  lobby;
    bool   in_lobby = false, in_game = false;
    Link   link;
    Toy    g;
    int    played_here = 0;            // own moves
    std::mt19937 rng;

    void setup(int i, const char* name, const char* fw)
    {
        id = i;
        mac.b[0] = 0x24; mac.b[5] = uint8_t(i + 1);
        ctx.node = i;
        a.send = send_cb;
        a.ctx = &ctx;
        rng.seed(100 + i);
        lobby.begin(mac, name, "toy", fw, a, air.now);
        in_lobby = true;
    }
    void start_from_lobby()
    {
        link.begin(mac, lobby.partner(), lobby.partner_name(), lobby.session(), lobby.inviter(), a, air.now);
        in_lobby = false;
        in_game = true;
        g = Toy{};
    }
};

std::vector<Board*> boards;

void deliver()
{
    std::vector<Packet> due, later;
    for (auto& p : air.flying) (int32_t(air.now - p.at) >= 0 ? due : later).push_back(p);
    air.flying = later;
    for (auto& p : due)
        for (Board* b : boards) {
            if (b->id == p.from) continue;
            if (b->in_lobby) b->lobby.receive(boards[p.from]->mac, p.data.data(), p.data.size(), air.now);
            if (b->in_game)  b->link.receive(boards[p.from]->mac, p.data.data(), p.data.size(), air.now);
        }
}

// One 10 ms step: packets, ticks, and the players (who move when they may)
void step(bool players = true)
{
    air.now += 10;
    deliver();
    for (Board* b : boards) {
        if (b->in_lobby) b->lobby.tick(air.now);
        if (!b->in_game) continue;
        b->link.tick(air.now);
        if (b->link.started_next()) b->g = Toy{};
        if (b->link.ended()) continue;
        // The partner's moves first, each checked
        for (int m; (m = b->link.next_move()) >= 0;) {
            if (!b->g.legal(m) || b->g.side == b->link.my_side()) { b->link.disagree(air.now); break; }
            b->g.play(m);
            b->link.played(m, air.now);
        }
        if (!players || b->link.ended()) continue;
        if (b->g.winner < 0 && b->g.side == b->link.my_side() && b->link.up(air.now)
            && b->link.next_move() < 0 && b->rng() % 20 == 0) {
            const int m = 1 + int(b->rng() % 3);
            b->g.play(m);
            b->link.played(m, air.now);
            ++b->played_here;
        }
    }
}

void run(int ms, bool players = true) { for (int t = 0; t < ms; t += 10) step(players); }

// Two boards in a game together (A invited B)
void pair_up(Board& A, Board& B)
{
    int bi = -1;
    for (int t = 0; t < 1000 && bi < 0; ++t) {
        step(false);
        for (int i = 0; i < A.lobby.count(); ++i) if (A.lobby.at(i).mac == B.mac) bi = i;
    }
    CHECK(bi >= 0);
    CHECK(A.lobby.can_invite(bi));
    A.lobby.invite(bi, 0xABC00000u + A.id, air.now);
    for (int t = 0; t < 300 && !B.lobby.asked(); ++t) step(false);
    CHECK(B.lobby.asked());
    B.lobby.accept(air.now);
    CHECK(B.lobby.poll() == Lobby::Event::Started);
    B.start_from_lobby();
    Lobby::Event e = Lobby::Event::None;
    for (int t = 0; t < 500 && e == Lobby::Event::None; ++t) { step(false); e = A.lobby.poll(); }
    CHECK(e == Lobby::Event::Started);
    A.start_from_lobby();
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

void test_lobby_list()
{
    reset_air(0.2, 0.1, 80);
    Board A, B, C;
    boards = {&A, &B, &C};
    A.setup(0, "Ann", "v1.0.0");
    B.setup(1, "Bob", "v1.0.0");
    C.setup(2, "Cy", "v0.9.9");
    run(2000, false);
    CHECK(A.lobby.count() == 2);
    for (int i = 0; i < A.lobby.count(); ++i) {
        const Nearby& n = A.lobby.at(i);
        if (n.mac == B.mac) { CHECK(strcmp(n.name, "Bob") == 0); CHECK(A.lobby.can_invite(i)); }
        if (n.mac == C.mac) { CHECK(A.lobby.same_game(i)); CHECK(!A.lobby.same_version(i)); CHECK(!A.lobby.can_invite(i)); }
    }
    // C leaves: forgotten after a while
    C.in_lobby = false;
    run(5000, false);
    CHECK(A.lobby.count() == 1);
    // Another protocol version shows by name only
    uint8_t pkt[40] = {'C', 'Y', uint8_t(kProto + 1), 1, 'Z', 'o', 'e'};
    Mac z; z.b[5] = 99;
    A.lobby.receive(z, pkt, sizeof pkt, air.now);
    bool seen = false;
    for (int i = 0; i < A.lobby.count(); ++i)
        if (A.lobby.at(i).mac == z) { seen = true; CHECK(strcmp(A.lobby.at(i).name, "Zoe") == 0); CHECK(!A.lobby.can_invite(i)); }
    CHECK(seen);
}

void test_decline_and_cancel()
{
    reset_air(0.3, 0.1, 100);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    run(1500, false);
    A.lobby.invite(0, 77, air.now);
    for (int t = 0; t < 300 && !B.lobby.asked(); ++t) step(false);
    CHECK(B.lobby.asked());
    CHECK(strcmp(B.lobby.asker_name(), "Ann") == 0);
    B.lobby.decline(air.now);
    Lobby::Event e = Lobby::Event::None;
    for (int t = 0; t < 400 && e == Lobby::Event::None; ++t) { step(false); e = A.lobby.poll(); }
    CHECK(e == Lobby::Event::Declined);
    CHECK(!A.lobby.inviting());
    // A cancels an invite: B's question goes away
    run(3500, false);
    A.lobby.invite(0, 78, air.now);
    for (int t = 0; t < 300 && !B.lobby.asked(); ++t) step(false);
    CHECK(B.lobby.asked());
    A.lobby.cancel(air.now);
    run(2000, false);
    CHECK(!B.lobby.asked());
    // B says Play after A gave up: B's game ends at once ("Ann left")
    A.lobby.invite(0, 79, air.now);
    for (int t = 0; t < 300 && !B.lobby.asked(); ++t) step(false);
    A.lobby.cancel(air.now);
    B.lobby.accept(air.now);
    B.start_from_lobby();
    run(2000, false);
    CHECK(B.link.ended());
    CHECK(B.link.end_reason() == Link::End::PeerLeft);
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
    CHECK(A.link.up(air.now) && B.link.up(air.now));
    CHECK(A.g.moves == B.g.moves);

    // B "turns off" right after A moved (B saved before hearing it), then comes back
    for (int t = 0; t < 20000 && !(A.g.side == A.link.my_side() && A.g.winner < 0); t += 10) step();
    uint8_t saved[Link::kSaveBytes];
    CHECK(B.link.save(saved, sizeof saved) == Link::kSaveBytes);
    const Toy b_game = B.g;
    B.in_game = false;
    run(1500);                             // A moves (maybe twice); B hears nothing
    Link fresh;
    CHECK(fresh.load(saved, sizeof saved, B.a, air.now));
    B.link = fresh;
    B.g = b_game;
    B.in_game = true;
    CHECK(finished_together(A, B, 120000));
    CHECK(!A.link.ended() && !B.link.ended());
}

void test_leave_and_disagree()
{
    reset_air(0.3, 0.1, 50);
    Board A, B;
    boards = {&A, &B};
    A.setup(0, "Ann", "v1");
    B.setup(1, "Bob", "v1");
    pair_up(A, B);
    run(3000);
    A.link.leave(air.now);
    A.in_game = false;                     // radio off after the "left" packets
    run(3000);
    // With 30 % loss, three "left" packets may all be lost: then B waits
    CHECK(B.link.ended() || !B.link.up(air.now));

    // A board that opens the old game later is told it's over
    reset_air(0, 0, 0);
    Board C, D;
    boards = {&C, &D};
    C.setup(0, "Cy", "v1");
    D.setup(1, "Di", "v1");
    pair_up(C, D);
    run(2000);
    C.link.leave(air.now);
    C.in_game = false;
    run(500);
    CHECK(D.link.ended() && D.link.end_reason() == Link::End::PeerLeft);

    // Different games on the two boards: found out by the hash
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
    CHECK(E.link.ended() || F.link.ended());
    CHECK((E.link.ended() ? E : F).link.end_reason() == Link::End::OutOfStep);
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
    buf[0] = 'X';
    CHECK(!l.load(buf, sizeof buf, A.a, air.now));
    CHECK(!l.load(buf, sizeof buf - 1, A.a, air.now));
}

} // namespace

int main()
{
    test_names();
    test_lobby_list();
    test_decline_and_cancel();
    test_game_lossy(0, 0, 0, 1);
    test_game_lossy(0.3, 0.1, 150, 2);
    test_game_lossy(0.5, 0.2, 400, 3);
    test_link_down_and_reboot();
    test_leave_and_disagree();
    test_save_round_trip();
    if (failures) { printf("%d failed\n", failures); return 1; }
    printf("net: all passed\n");
    return 0;
}
