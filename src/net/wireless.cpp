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
enum Ask : uint8_t { kAskNone = 0, kAskOffer = 1, kAskNo = 2 };
enum Beam : uint8_t { kAvailable = 1, kBusy = 2, kPaused = 4 };
// Status flags. Forfeit / done / gone end the session: gone = "this board
// has no such game" (its answer to a session it doesn't know).
enum Flag : uint8_t { kFlagAgain = 1, kFlagAway = 2, kFlagForfeit = 4, kFlagDone = 8, kFlagGone = 16 };
constexpr uint8_t kFlagEnds = kFlagForfeit | kFlagDone | kFlagGone;

constexpr size_t kHeader = 4;
constexpr size_t kBeaconBytes = kHeader + kNameMax + kFwMax + 1 + 2 + 1 + 1 + 6 + 4 + 1 + 1 + 2;
constexpr size_t kStatusFixed = kHeader + 6 + 4 + 2 + 2 + 1 + 4 + 2 + 1;

// Packets carry a counter so a late copy can't undo a newer one. A big
// jump back means the sender started again: taken.
bool stale(uint16_t seq, uint16_t last, bool have) { const int16_t d = int16_t(seq - last); return have && d <= 0 && d > -64; }
uint16_t seq_start(uint32_t now) { return uint16_t((now * 2654435761u) >> 16); }

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

