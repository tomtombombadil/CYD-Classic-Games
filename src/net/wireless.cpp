#include "wireless.h"

#include <cstdio>
#include <cstring>

namespace net {

namespace {

// Every packet: 'C' 'Y' <link version> <kind>, then the body.
//
// FROZEN FOREVER (any link version reads them; later versions may only add
// fields at the end):
//   Call  (0x10): fw major, minor, patch; u16 counter
//   Hello (0x11): to[6] (all 0xFF: anyone looking); fw major, minor, patch;
//                 name u16 + u16; state (1 available, 2 busy); u16 counter;
//                 game count, then key + version per game
// This link version only:
//   Request (0x12): to[6]; session u32; game key, version; move timer u16;
//                   name u16 + u16; u16 counter; game count + key/version each
//   Answer  (0x13): to[6]; session u32; answer; move timer u16; u16 counter
//   Status  (0x14): to[6]; session u32; game no u16; ply u16; flags u16;
//                   hash u32; u16 counter; move timer u16 (the asked board's
//                   word counts); move count, then u32 moves
// Link versions before 3 sent "beacons" (kind 1) with a typed name: such a
// board is listed as an older board, its name never shown.
constexpr uint8_t kMagic0 = 'C', kMagic1 = 'Y';
enum Kind : uint8_t { kLegacy = 1, kCall = 0x10, kHello = 0x11, kRequest = 0x12, kAnswer = 0x13, kStatus = 0x14 };
enum State : uint8_t { kAvailable = 1, kBusy = 2 };

constexpr size_t kHeader = 4;
constexpr size_t kCallBytes = kHeader + 3 + 2;
constexpr size_t kHelloFixed = kHeader + 6 + 3 + 2 + 2 + 1 + 2 + 1;
constexpr size_t kRequestFixed = kHeader + 6 + 4 + 1 + 1 + 2 + 2 + 2 + 2 + 1;
constexpr size_t kAnswerBytes = kHeader + 6 + 4 + 1 + 2 + 2;
constexpr size_t kStatusFixed = kHeader + 6 + 4 + 2 + 2 + 2 + 4 + 2 + 2 + 1;
const Mac kEveryone = {{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}};

// Packets carry a counter so a late copy can't undo a newer one. A big
// jump back means the sender started again: taken.
bool stale(uint16_t seq, uint16_t last, bool have) { const int16_t d = int16_t(seq - last); return have && d <= 0 && d > -64; }
uint16_t seq_start(uint32_t now) { return uint16_t((now * 2654435761u) >> 16); }

uint16_t agree_timer(uint16_t a, uint16_t b)
{
    // The shorter applies; Off (0) = no limit
    if (!a) return b;
    if (!b) return a;
    return a < b ? a : b;
}

struct Writer {
    uint8_t* p;
    size_t   n = 0, cap;
    Writer(uint8_t* b, size_t c) : p(b), cap(c) {}
    void u8(uint8_t v)   { if (n < cap) p[n] = v; ++n; }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void bytes(const uint8_t* b, size_t len) { for (size_t k = 0; k < len; ++k) u8(b[k]); }
    void mac(const Mac& m) { bytes(m.b, 6); }
    void fw(const Version& v) { u8(v.major); u8(v.minor); u8(v.patch); }
    bool ok() const { return n <= cap; }
};

struct Reader {
    const uint8_t* p;
    size_t n = 0, len;
    Reader(const uint8_t* b, size_t l, size_t start = 0) : p(b), n(start), len(l) {}
    bool     more(size_t k) const { return n + k <= len; }
    uint8_t  u8()  { return n < len ? p[n++] : (n++, 0); }
    uint16_t u16() { const uint16_t a = u8(); return uint16_t(a | (u8() << 8)); }
    uint32_t u32() { const uint32_t a = u16(); return a | (uint32_t(u16()) << 16); }
    void bytes(uint8_t* b, size_t k) { for (size_t i = 0; i < k; ++i) b[i] = u8(); }
    void mac(Mac& m) { bytes(m.b, 6); }
    void fw(Version& v) { v.major = u8(); v.minor = u8(); v.patch = u8(); }
    int  games(GameOffer* out, int cap)
    {
        const int n = u8();
        int k = 0;
        for (int i = 0; i < n && more(2); ++i) {
            GameOffer g;
            g.key = u8();
            g.version = u8();
            if (k < cap) out[k++] = g;
        }
        return k;
    }
    bool ok() const { return n <= len; }
};

void header(Writer& w, uint8_t kind)
{
    w.u8(kMagic0);
    w.u8(kMagic1);
    w.u8(kLink);
    w.u8(kind);
}

void write_games(Writer& w, const GameOffer* g, int n)
{
    w.u8(uint8_t(n));
    for (int k = 0; k < n; ++k) { w.u8(g[k].key); w.u8(g[k].version); }
}

// The parts of a status every receiver looks at first
struct StatusHead {
    Mac      to;
    uint32_t session = 0;
    uint16_t game = 0, ply = 0, flags = 0;
    uint32_t hash = 0;
    uint16_t seq = 0, timer = 0;
    uint8_t  n = 0;
    uint32_t moves[kRecent] = {};
};

bool read_status(const uint8_t* d, size_t len, StatusHead& s)
{
    if (len < kStatusFixed) return false;
    Reader r(d, len, kHeader);
    r.mac(s.to);
    s.session = r.u32();
    s.game = r.u16();
    s.ply = r.u16();
    s.flags = r.u16();
    s.hash = r.u32();
    s.seq = r.u16();
    s.timer = r.u16();
    s.n = r.u8();
    if (s.n > kRecent || !r.more(4u * s.n)) return false;
    for (int k = 0; k < s.n; ++k) s.moves[k] = r.u32();
    return r.ok();
}

void write_status(Writer& w, const Mac& to, uint32_t session, uint16_t game, uint16_t ply, uint16_t flags,
                  uint32_t hash, uint16_t seq, uint16_t timer)
{
    header(w, kStatus);
    w.mac(to);
    w.u32(session);
    w.u16(game);
    w.u16(ply);
    w.u16(flags);
    w.u32(hash);
    w.u16(seq);
    w.u16(timer);
}

} // namespace

bool Mac::operator==(const Mac& o) const { return memcmp(b, o.b, 6) == 0; }
bool Mac::operator<(const Mac& o) const { return memcmp(b, o.b, 6) < 0; }
bool Mac::zero() const { static const uint8_t z[6] = {}; return memcmp(b, z, 6) == 0; }

int Version::cmp(const Version& o) const
{
    if (major != o.major) return major < o.major ? -1 : 1;
    if (minor != o.minor) return minor < o.minor ? -1 : 1;
    if (patch != o.patch) return patch < o.patch ? -1 : 1;
    return 0;
}

Version Version::parse(const char* s)
{
    Version v;
    if (!s) return v;
    if (*s == 'v' || *s == 'V') ++s;
    unsigned a = 0, b = 0, c = 0;
    if (sscanf(s, "%u.%u.%u", &a, &b, &c) == 3 && a < 256 && b < 256 && c < 256) {
        v.major = uint8_t(a);
        v.minor = uint8_t(b);
        v.patch = uint8_t(c);
    }
    return v;
}

int Nearby::version_of(int key) const
{
    for (int k = 0; k < n_games; ++k) if (games[k].key == key) return games[k].version;
    return -1;
}

int Profile::version_of(int key) const
{
    for (int k = 0; k < n_games; ++k) if (games[k].key == key) return games[k].version;
    return -1;
}

uint32_t hash_move(uint32_t h, uint32_t move)
{
    for (int b = 0; b < 4; ++b) h = (h ^ ((move >> (8 * b)) & 0xFF)) * 16777619u;
    return h;
}

int packet_kind(const uint8_t* d, size_t n, int* link)
{
    if (link) *link = 0;
    if (!d || n < kHeader || d[0] != kMagic0 || d[1] != kMagic1) return 0;
    if (link) *link = d[2];
    const uint8_t k = d[3];
    // The Call and the Hello read the same in every link version
    if (k == kCall && n >= kCallBytes) return kCall;
    if (k == kHello && n >= kHelloFixed) return kHello;
    if (d[2] < 3 && k == kLegacy) return kLegacy;
    if (d[2] != kLink) return 0;
    if (k == kRequest && n >= kRequestFixed) return kRequest;
    if (k == kAnswer && n >= kAnswerBytes) return kAnswer;
    if (k == kStatus && n >= kStatusFixed) return kStatus;
    return 0;
}

uint32_t status_session(const uint8_t* d, size_t n)
{
    if (packet_kind(d, n) != kStatus) return 0;
    Reader r(d, n, kHeader + 6);
    return r.u32();
}

void send_end(const Air& air, const Mac& to, uint32_t session, uint16_t flags)
{
    if (!air.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    write_status(w, to, session, 0, 0, flags, 0, 0, 0);
    w.u8(0);
    air.send(buf, w.n, air.ctx);
}

// ---- Presence -------------------------------------------------------------------------------
void Presence::begin(const Mac& me, const Air& air, uint32_t now)
{
    *this = Presence{};
    me_ = me;
    air_ = air;
    seq_ = seq_start(now ^ (uint32_t(me.b[5]) << 8));
}

void Presence::set_profile(const Profile& p, uint32_t now)
{
    me_p_ = p;
    if (me_p_.n_games > kMaxGames) me_p_.n_games = kMaxGames;
    if (p.busy || !p.available) {
        // In a game, or 1P: no requests either way
        if (req_on_) cancel(now);
        if (in_on_) {
            in_on_ = false;
            ans_to_ = in_from_;
            ans_session_ = in_session_;
            ans_ = Answer::Busy;
            ans_until_ms_ = now + kLingerMs;
            send_answer(ans_to_, ans_session_, ans_, now);
        }
    }
}

bool Presence::playable(const Nearby& b, int key) const
{
    const int mine = me_p_.version_of(key);
    return b.link == kLink && b.available && !b.busy && mine >= 0 && b.version_of(key) == mine;
}

const Nearby* Presence::find(const Mac& m) const
{
    for (int i = 0; i < n_; ++i) if (near_[i].mac == m) return &near_[i];
    return nullptr;
}

Nearby* Presence::lookup(const Mac& m)
{
    for (int i = 0; i < n_; ++i) if (near_[i].mac == m) return &near_[i];
    return nullptr;
}

void Presence::send_call(uint32_t now)
{
    call_ms_ = now + kWakeSendMs;            // boards nearby may be dozing
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kCall);
    w.fw(me_p_.fw);
    w.u16(seq_++);
    air_.send(buf, w.n, air_.ctx);
}

void Presence::send_hello(const Mac& to)
{
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kHello);
    w.mac(to);
    w.fw(me_p_.fw);
    w.u16(me_p_.name_a);
    w.u16(me_p_.name_b);
    w.u8(uint8_t((me_p_.available ? kAvailable : 0) | (me_p_.busy ? kBusy : 0)));
    w.u16(seq_++);
    write_games(w, me_p_.games, me_p_.n_games);
    air_.send(buf, w.n, air_.ctx);
}

