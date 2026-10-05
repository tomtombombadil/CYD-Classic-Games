// CYD-to-CYD play: the messages and the two state machines behind them.
// Plain C++ (no Arduino, no LVGL), host-tested in tools/host_tests/test_net.cpp
// with a fake radio that loses, repeats and reorders packets.
//
// HARD RULE (Tom, CLAUDE.md): nothing a player writes or says ever goes on
// the air. Every field below is a number from a fixed set: names are two
// word numbers (src/net/names.*), games are fixed game keys
// (src/games/common/net_games.h), moves are move keys, answers are codes.
//
// The air: ESP-NOW broadcasts on one WiFi channel (src/hal/radio.*). Every
// packet goes to everyone in range; the ones meant for one board carry its
// address. Nothing is acknowledged: a board repeats what it has to say
// twice a second until it changes, so a lost packet is replaced by the next.
//
// Finding players (Tom, 2026-10-04): a board in 2P listens and stays quiet.
// Only a board whose player is looking sends a Call ("who's there?"), and
// boards in 2P answer with a Hello: name, firmware, link version, whether
// they're in a game, and the games they'll play with each game's version.
// THE CALL AND THE HELLO KEEP THEIR LAYOUT FOREVER (later versions may only
// add fields at the end), so any later board can still list an older one
// and tell its player who needs an update.
//
// Requests: "play Chess with me" goes to one board, repeated until it is
// answered: Play, No Thanks, Other Game (the asked player then sends a
// request of their own), or by the board itself: Busy (in a game, or
// already asked by someone - the first asker has priority) or Game Off
// (that game isn't on there, or its version differs). While the question
// is up the asked board says "Ringing", so the asker can tell an
// unanswered request (30 s) from a board that went out of range.
//
// Link (a session): both boards send a status - which session, which game
// of the session, how many moves were played, the last few moves (move
// keys) and a hash of all of them. A board that is behind plays the moves
// it missed (each checked by its own rules); a hash that doesn't match
// voids that game (not counted) and both boards may start the next.
// Nothing heard for 3 s = the link is down: the game waits. A session can
// be put away ("suspended": the boards lost touch for good, or one
// restarted) and continued once both players say so when they meet again.
// Play Again starts the next game once both boards asked for it; the first
// mover alternates (the asked player moves first in the first game).
// Forfeit ends the session (a loss for that player, a win for the other);
// Goodbye ends it after a game is over; Close ends it not counted.
#pragma once

#include <cstddef>
#include <cstdint>