// A bare status carrying only an ending flag, for a session this board
// doesn't play (any more)
void send_flag(const Air& air, const Mac& to, uint32_t session, uint8_t flag)
{
    if (!air.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kStatus);
    w.bytes(to.b, 6);
    w.u32(session);
    w.u16(0);
    w.u16(0);
    w.u8(flag);
    w.u32(0);
    w.u16(0);
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
    uint16_t seq = 0;
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
    s.seq = r.u16();
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

uint32_t status_session(const uint8_t* d, size_t n)
{
    if (packet_kind(d, n) != kStatus) return 0;
    Reader r(d, n);
    r.n = kHeader + 6;
    return r.u32();
}

// ---- Presence -------------------------------------------------------------------------------
void Presence::begin(const Mac& me, const char* fw, const Air& air, uint32_t now)
{
    *this = Presence{};
    me_ = me;
    air_ = air;
    copy(fw_, sizeof fw_, fw);
    next_ms_ = now;
    seq_ = seq_start(now);
}

void Presence::set_profile(const char* name, bool available, uint16_t games, int busy_game, bool paused,
                           uint32_t now)
{
    char clean[kNameMax + 1];
    clean_name(name, clean, sizeof clean);
    const bool changed = strcmp(clean, name_) != 0 || available != available_ || games != games_
                      || busy_game != busy_game_ || paused != paused_;
    copy(name_, sizeof name_, clean);
    available_ = available;
    games_ = games;
    busy_game_ = busy_game;
    paused_ = paused;
    if (busy_game >= 0 || !available) {
        // In a game, or hidden: no offers either way
        if (asking_) asking_ = false;
        if (asked_) {
            asked_ = false;
            no_to_ = asked_by_;
            no_session_ = asked_session_;
            no_reason_ = busy_game >= 0 ? Reason::Busy : Reason::GameOff;
            no_until_ms_ = now + 3000;
        }
    }
    if (changed) beacon(now);
}

bool Presence::same_version(int i) const { return near_[i].proto_ok && strcmp(near_[i].fw, fw_) == 0; }

bool Presence::can_offer(int i, int game) const
{
    const Nearby& b = near_[i];
    return same_version(i) && b.available && !b.busy && game >= 0 && game < kMaxGames
        && ((b.games >> game) & 1);
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

void Presence::beacon(uint32_t now)
{
    next_ms_ = now + kSendMs;
    if (!air_.send) return;
    uint8_t buf[kPacketMax];
    Writer w(buf, sizeof buf);
    header(w, kBeacon);
    w.text(name_, kNameMax);
    w.text(fw_, kFwMax);
    w.u8(uint8_t((available_ ? kAvailable : 0) | (busy_game_ >= 0 ? kBusy : 0) | (paused_ ? kPaused : 0)));
    w.u16(games_);
    w.u8(uint8_t(busy_game_ >= 0 ? busy_game_ : 0xFF));
    uint8_t ask = kAskNone, game = 0xFF, reason = 0;
    Mac to;
    uint32_t session = 0;
    if (asking_) { ask = kAskOffer; to = ask_to_; session = ask_session_; game = uint8_t(ask_game_); }
    else if (int32_t(no_until_ms_ - now) > 0) { ask = kAskNo; to = no_to_; session = no_session_; reason = uint8_t(no_reason_); }
    w.u8(ask);
    w.bytes(to.b, 6);
    w.u32(session);
    w.u8(game);
    w.u8(reason);
    w.u16(seq_++);
    air_.send(buf, w.n, air_.ctx);
}

void Presence::tick(uint32_t now)
{
    for (int i = 0; i < n_;) {
        if (now - near_[i].seen_ms > kForgetMs) {
            if (asking_ && near_[i].mac == ask_to_) { asking_ = false; event_ = Event::Gone; }
            near_[i] = near_[--n_];
        } else {
            ++i;
        }
    }
    if (asked_ && now - asked_seen_ms_ > kForgetMs) asked_ = false;
    if (asking_ && now - ask_since_ > kOfferMs) {
        asking_ = false;
        event_ = Event::NoAnswer;
        beacon(now);
    }
    if (int32_t(now - next_ms_) >= 0) beacon(now);
}

void Presence::receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now)
{
    if (from == me_) return;
    bool other = false;
    const int kind = packet_kind(d, n, &other);
    if (other) {                                     // another protocol: listed by name only
        if (n < kHeader + kNameMax) return;
        Nearby* b = lookup(from);
        if (!b && n_ < kMaxNearby) { b = &near_[n_++]; *b = Nearby{}; b->mac = from; }
        if (!b) return;
        Reader r(d, n);
        r.n = kHeader;
        r.text(b->name, kNameMax);
        copy(b->fw, sizeof b->fw, "?");
        b->proto_ok = false;
        b->available = true;
        b->busy = false;
        b->games = 0;
        b->seen_ms = now;
        return;
    }
    if (kind == kBeacon) {
        Reader r(d, n);
        r.n = kHeader;
        Nearby nb;
        nb.mac = from;
        r.text(nb.name, kNameMax);
        r.text(nb.fw, kFwMax);
        const uint8_t beam = r.u8();
        nb.games = r.u16();
        const uint8_t bg = r.u8();
        const uint8_t ask = r.u8();
        Mac to;
        r.bytes(to.b, 6);
        const uint32_t session = r.u32();
        const uint8_t game = r.u8();
        const uint8_t reason = r.u8();
        nb.seq = r.u16();
        if (!r.ok()) return;
        Nearby* known = lookup(from);
        if (known && known->proto_ok && stale(nb.seq, known->seq, true)) return;   // a late copy
        nb.available = beam & kAvailable;
        nb.busy = beam & kBusy;
        nb.paused = beam & kPaused;
        nb.busy_game = (nb.busy && bg < kMaxGames) ? bg : -1;
        nb.seen_ms = now;
        if (!nb.name[0]) copy(nb.name, sizeof nb.name, "?");
        Nearby* b = lookup(from);
        if (nb.available || nb.busy) {
            if (!b && n_ < kMaxNearby) b = &near_[n_++];
            if (b) *b = nb;
        } else if (b) {
            *b = near_[--n_];                        // it went hidden: off the list
        }
        // Offers to this board ride on the asker's beacons; one without it = withdrawn
        if (ask == kAskOffer && to == me_ && game < kMaxGames) {
            if (event_ == Event::Started || (session == session_ && from == partner_ && !inviter_)) {
                // the offer this board just said Play to: the asker hasn't heard yet
            } else if (busy_game_ >= 0 || !available_ || !((games_ >> game) & 1)) {
                // Can't take it: say why (a game on already, or that game is off)
                if (!(no_to_ == from && no_session_ == session && int32_t(no_until_ms_ - now) > 0)) {
                    no_to_ = from;
                    no_session_ = session;
                    no_reason_ = busy_game_ >= 0 ? Reason::Busy : Reason::GameOff;
                    no_until_ms_ = now + 3000;
                    beacon(now);
                }
            } else {
                if (!asked_ || asked_by_ != from || asked_session_ != session) {
                    asked_ = true;
                    asked_by_ = from;
                    asked_session_ = session;
                }
                asked_game_ = game;
                copy(asked_name_, sizeof asked_name_, nb.name);
                asked_seen_ms_ = now;
            }
        } else if (asked_ && asked_by_ == from) {
            asked_ = false;
        }
        if (ask == kAskNo && to == me_ && asking_ && from == ask_to_ && session == ask_session_) {
            asking_ = false;
            reason_ = reason <= uint8_t(Reason::GameOff) ? Reason(reason) : Reason::NotNow;
            event_ = Event::Declined;
            beacon(now);
        }
        return;
    }
    if (kind == kStatus) {
        StatusHead s;
        if (!read_status(d, n, s) || s.to != me_) return;
        if (asking_ && from == ask_to_ && s.session == ask_session_ && !(s.flags & kFlagEnds)) {
            // The asked board said Play and started: so do we
            asking_ = false;
            event_ = Event::Started;
            partner_ = from;
            copy(partner_name_, sizeof partner_name_, ask_name_);
            session_ = s.session;
            game_ = ask_game_;
            inviter_ = true;
            beacon(now);
        } else if (!(s.flags & kFlagEnds)) {
            send_flag(air_, from, s.session, kFlagGone);   // a game this board isn't playing
        }
    }
}

void Presence::offer(int i, int game, uint32_t session, uint32_t now)
{
    if (i < 0 || i >= n_ || !can_offer(i, game) || busy_game_ >= 0) return;
    asking_ = true;
    ask_to_ = near_[i].mac;
    copy(ask_name_, sizeof ask_name_, near_[i].name);
    ask_session_ = session;
    ask_game_ = game;
    ask_since_ = now;
    beacon(now);
}

void Presence::cancel(uint32_t now)
{
    if (!asking_) return;
    asking_ = false;
    beacon(now);
}

void Presence::accept(uint32_t now)
{
    if (!asked_) return;
    asked_ = false;
    asking_ = false;                 // a game now: any offer of ours is off
    event_ = Event::Started;
    partner_ = asked_by_;
    copy(partner_name_, sizeof partner_name_, asked_name_);
    session_ = asked_session_;
    game_ = asked_game_;
    inviter_ = false;
    beacon(now);
}

void Presence::decline(Reason why, uint32_t now)
{
    if (!asked_) return;
    asked_ = false;
    no_to_ = asked_by_;
    no_session_ = asked_session_;
    no_reason_ = why;
    no_until_ms_ = now + 3000;
    beacon(now);
}

Presence::Event Presence::poll()
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
    seq_ = seq_start(now ^ session);
    send_status(now);
}