void Presence::send_request(uint32_t now)
{
    req_ms_ = now + (req_ringing_ ? kSendMs : kWakeSendMs);   // until it rings there: it may be dozing
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kRequest);
    w.mac(req_to_);
    w.u32(req_session_);
    w.u8(uint8_t(req_key_));
    w.u8(uint8_t(me_p_.version_of(req_key_)));
    w.u16(me_p_.move_timer);
    w.u16(me_p_.name_a);
    w.u16(me_p_.name_b);
    w.u16(seq_++);
    write_games(w, me_p_.games, me_p_.n_games);
    air_.send(buf, w.n, air_.ctx);
}

void Presence::send_answer(const Mac& to, uint32_t session, Answer a, uint32_t now)
{
    if (a == ans_ && to == ans_to_ && session == ans_session_) ans_ms_ = now + kSendMs;
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kAnswer);
    w.mac(to);
    w.u32(session);
    w.u8(uint8_t(a));
    w.u16(me_p_.move_timer);
    w.u16(seq_++);
    air_.send(buf, w.n, air_.ctx);
}

void Presence::search(bool on, uint32_t now)
{
    if (on == searching_) return;
    searching_ = on;
    if (on) send_call(now);
}

void Presence::tick(uint32_t now)
{
    for (int i = 0; i < n_;) {
        if (now - near_[i].seen_ms > kForgetMs) near_[i] = near_[--n_];
        else ++i;
    }
    if (searching_ && int32_t(now - call_ms_) >= 0) send_call(now);
    if (hello_due_ && int32_t(now - hello_ms_) >= 0) {
        hello_due_ = false;
        if (me_p_.available) send_hello(hello_to_);
    }
    // Asking: repeat until answered; nobody rings = no answer; a board that
    // rang and went quiet (or dropped off the list) = out of range
    if (req_on_) {
        if (req_ringing_ && now - req_heard_ms_ > kForgetMs) {
            req_on_ = false;
            event_ = Event::Gone;
        } else if (!req_ringing_ && searching_ && !find(req_to_) && now - req_since_ > kForgetMs) {
            req_on_ = false;
            event_ = Event::Gone;
        } else if (now - req_since_ > kRequestMs) {
            cancel(now);
            event_ = Event::NoAnswer;
        } else if (int32_t(now - req_ms_) >= 0) {
            send_request(now);
        }
    }
    // Being asked: say it rings there; the asker gone quiet = it went away
    if (in_on_) {
        if (now - in_heard_ms_ > kForgetMs) {
            in_on_ = false;
            event_ = Event::AskerGone;
        } else if (int32_t(now - in_ms_) >= 0) {
            in_ms_ = now + kSendMs;
            send_answer(in_from_, in_session_, Answer::Ringing, now);
        }
    }
    if (ans_ != Answer::None && int32_t(ans_until_ms_ - now) > 0 && int32_t(now - ans_ms_) >= 0)
        send_answer(ans_to_, ans_session_, ans_, now);
}