namespace net {

// Link version: finding, requests and sessions. Boards play together only
// with the same link version (and the same version of that game).
constexpr uint8_t  kLink = 3;
constexpr size_t   kPacketMax = 128;        // biggest packet we send or take
constexpr int      kRecent = 12;            // moves repeated in every status
constexpr int      kMaxGames = 16;          // wireless games a board can list
constexpr uint32_t kSendMs = 500;           // repeat of a request, answer or status
constexpr uint32_t kSuspendedSendMs = 2000; // a put-away session's status
constexpr uint32_t kLostMs = 3000;          // nothing heard this long = link down
constexpr uint32_t kForgetMs = 6000;        // a board not heard this long leaves the list
constexpr uint32_t kRequestMs = 30000;      // a request nobody answers runs out
constexpr uint32_t kLingerMs = 4000;        // an ending (or an answer) keeps being said

struct Mac {
    uint8_t b[6] = {};
    bool operator==(const Mac& o) const;
    bool operator!=(const Mac& o) const { return !(*this == o); }
    bool operator<(const Mac& o) const;
    bool zero() const;
};

// How the state machines reach the air: broadcast one packet
struct Air {
    void (*send)(const uint8_t* data, size_t len, void* ctx) = nullptr;
    void* ctx = nullptr;
};

struct Version {
    uint8_t major = 0, minor = 0, patch = 0;
    int  cmp(const Version& o) const;        // <0 older, 0 same, >0 newer
    static Version parse(const char* s);     // "v0.21.0" / "0.21.0" ("dev" etc. = 0.0.0)
};

// A wireless game a board will play: its key and version
struct GameOffer {
    uint8_t key = 0, version = 0;
};

// The packet's kind (Call, Hello, Request, Answer, Status - see wireless.cpp),
// or 0 if it isn't a CYD Classic Games packet of this link version.
// *link (optional) = the sender's link version for any of our packets.
int  packet_kind(const uint8_t* d, size_t n, int* link = nullptr);
// The session a status packet is about (0 if it isn't one)
uint32_t status_session(const uint8_t* d, size_t n);
// A bare status that only ends a session (flags = Link flags): the answer
// for a session this board no longer plays (wplay keeps a few endings)
void send_end(const Air& air, const Mac& to, uint32_t session, uint16_t flags);

// Answers to a request
enum class Answer : uint8_t {
    None = 0,
    Play = 1,          // yes: the asked board starts the session
    NoThanks = 2,      // the player said No Thanks
    OtherGame = 3,     // the player will ask for another game
    Busy = 4,          // in a game, or someone else asked first
    GameOff = 5,       // that game isn't on there (or its version differs)
    Cancel = 6,        // the asker stopped asking
    Ringing = 7,       // the question is up on the asked board
};

// ---- Presence: finding players and agreeing a game -------------------------------------------
struct Nearby {
    Mac       mac;
    uint16_t  name_a = 0, name_b = 0;       // names::format
    int       link = kLink;                 // its link version
    Version   fw;
    bool      available = false;            // in 2P
    bool      busy = false;                 // in a game with someone
    int       n_games = 0;
    GameOffer games[kMaxGames];
    uint32_t  seen_ms = 0;
    uint16_t  seq = 0;
    int  version_of(int key) const;          // that game's version there, -1 = not offered
};

// What this board tells others
struct Profile {
    uint16_t  name_a = 0, name_b = 0;
    Version   fw;
    bool      available = false;            // 2P: answers calls and requests
    bool      busy = false;                 // in a game that is going
    int       n_games = 0;                  // the games it will play
    GameOffer games[kMaxGames];
    uint16_t  move_timer = 30;              // seconds, 0 = Off
    int  version_of(int key) const;
};

class Presence {
public:
    static constexpr int kMaxNearby = 8;
    enum class Event { None, Started, Answered, NoAnswer, Gone, Cancelled, AskerGone };

    void begin(const Mac& me, const Air& air, uint32_t now);
    void set_profile(const Profile& p, uint32_t now);
    void tick(uint32_t now);                 // calls, answers, forget boards gone quiet, time-outs
    void receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    // Looking for players: Calls go out while on; the list fills from the Hellos
    void search(bool on, uint32_t now);
    bool searching() const { return searching_; }
    int  count() const { return n_; }
    const Nearby& at(int i) const { return near_[i]; }
    const Nearby* find(const Mac& m) const;
    // Both boards will play game `key` and have the same version of it
    bool playable(const Nearby& b, int key) const;

    // Asking someone to play (one request at a time)
    void request(const Mac& to, uint16_t name_a, uint16_t name_b, int key, uint32_t session, uint32_t now);
    void cancel(uint32_t now);               // stop asking (the other board is told)
    bool requesting() const { return req_on_; }
    bool ringing() const { return req_ringing_; }   // the question is up there
    const Mac& request_to() const { return req_to_; }
    int  request_key() const { return req_key_; }
    uint16_t request_name_a() const { return req_name_a_; }
    uint16_t request_name_b() const { return req_name_b_; }

    // Being asked: one request at a time (the first asker has priority)
    bool asked() const { return in_on_; }
    const Mac& asker() const { return in_from_; }
    uint16_t asker_name_a() const { return in_name_a_; }
    uint16_t asker_name_b() const { return in_name_b_; }
    int  asked_key() const { return in_key_; }
    uint16_t asker_timer() const { return in_timer_; }
    int  asker_games(GameOffer* out, int cap) const;   // what the asker will play (for Other Game)
    void accept(uint32_t now);               // Event::Started at once
    void decline(Answer why, uint32_t now);  // NoThanks or OtherGame

