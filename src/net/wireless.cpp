#include "wireless.h"

#include <cstdio>
#include <cstring>

namespace net {

namespace {

// Packet: 'C' 'Y' proto kind, then the body. The player name sits right
// after the header in a beacon in every protocol version, so a board with
// another version can still be listed by name.
constexpr uint8_t kMagic0 = 'C', kMagic1 = 'Y';
enum Kind : uint8_t { kBeacon = 1, kStatus = 2 };
enum Ask : uint8_t { kAskNone = 0, kAskInvite = 1, kAskNo = 3 };
enum Flag : uint8_t { kFlagAgain = 1, kFlagAway = 2, kFlagLeft = 4 };

constexpr size_t kHeader = 4;
constexpr size_t kBeaconBytes = kHeader + kNameMax + kGameMax + kFwMax + 1 + 6 + 4;
constexpr size_t kStatusFixed = kHeader + 6 + 4 + 2 + 2 + 1 + 4 + 1;

struct Writer {
    uint8_t* p;
    size_t   n = 0, cap;
    Writer(uint8_t* b, size_t c) : p(b), cap(c) {}
    void u8(uint8_t v)   { if (n < cap) p[n] = v; ++n; }
    void u16(uint16_t v) { u8(uint8_t(v)); u8(uint8_t(v >> 8)); }
    void u32(uint32_t v) { u16(uint16_t(v)); u16(uint16_t(v >> 16)); }
    void bytes(const uint8_t* b, size_t len) { for (size_t k = 0; k < len; ++k) u8(b[k]); }
    void text(const char* s, size_t len)       // fixed width, zero padded
    {
        size_t k = 0;
        for (; k < len && s[k]; ++k) u8(uint8_t(s[k]));
        for (; k < len; ++k) u8(0);
    }
    bool ok() const { return n <= cap; }
};

struct Reader {
    const uint8_t* p;
    size_t n = 0, len;
    Reader(const uint8_t* b, size_t l) : p(b), len(l) {}
    bool     more(size_t k) const { return n + k <= len; }
    uint8_t  u8()  { return n < len ? p[n++] : (n++, 0); }
    uint16_t u16() { const uint16_t a = u8(); return uint16_t(a | (u8() << 8)); }
    uint32_t u32() { const uint32_t a = u16(); return a | (uint32_t(u16()) << 16); }
    void bytes(uint8_t* b, size_t k) { for (size_t i = 0; i < k; ++i) b[i] = u8(); }
    void text(char* out, size_t k)             // k bytes into a NUL-ended string (out holds k + 1)
    {
        for (size_t i = 0; i < k; ++i) {
            const uint8_t c = u8();
            out[i] = (c >= 32 && c < 127) ? char(c) : 0;
        }
        out[k] = 0;
    }
    bool ok() const { return n <= len; }
};

void header(Writer& w, uint8_t kind)
{
    w.u8(kMagic0);
    w.u8(kMagic1);
    w.u8(kProto);
    w.u8(kind);
}

void copy(char* out, size_t cap, const char* in)
{
    snprintf(out, cap, "%s", in ? in : "");
}

// A status that says "left" for someone else's session: the answer to a
// board that still thinks it plays a game this board no longer has
void send_left(const Air& air, const Mac& to, uint32_t session)
{
    if (!air.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kStatus);
    w.bytes(to.b, 6);
    w.u32(session);
    w.u16(0);
    w.u16(0);
    w.u8(kFlagLeft);
    w.u32(0);
    w.u8(0);
    air.send(buf, w.n, air.ctx);
}

// The parts of a status every receiver looks at first
struct StatusHead {
    Mac      to;
    uint32_t session = 0;
    uint16_t game = 0, ply = 0;
    uint8_t  flags = 0;
    uint32_t hash = 0;
    uint8_t  n = 0;
    uint16_t moves[kRecent] = {};
};

bool read_status(const uint8_t* d, size_t len, StatusHead& s)
{
    if (len < kStatusFixed) return false;
    Reader r(d, len);
    r.n = kHeader;
    r.bytes(s.to.b, 6);
    s.session = r.u32();
    s.game = r.u16();
    s.ply = r.u16();
    s.flags = r.u8();
    s.hash = r.u32();
    s.n = r.u8();
    if (s.n > kRecent || !r.more(2u * s.n)) return false;
    for (int k = 0; k < s.n; ++k) s.moves[k] = r.u16();
    return r.ok();
}

} // namespace

bool Mac::operator==(const Mac& o) const { return memcmp(b, o.b, 6) == 0; }
bool Mac::zero() const { static const uint8_t z[6] = {}; return memcmp(b, z, 6) == 0; }

uint32_t hash_move(uint32_t h, int move)
{
    const uint16_t m = uint16_t(move);
    h = (h ^ (m & 0xFF)) * 16777619u;
    h = (h ^ (m >> 8)) * 16777619u;
    return h;
}

void default_name(const Mac& me, char* out, size_t cap)
{
    snprintf(out, cap, "CYD-%02X%02X", me.b[4], me.b[5]);
}

void clean_name(const char* in, char* out, size_t cap)
{
    size_t n = 0;
    if (cap == 0) return;
    while (in && *in == ' ') ++in;
    for (; in && *in && n + 1 < cap && n < kNameMax; ++in) {
        const char c = *in;
        out[n++] = (c >= 32 && c < 127) ? c : '?';
    }
    while (n && out[n - 1] == ' ') --n;
    out[n] = 0;
}

int packet_kind(const uint8_t* d, size_t n, bool* other_proto)
{
    if (other_proto) *other_proto = false;
    if (!d || n < kHeader || d[0] != kMagic0 || d[1] != kMagic1) return 0;
    if (d[2] != kProto) { if (other_proto) *other_proto = true; return 0; }
    if (d[3] == kBeacon && n >= kBeaconBytes) return kBeacon;
    if (d[3] == kStatus && n >= kStatusFixed) return kStatus;
    return 0;
}

// ---- Lobby ----------------------------------------------------------------------------------
void Lobby::begin(const Mac& me, const char* name, const char* game, const char* fw, const Air& air, uint32_t now)
{
    *this = Lobby{};
    me_ = me;
    air_ = air;
    clean_name(name, name_, sizeof name_);
    copy(game_, sizeof game_, game);
    copy(fw_, sizeof fw_, fw);
    next_ms_ = now;
}

void Lobby::set_name(const char* name, uint32_t now)
{
    clean_name(name, name_, sizeof name_);
    beacon(now);
}

bool Lobby::same_game(int i) const { return near_[i].proto_ok && strcmp(near_[i].game, game_) == 0; }
bool Lobby::same_version(int i) const { return near_[i].proto_ok && strcmp(near_[i].fw, fw_) == 0; }

void Lobby::beacon(uint32_t now)
{
    next_ms_ = now + kSendMs;
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kBeacon);
    w.text(name_, kNameMax);
    w.text(game_, kGameMax);
    w.text(fw_, kFwMax);
    uint8_t ask = kAskNone;
    Mac to;
    uint32_t session = 0;
    if (asking_) { ask = kAskInvite; to = ask_to_; session = ask_session_; }
    else if (int32_t(no_until_ms_ - now) > 0) { ask = kAskNo; to = no_to_; session = no_session_; }
    w.u8(ask);
    w.bytes(to.b, 6);
    w.u32(session);
    air_.send(buf, w.n, air_.ctx);
}