void Presence::receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    if (from == me_) return;
    int link = 0;
    const int kind = packet_kind(d, n, &link);
    if (kind == kCall) {
        // Someone looks for players: answer soon (a little later on boards
        // with a higher address, so a room full of boards doesn't answer at once)
        heard_call_ = true;
        call_heard_ms_ = now;
        if (me_p_.available && !hello_due_) {
            hello_due_ = true;
            hello_to_ = kEveryone;
            hello_ms_ = now + 20 + (me_.b[5] % 8) * 20;
        }
        return;
    }
    if (kind == kHello || kind == kLegacy) {
        if (!searching_) return;
        Nearby nb;
        nb.mac = from;
        nb.link = link;
        nb.seen_ms = now;
        if (kind == kLegacy) {
            nb.name_a = nb.name_b = 0xFFFF;          // its name is typed text: never shown
            nb.available = true;
        } else {
            Reader r(d, n, kHeader);
            Mac to;
            r.mac(to);
            r.fw(nb.fw);
            nb.name_a = r.u16();
            nb.name_b = r.u16();
            const uint8_t st = r.u8();
            nb.seq = r.u16();
            nb.n_games = r.games(nb.games, kMaxGames);
            if (!r.ok()) return;
            Nearby* known = lookup(from);
            if (known && known->link == link && stale(nb.seq, known->seq, true)) return;
            nb.available = st & kAvailable;
            nb.busy = st & kBusy;
        }
        Nearby* b = lookup(from);
        if (!b && n_ < kMaxNearby) b = &near_[n_++];
        if (b) *b = nb;
        return;
    }
    if (kind == kRequest) { hear_request(from, d, n, now); return; }
    if (kind == kAnswer) { hear_answer(from, d, n, now); return; }
    if (kind == kStatus) {
        StatusHead s;
        if (!read_status(d, n, s) || s.to != me_) return;
        if (req_on_ && from == req_to_ && s.session == req_session_ && !(s.flags & kFlagEnds)) {
            // The asked board said Play and started (its answer got lost): so do we
            // (its move timer comes with its statuses: the Link takes it up)
            started(from, req_name_a_, req_name_b_, req_session_, req_key_, true, me_p_.move_timer);
            req_on_ = false;
        } else if (from == partner_ && s.session == session_) {
            // the session agreed a moment ago: the app takes it up on its next tick
        } else if (!(s.flags & kFlagEnds)) {
            send_end(air_, from, s.session, kFlagGone);   // a session this board doesn't play
        }
    }
}