    // What happened (cleared by reading). Answered: answer(). Started:
    // partner, its name, session, the game, whether this board asked, and
    // the move timer both boards agreed (the shorter; 0 = Off).
    Event poll();
    Answer answer() const { return answer_; }
    const Mac& partner() const { return partner_; }
    uint16_t partner_name_a() const { return partner_a_; }
    uint16_t partner_name_b() const { return partner_b_; }
    uint32_t session() const { return session_; }
    int  game_key() const { return game_key_; }
    bool inviter() const { return inviter_; }
    uint16_t timer() const { return timer_; }
    uint16_t partner_timer() const { return partner_timer_; }

private:
    void send_call(uint32_t now);
    void send_hello(const Mac& to);
    void send_request(uint32_t now);
    void send_answer(const Mac& to, uint32_t session, Answer a, uint32_t now);
    void started(const Mac& who, uint16_t a, uint16_t b, uint32_t session, int key, bool inviter,
                 uint16_t their_timer);
    Nearby* lookup(const Mac& m);
    void hear_request(const Mac& from, const uint8_t* d, size_t n, uint32_t now);
    void hear_answer(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    Air      air_;
    Mac      me_;
    Profile  me_p_;
    uint16_t seq_ = 0;
    // searching
    bool     searching_ = false;
    uint32_t call_ms_ = 0;
    Nearby   near_[kMaxNearby];
    int      n_ = 0;
    // answering calls
    Mac      hello_to_;
    bool     hello_due_ = false;
    uint32_t hello_ms_ = 0;
    // asking
    bool     req_on_ = false, req_ringing_ = false;
    Mac      req_to_;
    uint16_t req_name_a_ = 0, req_name_b_ = 0;
    int      req_key_ = -1;
    uint32_t req_session_ = 0, req_since_ = 0, req_heard_ms_ = 0, req_ms_ = 0;
    // being asked
    bool     in_on_ = false;
    Mac      in_from_;
    uint16_t in_name_a_ = 0, in_name_b_ = 0, in_timer_ = 0;
    int      in_key_ = -1;
    uint32_t in_session_ = 0, in_heard_ms_ = 0, in_ms_ = 0;
    int      in_n_games_ = 0;
    GameOffer in_games_[kMaxGames];
    // an answer repeated for a while (to the asker; Play repeats too)
    Mac      ans_to_;
    uint32_t ans_session_ = 0, ans_until_ms_ = 0, ans_ms_ = 0;
    Answer   ans_ = Answer::None;
    // outcome
    Event    event_ = Event::None;
    Answer   answer_ = Answer::None;
    Mac      partner_;
    uint16_t partner_a_ = 0, partner_b_ = 0;
    uint32_t session_ = 0;
    int      game_key_ = -1;
    bool     inviter_ = false;
    uint16_t timer_ = 0, partner_timer_ = 0;
};

// ---- Link: a session ------------------------------------------------------------------------
class Link {
public:
    // YouLeft: dropped here (Clear 2P Sessions with nobody in range): not
    // recorded; the other board is told "gone". Closed / PeerClosed: closed
    // after the boards lost touch - "Communications failed. Game not counted."
    enum class End : uint8_t {
        None, YouForfeited, PeerForfeited, YouDone, PeerDone, PeerGone, YouLeft, Closed, PeerClosed,
    };

    // Saved with the session, so a game can carry on later (even after a restart)
    static constexpr size_t kSaveBytes = 4 + 6 + 6 + 2 + 2 + 4 + 1 + 1 + 2 + 2 + 2 + 4 + 4 * kRecent + 4 * kRecent + 1;

    // A new session: its first game starts with no moves. inviter = this
    // board asked (the asked player moves first in the first game).
    void begin(const Mac& me, const Mac& peer, uint16_t peer_a, uint16_t peer_b, uint32_t session,
               bool inviter, int game_key, uint16_t timer, const Air& air, uint32_t now);
    size_t save(uint8_t* buf, size_t cap) const;
    bool   load(const uint8_t* buf, size_t len, const Air& air, uint32_t now);

    void tick(uint32_t now);                 // status every kSendMs (while not ended)
    void receive(const Mac& from, const uint8_t* d, size_t n, uint32_t now);

    // Turns: the side this board plays in the current game (0 moves first)
    int  my_side() const;
    int  game_no() const { return game_no_; }  // 0 = the first game of the session
    int  ply() const { return ply_; }
    uint16_t peer_name_a() const { return peer_a_; }
    uint16_t peer_name_b() const { return peer_b_; }
    const Mac& peer() const { return peer_; }
    uint32_t session() const { return session_; }
    int  game_key() const { return game_key_; }
    uint16_t timer() const { return timer_; }   // the agreed move timer (s), 0 = Off