void Lobby::tick(uint32_t now)
{
    // Forget boards gone quiet (and an invite to or from one of them)
    for (int i = 0; i < n_;) {
        if (now - near_[i].seen_ms > kForgetMs) {
            if (asking_ && near_[i].mac == ask_to_) { asking_ = false; event_ = Event::Gone; }
            near_[i] = near_[--n_];
        } else {
            ++i;
        }
    }
    if (asked_ && now - asked_seen_ms_ > kForgetMs) asked_ = false;
    if (int32_t(now - next_ms_) >= 0) beacon(now);
}

Nearby* Lobby::find(const Mac& m)
{
    for (int i = 0; i < n_; ++i) if (near_[i].mac == m) return &near_[i];
    return nullptr;
}

void Lobby::receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    if (from == me_) return;
    bool other = false;
    const int kind = packet_kind(d, n, &other);
    if (other) {                                     // another protocol: list it by name only
        if (n < kHeader + kNameMax) return;
        Nearby* b = find(from);
        if (!b && n_ < kMaxNearby) { b = &near_[n_++]; *b = Nearby{}; b->mac = from; }
        if (!b) return;
        Reader r(d, n);
        r.n = kHeader;
        r.text(b->name, kNameMax);
        b->game[0] = 0;
        copy(b->fw, sizeof b->fw, "?");
        b->proto_ok = false;
        b->seen_ms = now;
        return;
    }
    if (kind == kBeacon) {
        Reader r(d, n);
        r.n = kHeader;
        Nearby nb;
        nb.mac = from;
        r.text(nb.name, kNameMax);
        r.text(nb.game, kGameMax);
        r.text(nb.fw, kFwMax);
        const uint8_t ask = r.u8();
        Mac to;
        r.bytes(to.b, 6);
        const uint32_t session = r.u32();
        if (!r.ok()) return;
        nb.seen_ms = now;
        if (!nb.name[0]) copy(nb.name, sizeof nb.name, "?");
        Nearby* b = find(from);
        if (!b && n_ < kMaxNearby) b = &near_[n_++];
        if (b) *b = nb;
        // Invites to this board ride on the asker's beacons; one without it = withdrawn
        if (ask == kAskInvite && to == me_) {
            if (!asked_ || asked_by_ != from || asked_session_ != session) {
                asked_ = true;
                asked_by_ = from;
                asked_session_ = session;
            }
            copy(asked_name_, sizeof asked_name_, nb.name);
            asked_seen_ms_ = now;
        } else if (asked_ && asked_by_ == from) {
            asked_ = false;
        }
        if (ask == kAskNo && to == me_ && asking_ && from == ask_to_ && session == ask_session_) {
            asking_ = false;
            event_ = Event::Declined;
        }
        return;
    }
    if (kind == kStatus) {
        StatusHead s;
        if (!read_status(d, n, s) || s.to != me_) return;
        if (asking_ && from == ask_to_ && s.session == ask_session_ && !(s.flags & kFlagLeft)) {
            // The invited board said Play and started: so do we
            asking_ = false;
            event_ = Event::Started;
            partner_ = from;
            copy(partner_name_, sizeof partner_name_, ask_name_);
            session_ = s.session;
            inviter_ = true;
        } else if (!(s.flags & kFlagLeft)) {
            send_left(air_, from, s.session);         // a game this board isn't playing
        }
    }
}