void Presence::hear_request(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    Reader r(d, n, kHeader);
    Mac to;
    r.mac(to);
    if (to != me_) return;
    const uint32_t session = r.u32();
    const int key = r.u8();
    const int version = r.u8();
    const uint16_t timer = r.u16();
    const uint16_t a = r.u16(), b = r.u16();
    r.u16();                                             // counter (requests are idempotent)
    GameOffer games[kMaxGames];
    const int ng = r.games(games, kMaxGames);
    if (!r.ok()) return;
    if (in_on_ && in_from_ == from && in_session_ == session) { in_heard_ms_ = now; return; }
    if (session == session_ && from == partner_ && !inviter_) return;     // said Play already (repeating)
    if (ans_to_ == from && ans_session_ == session && int32_t(ans_until_ms_ - now) > 0) return;   // answered
    Answer no = Answer::None;
    if (!me_p_.available || me_p_.busy || in_on_) {
        no = Answer::Busy;                               // the first asker has priority
    } else if (req_on_) {
        if (req_to_ != from) {
            no = Answer::Busy;
        } else if (me_ < from) {
            return;                                      // both asked at once: the lower address keeps asking
        } else {
            req_on_ = false;                             // ... and the other gives way: take theirs
        }
    }
    if (no == Answer::None && (me_p_.version_of(key) < 0 || me_p_.version_of(key) != version)) no = Answer::GameOff;
    if (no != Answer::None) {
        ans_to_ = from;
        ans_session_ = session;
        ans_ = no;
        ans_until_ms_ = now + kLingerMs;
        send_answer(from, session, no, now);
        return;
    }
    in_on_ = true;
    in_from_ = from;
    in_session_ = session;
    in_key_ = key;
    in_timer_ = timer;
    in_name_a_ = a;
    in_name_b_ = b;
    in_n_games_ = ng;
    for (int k = 0; k < ng; ++k) in_games_[k] = games[k];
    in_heard_ms_ = now;
    in_ms_ = now + kSendMs;
    send_answer(from, session, Answer::Ringing, now);
}

