// CYD-to-CYD play: the messages and the two state machines behind them.
// Plain C++ (no Arduino, no LVGL), host-tested in tools/host_tests/test_net.cpp
// with a fake radio that loses, repeats and reorders packets.
//
// The air: ESP-NOW broadcasts on one WiFi channel (src/hal/radio.*). Every
// packet goes to everyone in range; the ones meant for one board carry its
// address. Nothing is acknowledged: each board keeps sending its whole
// state twice a second (a beacon, and a status while it has a game going),
// and at once when it changes, so a lost packet is just replaced by the next.
//
// Presence: a board with the radio on beacons its player name, firmware,
// whether it is available to play, the games it is willing to play and
// whether it is busy in a game. Offers ride on the beacons too: "play Chess
// with me" for one board, and the answer "no" with a reason. "Yes" is the
// answering board starting the game: its first status reaches the asker,
// which starts too. An offer nobody answers runs out after 30 s.
//
// Link (a game in progress): both boards send a status - which session,
// which game of the session, how many moves were played, the last few moves
// and a hash of all of them. A board that is behind plays the moves it
// missed (after checking each against its own rules); a hash that doesn't
// match means the boards disagree, and the game ends unrecorded. Nothing
// heard for 3 s = the link is down and the game waits. Play Again starts
// the next game once both boards asked for it; whoever moved second moves
// first. Forfeit ends the session (a loss for that player, a win for the
// other); Done (no rematch) ends it after a game is over.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

constexpr uint8_t  kProto = 2;              // packet format; a change makes boards incompatible
constexpr size_t   kNameMax = 12;           // characters in a player name
constexpr size_t   kFwMax = 15;             // characters in a firmware version
constexpr size_t   kPacketMax = 96;         // biggest packet we send or take
constexpr int      kRecent = 12;            // moves repeated in every status
constexpr int      kMaxGames = 16;          // games a board can offer (bits of a mask)
constexpr uint32_t kSendMs = 500;           // status / beacon repeat
constexpr uint32_t kLostMs = 3000;          // nothing heard this long = link down
constexpr uint32_t kForgetMs = 6000;        // a board not heard this long leaves the list
constexpr uint32_t kOfferMs = 30000;        // an offer nobody answers runs out
constexpr uint32_t kLingerMs = 4000;        // a board that ended a session keeps saying so

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
// The packet's kind (1 beacon, 2 status), or 0 if it isn't ours / not this
// protocol. *other_proto is set when it is ours but another protocol version.
int  packet_kind(const uint8_t* d, size_t n, bool* other_proto = nullptr);
// The session a status packet is about (0 if it isn't one)
uint32_t status_session(const uint8_t* d, size_t n);

// Why an offer was turned down
enum class Reason : uint8_t {
    NotNow = 0,        // the player said "Not Now"
    OtherGame = 1,     // the player would rather play another game
    Busy = 2,          // in a game already
    GameOff = 3,       // that game is off on this board (or it isn't available)
};

// ---- Presence -------------------------------------------------------------------------------
struct Nearby {
    Mac      mac;
    char     name[kNameMax + 1] = "";
    char     fw[kFwMax + 1] = "";
    bool     proto_ok = true;               // same packet format
    bool     available = false;             // wants to be asked
    bool     busy = false;                  // in a game with someone
    bool     paused = false;                // ... that is paused (its game screen is closed)
    int      busy_game = -1;                // which game
    uint16_t games = 0;                     // bit g = willing to play game g
    uint32_t seen_ms = 0;
    uint16_t seq = 0;                       // its last beacon's counter
};

class Presence {
public:
    static constexpr int kMaxNearby = 8;
    enum class Event { None, Started, Declined, NoAnswer, Gone };

    void begin(const Mac& me, const char* fw, const Air& air, uint32_t now);
    // What this board tells the others. busy_game = -1: not in a game.
    void set_profile(const char* name, bool available, uint16_t games, int busy_game, bool paused,
                     uint32_t now);
    void tick(uint32_t now);                 // beacon, forget boards gone quiet, offers run out
    void receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    // Boards heard lately (available or busy ones; others aren't listed)
    int  count() const { return n_; }
    const Nearby& at(int i) const { return near_[i]; }
    const Nearby* find(const Mac& m) const;
    bool same_version(int i) const;
    bool can_offer(int i, int game) const;   // available, free, same version, plays it

    // Asking someone to play a game (one offer at a time)
    void offer(int i, int game, uint32_t session, uint32_t now);
    void cancel(uint32_t now);
    bool offering() const { return asking_; }
    const char* offer_name() const { return ask_name_; }
    int  offer_game() const { return ask_game_; }