int Link::my_side() const
{
    const int inviter_side = game_no_ & 1;   // the first mover alternates game by game
    return inviter_ ? inviter_side : 1 - inviter_side;
}

uint8_t Link::reply_flag() const
{
    switch (ended_) {
        case End::YouForfeited: return kFlagForfeit;
        case End::YouDone:      return kFlagDone;
        default:                return kFlagGone;
    }
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
    if (away_) flags |= kFlagAway;
    if (ended_ != End::None) flags |= reply_flag();
    w.u8(flags);
    w.u32(hash_);
    w.u16(seq_++);
    const int n = ply_ < kRecent ? ply_ : kRecent;
    w.u8(uint8_t(n));
    for (int k = 0; k < n; ++k) w.u16(recent_[k]);
    air_.send(buf, w.n, air_.ctx);
}

void Link::tick(uint32_t now)
{
    // An ending this board made is repeated for a while, so the other board hears it
    const bool saying_end = (ended_ == End::YouForfeited || ended_ == End::YouDone || ended_ == End::YouLeft)
                            && !linger_over(now);
    if (ended_ != End::None && !saying_end) return;
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
        if (!(s.flags & kFlagEnds)) send_flag(air_, from, s.session, kFlagGone);   // not our game
        return;
    }
    heard_ = true;
    heard_ms_ = now;
    if (ended_ != End::None) {
        // Still being asked about a game that ended here: say so again
        if (!(s.flags & kFlagEnds)) send_flag(air_, from, s.session, reply_flag());
        return;
    }
    if (s.flags & kFlagForfeit) { stop(End::PeerForfeited, now); return; }
    if (s.flags & kFlagDone)    { stop(End::PeerDone, now); return; }
    if (s.flags & kFlagGone)    { stop(End::PeerGone, now); return; }
    if (stale(s.seq, peer_seq_, have_seq_)) return;      // a late copy of an older status
    peer_seq_ = s.seq;
    have_seq_ = true;
    peer_away_ = (s.flags & kFlagAway) != 0;
    if (s.game < game_no_) return;                       // it will catch up with ours
    if (s.game == game_no_ + 1) {
        if (!again_) { stop(End::OutOfStep, now); send_flag(air_, peer_, session_, kFlagGone); return; }
        next_game();
        send_status(now);
    } else if (s.game > game_no_ + 1) {
        stop(End::OutOfStep, now);
        send_flag(air_, peer_, session_, kFlagGone);
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
        if (!ok || h != s.hash) { disagree(now); return; }
    } else if (s.ply - ply_ > s.n) {
        disagree(now);                                     // moves we can't get back
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
    if (peer_game_ == game_no_ && peer_ply_ == ply_ && hash_ != peer_hash_check_) { disagree(now); return; }
    send_status(now);
}