void Presence::hear_answer(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    Reader r(d, n, kHeader);
    Mac to;
    r.mac(to);
    if (to != me_) return;
    const uint32_t session = r.u32();
    const uint8_t a = r.u8();
    const uint16_t timer = r.u16();
    if (!r.ok() || a > uint8_t(Answer::Ringing)) return;
    const Answer ans = Answer(a);
    if (ans == Answer::Cancel) {
        if (in_on_ && in_from_ == from && in_session_ == session) {
            in_on_ = false;
            event_ = Event::Cancelled;
        }
        return;
    }
    if (!req_on_ || from != req_to_ || session != req_session_) return;
    if (ans == Answer::Ringing) {
        req_ringing_ = true;
        req_heard_ms_ = now;
        return;
    }
    req_on_ = false;
    if (ans == Answer::Play) {
        started(from, req_name_a_, req_name_b_, session, req_key_, true, timer);
        return;
    }
    answer_ = ans;
    event_ = Event::Answered;
}

void Presence::request(const Mac& to, uint16_t name_a, uint16_t name_b, int key, uint32_t session, uint32_t now)
{
    if (me_p_.busy || in_on_) return;
    req_on_ = true;
    req_ringing_ = false;
    req_to_ = to;
    req_name_a_ = name_a;
    req_name_b_ = name_b;
    req_key_ = key;
    req_session_ = session;
    req_since_ = now;
    send_request(now);
}

void Presence::cancel(uint32_t now)
{
    if (!req_on_) return;
    req_on_ = false;
    ans_to_ = req_to_;
    ans_session_ = req_session_;
    ans_ = Answer::Cancel;
    ans_until_ms_ = now + kLingerMs / 2;
    send_answer(ans_to_, ans_session_, ans_, now);
}

int Presence::asker_games(GameOffer* out, int cap) const
{
    int k = 0;
    for (; k < in_n_games_ && k < cap; ++k) out[k] = in_games_[k];
    return k;
}

void Presence::started(const Mac& who, uint16_t a, uint16_t b, uint32_t session, int key, bool inviter,
                       uint16_t their_timer)
{
    event_ = Event::Started;
    partner_ = who;
    partner_a_ = a;
    partner_b_ = b;
    session_ = session;
    game_key_ = key;
    inviter_ = inviter;
    partner_timer_ = their_timer;
    timer_ = agree_timer(me_p_.move_timer, their_timer);
}

void Presence::accept(uint32_t now)
{
    if (!in_on_) return;
    in_on_ = false;
    if (req_on_) cancel(now);                            // a game now: any request of ours is off
    started(in_from_, in_name_a_, in_name_b_, in_session_, in_key_, false, in_timer_);
    ans_to_ = in_from_;
    ans_session_ = in_session_;
    ans_ = Answer::Play;
    ans_until_ms_ = now + kLingerMs;
    send_answer(ans_to_, ans_session_, ans_, now);
}