    // A move made on this board (yours, or the partner's from next_move())
    void played(uint32_t move, uint32_t now);
    // The partner's next move this board hasn't played yet (true + *move).
    // The game checks it is legal before playing it (then calls played());
    // if not, call disagree().
    bool next_move(uint32_t* move) const;
    void disagree(uint32_t now);             // this game is void (not counted) on both boards
    bool voided() const { return void_; }    // the current game was voided

    bool up(uint32_t now) const;             // heard from the partner lately
    bool heard() const { return heard_; }    // heard at all since this board took up the session
    uint32_t heard_ms() const { return heard_ms_; }
    bool peer_again() const { return peer_again_; }
    bool again() const { return again_; }

    bool ended() const { return ended_ != End::None; }
    End  end_reason() const { return ended_; }
    bool by_time() const { return by_time_; }  // the forfeit was the move timer running out
    // The session ended here and has said so long enough (radio may go)
    bool linger_over(uint32_t now) const;
    uint16_t end_flags() const;              // what this board answers for the session once it ended

    // Play Again: the next game starts once both asked. Poll started_next().
    void want_again(uint32_t now);
    bool started_next();                     // cleared by reading
    void forfeit(uint32_t now, bool by_time = false);   // a loss here, a win there
    void done(uint32_t now);                 // Goodbye (after a game is over)
    void leave(uint32_t now);                // drop it, unrecorded ("gone" to the partner)
    void close(uint32_t now);                // the boards lost touch: not counted on either

    // Put away (the boards lost touch for good, or after a restart) and
    // meeting again: both players are asked "Continue?" (meet()); the game
    // goes on once both said yes (resumed()), else it is closed.
    void suspend(uint32_t now);
    bool suspended() const { return suspended_; }
    bool peer_suspended() const { return peer_suspended_; }
    bool meet() const { return meet_; }      // the question is due: both boards heard each other again
    void agree_continue(uint32_t now);
    bool continue_said() const { return cont_; }
    bool resumed();                          // cleared by reading

private:
    void send_status(uint32_t now, uint16_t extra_flags = 0);
    void next_game();
    uint32_t hash_at(int ply, bool* ok) const;
    void stop(End why, uint32_t now);
    void resume(uint32_t now);

    Air      air_;
    Mac      me_, peer_;
    uint16_t peer_a_ = 0, peer_b_ = 0;
    uint32_t session_ = 0;
    bool     inviter_ = false;
    int      game_key_ = 0;
    uint16_t timer_ = 0;
    uint16_t game_no_ = 0;
    uint16_t ply_ = 0;
    uint32_t hash_ = 0;
    uint32_t recent_[kRecent] = {};          // move at ply - k (k = 0 newest)
    uint32_t hashes_[kRecent] = {};          // hash after that move
    bool     again_ = false;
    bool     void_ = false;
    End      ended_ = End::None;
    bool     by_time_ = false;
    uint32_t ended_ms_ = 0;
    bool     suspended_ = false, cont_ = false, meet_ = false, resumed_ = false;
    uint32_t cont_until_ms_ = 0;             // "continue" keeps being said after resuming
    // the partner
    bool     heard_ = false;
    uint32_t heard_ms_ = 0;
    bool     peer_again_ = false, peer_suspended_ = false, peer_cont_ = false;
    uint16_t peer_game_ = 0, peer_ply_ = 0;
    uint32_t peer_recent_[kRecent] = {};
    int      peer_n_ = 0;                    // moves in peer_recent_
    uint32_t peer_hash_check_ = 0;           // the partner's hash at peer_ply_
    bool     started_next_ = false;
    uint32_t next_ms_ = 0;
    uint16_t seq_ = 0, peer_seq_ = 0;
    bool     have_seq_ = false;
};

// Link status flags (on the air)
enum : uint16_t {
    kFlagAgain = 1, kFlagForfeit = 2, kFlagDone = 4, kFlagGone = 8, kFlagVoid = 16,
    kFlagSuspended = 32, kFlagContinue = 64, kFlagByTime = 128, kFlagClosed = 256,
};
constexpr uint16_t kFlagEnds = kFlagForfeit | kFlagDone | kFlagGone | kFlagClosed;

uint32_t hash_move(uint32_t h, uint32_t move);
constexpr uint32_t kHashStart = 2166136261u;

} // namespace net