    // Being asked: the newest offer for this board while its sender still makes it
    bool asked() const { return asked_; }
    const char* asker_name() const { return asked_name_; }
    int  asked_game() const { return asked_game_; }
    void accept(uint32_t now);               // Event::Started at once
    void decline(Reason why, uint32_t now);

    // What happened (cleared by reading). Declined: reason(). Started:
    // partner, session, the game and whether this board asked (the asker
    // moves first in the first game).
    Event poll();
    Reason reason() const { return reason_; }
    const Mac& partner() const { return partner_; }
    const char* partner_name() const { return partner_name_; }
    uint32_t session() const { return session_; }
    int  game() const { return game_; }
    bool inviter() const { return inviter_; }

private:
    void beacon(uint32_t now);
    Nearby* lookup(const Mac& m);

    Air      air_;
    Mac      me_;
    char     name_[kNameMax + 1] = "";
    char     fw_[kFwMax + 1] = "";
    bool     available_ = false, paused_ = false;
    uint16_t games_ = 0;
    int      busy_game_ = -1;
    Nearby   near_[kMaxNearby];
    int      n_ = 0;
    uint32_t next_ms_ = 0;
    uint16_t seq_ = 0;
    // asking
    bool     asking_ = false;
    Mac      ask_to_;
    char     ask_name_[kNameMax + 1] = "";
    uint32_t ask_session_ = 0, ask_since_ = 0;
    int      ask_game_ = -1;
    // being asked
    bool     asked_ = false;
    Mac      asked_by_;
    char     asked_name_[kNameMax + 1] = "";
    uint32_t asked_session_ = 0, asked_seen_ms_ = 0;
    int      asked_game_ = -1;
    // a "no" repeated on the beacons for a while
    Mac      no_to_;
    uint32_t no_session_ = 0, no_until_ms_ = 0;
    Reason   no_reason_ = Reason::NotNow;
    // outcome
    Event    event_ = Event::None;
    Reason   reason_ = Reason::NotNow;
    Mac      partner_;
    char     partner_name_[kNameMax + 1] = "";
    uint32_t session_ = 0;
    int      game_ = -1;
    bool     inviter_ = false;
};

// ---- Link -----------------------------------------------------------------------------------
class Link {
public:
    // YouLeft: this board dropped the session (Clear 2P Sessions): nothing is
// recorded on either board; the other board is told "gone"
    enum class End : uint8_t { None, YouForfeited, PeerForfeited, YouDone, PeerDone, PeerGone, OutOfStep, YouLeft };

    // Saved with the session, so a game can carry on later
    static constexpr size_t kSaveBytes = 4 + 6 + 6 + (kNameMax + 1) + 4 + 1 + 2 + 2 + 4 + 2 * kRecent + 4 * kRecent;

    // A new session: its first game starts with no moves
    void begin(const Mac& me, const Mac& peer, const char* peer_name, uint32_t session, bool inviter,
               const Air& air, uint32_t now);
    size_t save(uint8_t* buf, size_t cap) const;
    bool   load(const uint8_t* buf, size_t len, const Air& air, uint32_t now);

    void tick(uint32_t now);                 // status every kSendMs (while not ended)
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
    bool peer_away() const { return peer_away_; }   // the partner's game screen is closed
    bool peer_again() const { return peer_again_; }
    bool again() const { return again_; }
    void set_away(bool away, uint32_t now);  // this board's game screen closed / open again

    bool ended() const { return ended_ != End::None; }
    End  end_reason() const { return ended_; }
    // The session ended here and has said so long enough (radio may go)
    bool linger_over(uint32_t now) const;

    // Play Again: the next game starts once both asked. Poll started_next().
    void want_again(uint32_t now);
    bool started_next();                     // cleared by reading
    void forfeit(uint32_t now);              // a loss here, a win there
    void done(uint32_t now);                 // no more games (after one is over)
    void leave(uint32_t now);                // drop it, unrecorded ("gone" to the partner)
    void say_end(uint32_t now) { if (ended()) send_status(now); }   // repeat the ending now

private:
    void send_status(uint32_t now, uint8_t extra_flags = 0);
    void next_game();
    uint32_t hash_at(int ply, bool* ok) const;
    void stop(End why, uint32_t now);
    uint8_t reply_flag() const;              // what an ended session answers

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
    bool     away_ = false;
    End      ended_ = End::None;
    uint32_t ended_ms_ = 0;
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
    uint16_t seq_ = 0, peer_seq_ = 0;
    bool     have_seq_ = false;
};

uint32_t hash_move(uint32_t h, int move);
constexpr uint32_t kHashStart = 2166136261u;

} // namespace net