void Lobby::invite(int i, uint32_t session, uint32_t now)
{
    if (i < 0 || i >= n_ || !can_invite(i)) return;
    asking_ = true;
    ask_to_ = near_[i].mac;
    copy(ask_name_, sizeof ask_name_, near_[i].name);
    ask_session_ = session;
    beacon(now);
}

void Lobby::cancel(uint32_t now)
{
    if (!asking_) return;
    asking_ = false;
    beacon(now);
}

void Lobby::accept(uint32_t now)
{
    (void)now;
    if (!asked_) return;
    asked_ = false;
    asking_ = false;                 // a game now: any invite of ours is off
    event_ = Event::Started;
    partner_ = asked_by_;
    copy(partner_name_, sizeof partner_name_, asked_name_);
    session_ = asked_session_;
    inviter_ = false;
}

void Lobby::decline(uint32_t now)
{
    if (!asked_) return;
    asked_ = false;
    no_to_ = asked_by_;
    no_session_ = asked_session_;
    no_until_ms_ = now + 3000;
    beacon(now);
}

Lobby::Event Lobby::poll()
{
    const Event e = event_;
    event_ = Event::None;
    return e;
}

// ---- Link -----------------------------------------------------------------------------------
void Link::begin(const Mac& me, const Mac& peer, const char* peer_name, uint32_t session, bool inviter,
                 const Air& air, uint32_t now)
{
    *this = Link{};
    me_ = me;
    peer_ = peer;
    clean_name(peer_name, peer_name_, sizeof peer_name_);
    session_ = session;
    inviter_ = inviter;
    air_ = air;
    hash_ = kHashStart;
    peer_hash_check_ = kHashStart;
    send_status(now);
}

