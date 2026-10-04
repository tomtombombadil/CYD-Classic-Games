// CYD-to-CYD play: the messages and the two state machines behind them.
// Plain C++ (no Arduino, no LVGL), host-tested in tools/host_tests/test_net.cpp
// with a fake radio that loses, repeats and delays packets.
//
// The air: ESP-NOW broadcasts on one WiFi channel (src/hal/radio.*). Every
// packet goes to everyone in range; the ones meant for one board carry its
// address. Nothing is acknowledged: each board keeps sending its whole
// state (a beacon in the lobby, a status in a game) twice a second, and at
// once when it changes, so a lost packet is just replaced by the next one.
//
// Lobby ("Play Nearby"): boards in it beacon their player name, the game
// they want to play and their firmware version. Tapping a board that wants
// the same game with the same firmware invites it (the invite rides on the
// beacons); the other board shows the question and Play / No Thanks. Play
// starts the game there at once; the inviter starts when the first status
// of that game reaches it.
//
// Link (a game in progress): both boards send a status - which session,
// which game of the session, how many moves were played, the last few moves
// and a hash of all of them. A board that is behind plays the moves it
// missed (after checking each against its own rules); a hash that doesn't
// match means the boards disagree, and the game ends unrecorded. Nothing
// heard for 3 s = the link is down and the game waits. Play Again starts the
// next game once both boards asked for it; whoever moved second moves first.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

constexpr uint8_t  kProto = 1;              // packet format; a change makes boards incompatible
constexpr size_t   kNameMax = 12;           // characters in a player name
constexpr size_t   kGameMax = 11;           // characters in a game id
constexpr size_t   kFwMax = 15;             // characters in a firmware version
constexpr size_t   kPacketMax = 96;         // biggest packet we send or take
constexpr int      kRecent = 12;            // moves repeated in every status
constexpr uint32_t kSendMs = 500;           // status / beacon repeat
constexpr uint32_t kLostMs = 3000;          // nothing heard this long = link down
constexpr uint32_t kForgetMs = 4000;        // a lobby board not heard this long goes away

struct Mac {
    uint8_t b[6] = {};
    bool operator==(const Mac& o) const;
    bool operator!=(const Mac& o) const { return !(*this == o); }
    bool zero() const;
};

// How the state machines reach the air: broadcast one packet
struct Air {
    void (*send)(const uint8_t* data, size_t len, void* ctx) = nullptr;
    void* ctx = nullptr;
};

// "CYD-3F2A" from the board's address: the name until the player picks one
void default_name(const Mac& me, char* out, size_t cap);
// A name as it is sent and stored: trimmed, at most kNameMax characters, printable ASCII
void clean_name(const char* in, char* out, size_t cap);
// The packet's kind, or 0 if it isn't ours / not this protocol. *other_proto
// is set when it is ours but from another protocol version.
int  packet_kind(const uint8_t* d, size_t n, bool* other_proto = nullptr);

// ---- Lobby --------------------------------------------------------------------------------
struct Nearby {
    Mac      mac;
    char     name[kNameMax + 1] = "";
    char     game[kGameMax + 1] = "";
    char     fw[kFwMax + 1] = "";
    bool     proto_ok = true;               // same packet format
    uint32_t seen_ms = 0;
};

class Lobby {
public:
    static constexpr int kMaxNearby = 8;
    enum class Event { None, Declined, Started, Gone };

    void begin(const Mac& me, const char* name, const char* game, const char* fw, const Air& air, uint32_t now);
    void set_name(const char* name, uint32_t now);
    void tick(uint32_t now);                 // beacon, forget boards gone quiet
    void receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    int  count() const { return n_; }
    const Nearby& at(int i) const { return near_[i]; }
    bool same_game(int i) const;             // wants this game
    bool same_version(int i) const;          // and runs this firmware: can be invited
    bool can_invite(int i) const { return same_game(i) && same_version(i); }

    // Asking someone (one at a time)
    void invite(int i, uint32_t session, uint32_t now);
    void cancel(uint32_t now);
    bool inviting() const { return asking_; }
    const char* invitee_name() const { return ask_name_; }

    // Being asked: the newest invite for this board, if its sender still asks
    bool asked() const { return asked_; }
    const char* asker_name() const { return asked_name_; }
    void accept(uint32_t now);               // Event::Started at once
    void decline(uint32_t now);

    // What happened (cleared by reading). After Started: who and which
    // session; `inviter` = this board asked (it moves first in game 1).
    Event poll();
    const Mac& partner() const { return partner_; }
    const char* partner_name() const { return partner_name_; }
    uint32_t session() const { return session_; }
    bool inviter() const { return inviter_; }

private:
    void beacon(uint32_t now);
    Nearby* find(const Mac& m);