void Presence::decline(Answer why, uint32_t now)
{
    if (!in_on_) return;
    in_on_ = false;
    ans_to_ = in_from_;
    ans_session_ = in_session_;
    ans_ = why;
    ans_until_ms_ = now + kLingerMs;
    send_answer(ans_to_, ans_session_, ans_, now);
}

Presence::Event Presence::poll()
{
    const Event e = event_;
    event_ = Event::None;
    return e;
}

// ---- Link -----------------------------------------------------------------------------------
void Link::begin(const Mac& me, const Mac& peer, uint16_t peer_a, uint16_t peer_b, uint32_t session,
                 bool inviter, int game_key, uint16_t timer, const Air& air, uint32_t now)
{
    *this = Link{};
    me_ = me;
    peer_ = peer;
    peer_a_ = peer_a;
    peer_b_ = peer_b;
    session_ = session;
    inviter_ = inviter;
    game_key_ = game_key;
    timer_ = timer;
    air_ = air;
    hash_ = kHashStart;
    peer_hash_check_ = kHashStart;
    seq_ = seq_start(now ^ session);
    send_status(now);
}

int Link::my_side() const
{
    // The asked player moves first in the first game; then it alternates
    const int inviter_side = (game_no_ + 1) & 1;
    return inviter_ ? inviter_side : 1 - inviter_side;
}

uint16_t Link::end_flags() const
{
    switch (ended_) {
        case End::YouForfeited: return uint16_t(kFlagForfeit | (by_time_ ? kFlagByTime : 0));
        case End::YouDone:      return kFlagDone;
        case End::Closed:       return kFlagClosed;
        default:                return kFlagGone;
    }
}

void Link::send_status(uint32_t now, uint16_t extra)
{
    // Link down: the partner may be dozing (its session put away), so
    // often. Put away and not meeting yet: bursts (tick()). Else twice a second.
    next_ms_ = now + (heard_ && up(now) ? kSendMs : kWakeSendMs);
    if (!air_.send) return;
    uint16_t flags = extra;
    if (ended_ != End::None) {
        flags |= end_flags();
    } else {
        if (again_) flags |= kFlagAgain;
        if (void_) flags |= kFlagVoid;
        if (suspended_) flags |= kFlagSuspended;
        if (cont_ || int32_t(cont_until_ms_ - now) > 0) flags |= kFlagContinue;
    }
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    write_status(w, peer_, session_, game_no_, ply_, flags, hash_, seq_++, timer_);
    const int n = ply_ < kRecent ? ply_ : kRecent;
    w.u8(uint8_t(n));
    for (int k = 0; k < n; ++k) w.u32(recent_[k]);
    air_.send(buf, w.n, air_.ctx);
}

void Link::tick(uint32_t now)
{
    // An ending this board made is repeated for a while, so the other board hears it
    if (ended_ != End::None && linger_over(now)) return;
    if (ended_ != End::None && ended_ != End::YouForfeited && ended_ != End::YouDone && ended_ != End::YouLeft
        && ended_ != End::Closed) return;
    if (ended_ == End::None && suspended_ && !meet_ && !cont_) {
        // Put away: call the partner in bursts (it may be dozing too)
        if (int32_t(now - next_burst_ms_) >= 0) {
            burst_until_ms_ = now + kBurstMs;
            next_burst_ms_ = now + kBurstEveryMs;
        }
        if (int32_t(burst_until_ms_ - now) <= 0) return;
    }
    if (int32_t(now - next_ms_) >= 0) send_status(now);
}

bool Link::linger_over(uint32_t now) const
{
    return ended_ != End::None && now - ended_ms_ >= kLingerMs;
}

uint32_t Link::hash_at(int p, bool* ok) const
{
    *ok = true;
    if (p == ply_) return hash_;
    if (p == 0) return kHashStart;
    const int k = ply_ - p;
    if (p < 0 || k < 0 || k >= kRecent || k >= ply_) { *ok = false; return 0; }
    return hashes_[k];
}

void Link::stop(End why, uint32_t now)
{
    if (ended_ != End::None) return;
    ended_ = why;
    ended_ms_ = now;
    meet_ = false;
}

void Link::next_game()
{
    ++game_no_;
    ply_ = 0;
    hash_ = kHashStart;
    again_ = false;
    void_ = false;
    peer_again_ = false;
    peer_game_ = game_no_;
    peer_ply_ = 0;
    peer_n_ = 0;
    peer_hash_check_ = kHashStart;
    started_next_ = true;
}