int Link::my_side() const
{
    const int inviter_side = game_no_ & 1;   // the first mover alternates game by game
    return inviter_ ? inviter_side : 1 - inviter_side;
}

void Link::send_status(uint32_t now, uint8_t extra)
{
    next_ms_ = now + kSendMs;
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kStatus);
    w.bytes(peer_.b, 6);
    w.u32(session_);
    w.u16(game_no_);
    w.u16(ply_);
    uint8_t flags = extra;
    if (again_) flags |= kFlagAgain;
    if (ended_ == End::YouLeft) flags |= kFlagLeft;
    w.u8(flags);
    w.u32(hash_);
    const int n = ply_ < kRecent ? ply_ : kRecent;
    w.u8(uint8_t(n));
    for (int k = 0; k < n; ++k) w.u16(recent_[k]);
    air_.send(buf, w.n, air_.ctx);
}

void Link::tick(uint32_t now)
{
    if (ended_ != End::None) return;
    if (int32_t(now - next_ms_) >= 0) send_status(now);
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

void Link::stop(End why)
{
    if (ended_ == End::None) ended_ = why;
}

void Link::next_game()
{
    ++game_no_;
    ply_ = 0;
    hash_ = kHashStart;
    again_ = false;
    peer_again_ = false;
    peer_game_ = game_no_;
    peer_ply_ = 0;
    peer_n_ = 0;
    peer_hash_check_ = kHashStart;
    started_next_ = true;
}

void Link::receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    if (packet_kind(d, n) != kStatus) return;
    StatusHead s;
    if (!read_status(d, n, s) || s.to != me_) return;
    if (from != peer_ || s.session != session_) {
        if (!(s.flags & kFlagLeft)) send_left(air_, from, s.session);   // not our game
        return;
    }
    heard_ = true;
    heard_ms_ = now;
    if (ended_ != End::None) {
        // Still being asked about a game that ended here: say so again
        if (!(s.flags & kFlagLeft)) send_left(air_, from, s.session);
        return;
    }
    if (s.flags & kFlagLeft) { stop(End::PeerLeft); return; }
    peer_away_ = (s.flags & kFlagAway) != 0;
    if (s.game < game_no_) return;                       // it will catch up with ours
    if (s.game == game_no_ + 1) {
        if (!again_) { stop(End::OutOfStep); return; }  // a game we never agreed to
        next_game();
        send_status(now);
    } else if (s.game > game_no_ + 1) {
        stop(End::OutOfStep);
        return;
    }
    peer_again_ = (s.flags & kFlagAgain) != 0;
    peer_game_ = s.game;
    peer_ply_ = s.ply;
    peer_n_ = s.n;
    for (int k = 0; k < s.n; ++k) peer_recent_[k] = s.moves[k];
    if (s.ply <= ply_) {
        bool ok;
        const uint32_t h = hash_at(s.ply, &ok);
        // Too far behind to catch up, or a different game: they disagree
        if (!ok || h != s.hash) { stop(End::OutOfStep); return; }
    } else if (s.ply - ply_ > s.n) {
        stop(End::OutOfStep);                              // moves we can't get back
        return;
    }                                                      // else next_move() hands them over
    peer_hash_check_ = s.hash;
    if (again_ && peer_again_) {
        next_game();
        send_status(now);
    }
}