    Air      air_;
    Mac      me_;
    char     name_[kNameMax + 1] = "";
    char     game_[kGameMax + 1] = "";
    char     fw_[kFwMax + 1] = "";
    Nearby   near_[kMaxNearby];
    int      n_ = 0;
    uint32_t next_ms_ = 0;
    // asking
    bool     asking_ = false;
    Mac      ask_to_;
    char     ask_name_[kNameMax + 1] = "";
    uint32_t ask_session_ = 0;
    // being asked
    bool     asked_ = false;
    Mac      asked_by_;
    char     asked_name_[kNameMax + 1] = "";
    uint32_t asked_session_ = 0;
    uint32_t asked_seen_ms_ = 0;
    // a "no thanks" repeated on the beacons for a while
    Mac      no_to_;
    uint32_t no_session_ = 0;
    uint32_t no_until_ms_ = 0;
    // outcome
    Event    event_ = Event::None;
    Mac      partner_;
    char     partner_name_[kNameMax + 1] = "";
    uint32_t session_ = 0;
    bool     inviter_ = false;
};

// ---- Link ---------------------------------------------------------------------------------
class Link {
public:
    // Saved with the game (both boards), so a game can carry on later
    static constexpr size_t kSaveBytes = 4 + 6 + 6 + (kNameMax + 1) + 4 + 1 + 2 + 2 + 4 + 2 * kRecent + 4 * kRecent;

    // A new session: game 1 starts with no moves
    void begin(const Mac& me, const Mac& peer, const char* peer_name, uint32_t session, bool inviter,
               const Air& air, uint32_t now);
    size_t save(uint8_t* buf, size_t cap) const;
    bool   load(const uint8_t* buf, size_t len, const Air& air, uint32_t now);
    void   set_air(const Air& air) { air_ = air; }

    void tick(uint32_t now);                 // status every kSendMs
    void receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    // Turns: the side this board plays in the current game (0 moves first)
    int  my_side() const;
    int  game_no() const { return game_no_; }  // 0 = the first game of the session
    int  ply() const { return ply_; }
    const char* peer_name() const { return peer_name_; }
    const Mac& peer() const { return peer_; }
    uint32_t session() const { return session_; }

    // A move made on this board (yours, or the partner's from next_move())
    void played(int move, uint32_t now);
    // The partner's next move this board hasn't played yet, or -1. The game
    // checks it is legal before playing it (then calls played()); if not,
    // call disagree().
    int  next_move() const;
    void disagree(uint32_t now);

    bool up(uint32_t now) const;             // heard from the partner lately
    bool heard() const { return heard_; }    // heard at all since opening
    bool peer_away() const { return peer_away_; }
    bool peer_again() const { return peer_again_; }
    bool again() const { return again_; }
    bool ended() const { return ended_ != End::None; }
    enum class End : uint8_t { None, YouLeft, PeerLeft, OutOfStep };
    End  end_reason() const { return ended_; }

    // Play Again: the next game starts once both asked. True when it did
    // (here or on a status from the partner) - poll with started_next().
    void want_again(uint32_t now);
    bool started_next();                     // cleared by reading
    void leave(uint32_t now);                // end the session (the partner is told)
    void away(uint32_t now);                 // this board closes the game for now

private:
    void send_status(uint32_t now, uint8_t extra_flags = 0);
    void next_game();
    uint32_t hash_at(int ply, bool* ok) const;
    void stop(End why);

    Air      air_;
    Mac      me_, peer_;
    char     peer_name_[kNameMax + 1] = "";
    uint32_t session_ = 0;
    bool     inviter_ = false;
    uint16_t game_no_ = 0;
    uint16_t ply_ = 0;
    uint32_t hash_ = 0;
    uint16_t recent_[kRecent] = {};          // move at ply - k (k = 0 newest)
    uint32_t hashes_[kRecent] = {};          // hash after that move
    bool     again_ = false;
    End      ended_ = End::None;
    // the partner
    bool     heard_ = false;
    uint32_t heard_ms_ = 0;
    bool     peer_away_ = false;
    bool     peer_again_ = false;
    uint16_t peer_game_ = 0, peer_ply_ = 0;
    uint16_t peer_recent_[kRecent] = {};
    int      peer_n_ = 0;                    // moves in peer_recent_
    uint32_t peer_hash_check_ = 0;           // the partner's hash at peer_ply_
    bool     started_next_ = false;
    uint32_t next_ms_ = 0;
    int      leave_sends_ = 0;               // "left" is repeated a few times
};

uint32_t hash_move(uint32_t h, int move);
constexpr uint32_t kHashStart = 2166136261u;

} // namespace net