void Link::resume(uint32_t now)
{
    suspended_ = false;
    meet_ = false;
    cont_ = false;
    cont_until_ms_ = now + kLingerMs;
    resumed_ = true;
    send_status(now);
}

void Link::receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    if (packet_kind(d, n) != kStatus) return;
    StatusHead s;
    if (!read_status(d, n, s) || s.to != me_) return;
    if (from != peer_ || s.session != session_) {
        if (!(s.flags & kFlagEnds)) send_end(air_, from, s.session, kFlagGone);   // not our session
        return;
    }
    heard_ = true;
    heard_ms_ = now;
    if (ended_ != End::None) {
        // Still being asked about a session that ended here: say so again
        if (!(s.flags & kFlagEnds)) send_end(air_, from, s.session, end_flags());
        return;
    }
    if (s.flags & kFlagForfeit) { by_time_ = (s.flags & kFlagByTime) != 0; stop(End::PeerForfeited, now); return; }
    if (s.flags & kFlagDone)    { stop(End::PeerDone, now); return; }
    if (s.flags & kFlagClosed)  { stop(End::PeerClosed, now); return; }
    if (s.flags & kFlagGone)    { stop(End::PeerGone, now); return; }
    if (stale(s.seq, peer_seq_, have_seq_)) return;      // a late copy of an older status
    peer_seq_ = s.seq;
    have_seq_ = true;
    if (inviter_) timer_ = s.timer;                     // the asked board agreed it: its word counts
    // Meeting again after one of the boards put the session away
    peer_suspended_ = (s.flags & kFlagSuspended) != 0;
    peer_cont_ = (s.flags & kFlagContinue) != 0;
    if (suspended_ || peer_suspended_) {
        const bool was = meet_;
        if (cont_ && peer_cont_) resume(now);
        else meet_ = !cont_;                             // ask this board's player (once they answer, wait)
        if (meet_ && !was) send_status(now);             // tell the other board at once: it may be in a burst
    } else if (cont_) {
        resume(now);                                     // the other board said yes and went on already
    }
    if (s.game < game_no_) return;                       // it will catch up with ours
    if (s.game == game_no_ + 1) {
        if (!again_) return;                             // (can't be: it waits for our Play Again)
        next_game();
        send_status(now);
    } else if (s.game > game_no_ + 1) {
        return;
    }
    if (s.flags & kFlagVoid) void_ = true;
    peer_again_ = (s.flags & kFlagAgain) != 0;
    peer_game_ = s.game;
    peer_ply_ = s.ply;
    peer_n_ = s.n;
    for (int k = 0; k < s.n; ++k) peer_recent_[k] = s.moves[k];
    if (!void_) {
        if (s.ply <= ply_) {
            bool ok;
            const uint32_t h = hash_at(s.ply, &ok);
            // Too far behind to catch up, or a different game: they disagree
            if (!ok || h != s.hash) { disagree(now); return; }
        } else if (s.ply - ply_ > s.n) {
            disagree(now);                               // moves we can't get back
            return;
        }                                                // else next_move() hands them over
    }
    peer_hash_check_ = s.hash;
    if (again_ && peer_again_) {
        next_game();
        send_status(now);
    }
}

bool Link::next_move(uint32_t* move) const
{
    if (ended_ != End::None || void_ || peer_game_ != game_no_ || peer_ply_ <= ply_) return false;
    const int j = peer_ply_ - (ply_ + 1);
    if (j < 0 || j >= peer_n_) return false;
    *move = peer_recent_[j];
    return true;
}

void Link::played(uint32_t move, uint32_t now)
{
    if (void_ || ended_ != End::None) return;
    for (int k = kRecent - 1; k > 0; --k) { recent_[k] = recent_[k - 1]; hashes_[k] = hashes_[k - 1]; }
    hash_ = hash_move(hash_, move);
    recent_[0] = move;
    hashes_[0] = hash_;
    ++ply_;
    again_ = false;
    // Caught up with the partner: our games must be the same
    if (peer_game_ == game_no_ && peer_ply_ == ply_ && hash_ != peer_hash_check_) { disagree(now); return; }
    send_status(now);
}

void Link::disagree(uint32_t now)
{
    if (ended_ != End::None) return;
    void_ = true;
    send_status(now);
}