int Link::next_move() const
{
    if (ended_ != End::None || peer_game_ != game_no_ || peer_ply_ <= ply_) return -1;
    const int j = peer_ply_ - (ply_ + 1);
    if (j < 0 || j >= peer_n_) return -1;
    return peer_recent_[j];
}

void Link::played(int move, uint32_t now)
{
    for (int k = kRecent - 1; k > 0; --k) { recent_[k] = recent_[k - 1]; hashes_[k] = hashes_[k - 1]; }
    hash_ = hash_move(hash_, move);
    recent_[0] = uint16_t(move);
    hashes_[0] = hash_;
    ++ply_;
    again_ = false;
    // Caught up with the partner: our games must be the same
    if (peer_game_ == game_no_ && peer_ply_ == ply_ && hash_ != peer_hash_check_) stop(End::OutOfStep);
    send_status(now);
}

void Link::disagree(uint32_t now)
{
    stop(End::OutOfStep);
    send_left(air_, peer_, session_);
    (void)now;
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

void Link::leave(uint32_t now)
{
    if (ended_ == End::None) ended_ = End::YouLeft;
    for (int k = 0; k < 3; ++k) send_status(now, kFlagLeft);   // the radio goes off next
}

void Link::away(uint32_t now)
{
    if (ended_ != End::None) return;
    for (int k = 0; k < 2; ++k) send_status(now, kFlagAway);
}

// ---- Save -----------------------------------------------------------------------------------
size_t Link::save(uint8_t* buf, size_t cap) const
{
    Writer w(buf, cap);
    w.u8('L'); w.u8('N'); w.u8('K'); w.u8('1');
    w.bytes(me_.b, 6);
    w.bytes(peer_.b, 6);
    w.text(peer_name_, kNameMax + 1);
    w.u32(session_);
    w.u8(uint8_t((inviter_ ? 1 : 0) | (uint8_t(ended_) << 1)));
    w.u16(game_no_);
    w.u16(ply_);
    w.u32(hash_);
    for (int k = 0; k < kRecent; ++k) w.u16(recent_[k]);
    for (int k = 0; k < kRecent; ++k) w.u32(hashes_[k]);
    return w.ok() && w.n == kSaveBytes ? w.n : 0;
}

bool Link::load(const uint8_t* buf, size_t len, const Air& air, uint32_t now)
{
    if (len != kSaveBytes || memcmp(buf, "LNK1", 4) != 0) return false;
    Link l;
    Reader r(buf, len);
    r.n = 4;
    r.bytes(l.me_.b, 6);
    r.bytes(l.peer_.b, 6);
    r.text(l.peer_name_, kNameMax);
    r.u8();                                   // the name's last byte (always 0)
    l.session_ = r.u32();
    const uint8_t f = r.u8();
    l.inviter_ = f & 1;
    const uint8_t end = uint8_t(f >> 1);
    if (end > uint8_t(End::OutOfStep)) return false;
    l.ended_ = End(end);
    l.game_no_ = r.u16();
    l.ply_ = r.u16();
    l.hash_ = r.u32();
    for (int k = 0; k < kRecent; ++k) l.recent_[k] = r.u16();
    for (int k = 0; k < kRecent; ++k) l.hashes_[k] = r.u32();
    if (!r.ok() || l.peer_.zero()) return false;
    l.air_ = air;
    l.next_ms_ = now;
    l.peer_game_ = l.game_no_;
    l.peer_ply_ = l.ply_;
    l.peer_hash_check_ = l.hash_;
    *this = l;
    return true;
}

} // namespace net