void Link::disagree(uint32_t now)
{
    stop(End::OutOfStep, now);
    send_flag(air_, peer_, session_, kFlagGone);
}

bool Link::up(uint32_t now) const { return heard_ && now - heard_ms_ < kLostMs; }

void Link::set_away(bool away, uint32_t now)
{
    if (away == away_) return;
    away_ = away;
    if (ended_ == End::None) send_status(now);
}

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

void Link::forfeit(uint32_t now)
{
    if (ended_ != End::None) return;
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

// ---- Save -----------------------------------------------------------------------------------
size_t Link::save(uint8_t* buf, size_t cap) const
{
    Writer w(buf, cap);
    w.u8('L'); w.u8('N'); w.u8('K'); w.u8('2');
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
    if (len != kSaveBytes || memcmp(buf, "LNK2", 4) != 0) return false;
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
    if (end > uint8_t(End::YouLeft)) return false;
    l.ended_ = End(end);
    l.ended_ms_ = now - kLingerMs;            // an ending from before is said already
    l.game_no_ = r.u16();
    l.ply_ = r.u16();
    l.hash_ = r.u32();
    for (int k = 0; k < kRecent; ++k) l.recent_[k] = r.u16();
    for (int k = 0; k < kRecent; ++k) l.hashes_[k] = r.u32();
    if (!r.ok() || l.peer_.zero()) return false;
    l.air_ = air;
    l.next_ms_ = now;
    l.seq_ = seq_start(now ^ l.session_);
    l.peer_game_ = l.game_no_;
    l.peer_ply_ = l.ply_;
    l.peer_hash_check_ = l.hash_;
    *this = l;
    return true;
}

} // namespace net