bool Link::up(uint32_t now) const { return heard_ && now - heard_ms_ < kLostMs; }

void Link::want_again(uint32_t now)
{
    if (ended_ != End::None) return;
    again_ = true;
    if (peer_again_ && peer_game_ == game_no_) next_game();
    send_status(now);
}

bool Link::started_next()
{
    const bool s = started_next_;
    started_next_ = false;
    return s;
}

bool Link::resumed()
{
    const bool r = resumed_;
    resumed_ = false;
    return r;
}

void Link::forfeit(uint32_t now, bool by_time)
{
    if (ended_ != End::None) return;
    by_time_ = by_time;
    stop(End::YouForfeited, now);
    send_status(now);
}

void Link::leave(uint32_t now)
{
    if (ended_ != End::None) return;
    stop(End::YouLeft, now);
    send_status(now);
}

void Link::done(uint32_t now)
{
    if (ended_ != End::None) return;
    stop(End::YouDone, now);
    send_status(now);
}

void Link::close(uint32_t now)
{
    if (ended_ != End::None) return;
    stop(End::Closed, now);
    send_status(now);
}

void Link::suspend(uint32_t now)
{
    if (ended_ != End::None || suspended_) return;
    suspended_ = true;
    meet_ = false;
    cont_ = false;
    send_status(now);
}

void Link::agree_continue(uint32_t now)
{
    if (ended_ != End::None) return;
    cont_ = true;
    meet_ = false;
    if (peer_cont_ && up(now)) resume(now);
    else send_status(now);
}

// ---- Save -----------------------------------------------------------------------------------
size_t Link::save(uint8_t* buf, size_t cap) const
{
    Writer w(buf, cap);
    w.u8('L'); w.u8('N'); w.u8('K'); w.u8('3');
    w.mac(me_);
    w.mac(peer_);
    w.u16(peer_a_);
    w.u16(peer_b_);
    w.u32(session_);
    w.u8(uint8_t((inviter_ ? 1 : 0) | (uint8_t(ended_) << 1) | (suspended_ ? 32 : 0) | (by_time_ ? 64 : 0)
                 | (void_ ? 128 : 0)));
    w.u8(uint8_t(game_key_));
    w.u16(timer_);
    w.u16(game_no_);
    w.u16(ply_);
    w.u32(hash_);
    for (int k = 0; k < kRecent; ++k) w.u32(recent_[k]);
    for (int k = 0; k < kRecent; ++k) w.u32(hashes_[k]);
    w.u8(0);
    return w.ok() && w.n == kSaveBytes ? w.n : 0;
}

bool Link::load(const uint8_t* buf, size_t len, const Air& air, uint32_t now)
{
    if (len != kSaveBytes || memcmp(buf, "LNK3", 4) != 0) return false;
    Link l;
    Reader r(buf, len, 4);
    r.mac(l.me_);
    r.mac(l.peer_);
    l.peer_a_ = r.u16();
    l.peer_b_ = r.u16();
    l.session_ = r.u32();
    const uint8_t f = r.u8();
    l.inviter_ = f & 1;
    const uint8_t end = uint8_t((f >> 1) & 15);
    if (end > uint8_t(End::PeerClosed)) return false;
    l.ended_ = End(end);
    l.suspended_ = (f & 32) != 0;
    l.by_time_ = (f & 64) != 0;
    l.void_ = (f & 128) != 0;
    l.game_key_ = r.u8();
    l.timer_ = r.u16();
    l.game_no_ = r.u16();
    l.ply_ = r.u16();
    l.hash_ = r.u32();
    for (int k = 0; k < kRecent; ++k) l.recent_[k] = r.u32();
    for (int k = 0; k < kRecent; ++k) l.hashes_[k] = r.u32();
    if (!r.ok() || l.peer_.zero()) return false;
    l.air_ = air;
    l.ended_ms_ = now - kLingerMs;            // an ending from before is said already
    l.next_ms_ = now;
    l.seq_ = seq_start(now ^ l.session_);
    l.peer_game_ = l.game_no_;
    l.peer_ply_ = l.ply_;
    l.peer_hash_check_ = l.hash_;
    // A session that was going when the board stopped is put away: it
    // carries on once both players meet again and say Continue
    if (l.ended_ == End::None) l.suspended_ = true;
    *this = l;
    return true;
}

} // namespace net
