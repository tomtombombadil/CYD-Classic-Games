#include "chess_core.h"

#include <cstring>
#include <new>

namespace chess {

namespace {

inline bool on_board(int r, int f) { return r >= 0 && r < 8 && f >= 0 && f < 8; }
inline uint8_t make_cell(int piece, int side) { return static_cast<uint8_t>(piece | (side << 3)); }

const int kKnight[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
const int kKingD[8][2]  = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
const int kDiag[4][2]   = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
const int kLine[4][2]   = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

// ---- Position keys (for repetition), computed instead of stored in an
// 8 KB table: a splitmix64 hash of what is where. Only Game::play uses them.
uint64_t mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
uint64_t z_piece(int cell, int square) { return mix(uint64_t(cell) * 64 + square); }
uint64_t z_extra(int k)                { return mix(0x10000 + k); }

// Pseudo-legal moves (own king may be left in check). captures_only also
// keeps promotions.
void pseudo(const Position& p, MoveList& out, bool captures_only, int only_from = -1)
{
    out.n = 0;
    const int s = p.side;
    auto add = [&](int from, int to, int promo) {
        if (out.n < kMaxMoves) out.m[out.n++] = Move{static_cast<uint8_t>(from), static_cast<uint8_t>(to), static_cast<uint8_t>(promo)};
    };
    for (int from = 0; from < 64; ++from) {
        if (only_from >= 0 && from != only_from) continue;
        const uint8_t c = p.sq[from];
        if (!c || side_of(c) != s) continue;
        const int r = from / 8, f = from % 8, pc = piece_of(c);
        if (pc == Pawn) {
            const int dir = s == 0 ? 1 : -1, last = s == 0 ? 7 : 0, home = s == 0 ? 1 : 6;
            auto pawn_to = [&](int to) {
                if (to / 8 == last) { add(from, to, Queen); add(from, to, Rook); add(from, to, Bishop); add(from, to, Knight); }
                else add(from, to, None);
            };
            const int one = from + 8 * dir;
            if (on_board(r + dir, f) && !p.sq[one]) {
                if (!captures_only || one / 8 == last) pawn_to(one);
                const int two = one + 8 * dir;
                if (!captures_only && r == home && !p.sq[two]) add(from, two, None);
            }
            for (int df = -1; df <= 1; df += 2) {
                if (!on_board(r + dir, f + df)) continue;
                const int to = (r + dir) * 8 + f + df;
                if ((p.sq[to] && side_of(p.sq[to]) != s) || to == p.ep) pawn_to(to);
            }
            continue;
        }
        auto step = [&](int to) {
            const uint8_t t = p.sq[to];
            if (t && side_of(t) == s) return;
            if (captures_only && !t) return;
            add(from, to, None);
        };
        if (pc == Knight || pc == King) {
            const int (*d)[2] = pc == Knight ? kKnight : kKingD;
            for (int k = 0; k < 8; ++k)
                if (on_board(r + d[k][0], f + d[k][1])) step((r + d[k][0]) * 8 + f + d[k][1]);
            if (pc == King && !captures_only) {
                // Castling: rights, empty squares between, not out of / through check
                const int back = s == 0 ? 0 : 56;
                if (from == back + 4 && !p.attacked(from, s ^ 1)) {
                    const uint8_t rook = make_cell(Rook, s);
                    if ((p.castle & (s == 0 ? 1 : 4)) && p.sq[back + 7] == rook && !p.sq[back + 5] && !p.sq[back + 6]
                        && !p.attacked(back + 5, s ^ 1) && !p.attacked(back + 6, s ^ 1))
                        add(from, back + 6, None);
                    if ((p.castle & (s == 0 ? 2 : 8)) && p.sq[back] == rook && !p.sq[back + 1] && !p.sq[back + 2]
                        && !p.sq[back + 3] && !p.attacked(back + 3, s ^ 1) && !p.attacked(back + 2, s ^ 1))
                        add(from, back + 2, None);
                }
            }
            continue;
        }
        const bool diag = pc == Bishop || pc == Queen, line = pc == Rook || pc == Queen;
        for (int pass = 0; pass < 2; ++pass) {
            if ((pass == 0 && !diag) || (pass == 1 && !line)) continue;
            const int (*d)[2] = pass == 0 ? kDiag : kLine;
            for (int k = 0; k < 4; ++k) {
                int rr = r + d[k][0], ff = f + d[k][1];
                while (on_board(rr, ff)) {
                    const int to = rr * 8 + ff;
                    step(to);
                    if (p.sq[to]) break;
                    rr += d[k][0]; ff += d[k][1];
                }
            }
        }
    }
}

bool legal_after(const Position& p, const Move& m, int s)
{
    Position q = p;
    q.make(m);
    return !q.attacked(q.king[s], s ^ 1);
}

void keep_legal(const Position& p, MoveList& l, int s)
{
    int n = 0;
    for (int k = 0; k < l.n; ++k) if (legal_after(p, l.m[k], s)) l.m[n++] = l.m[k];
    l.n = n;
}

} // namespace

// ---- Position --------------------------------------------------------------------------
void Position::start()
{
    *this = Position{};
    const uint8_t back[8] = {Rook, Knight, Bishop, Queen, King, Bishop, Knight, Rook};
    for (int f = 0; f < 8; ++f) {
        sq[f] = make_cell(back[f], 0);
        sq[8 + f] = make_cell(Pawn, 0);
        sq[48 + f] = make_cell(Pawn, 1);
        sq[56 + f] = make_cell(back[f], 1);
    }
    castle = 15;
    king[0] = 4;
    king[1] = 60;
    hash = compute_hash();
}

uint64_t Position::compute_hash() const
{
    uint64_t h = 0;
    for (int s = 0; s < 64; ++s) if (sq[s]) h ^= z_piece(sq[s], s);
    if (side) h ^= z_extra(0);
    h ^= z_extra(1 + (castle & 15));
    if (ep >= 0) h ^= z_extra(20 + ep % 8);
    return h;
}

bool Position::attacked(int square, int by) const
{
    const int r = square / 8, f = square % 8;
    // Pawns: a pawn of `by` attacks diagonally forward
    const int pr = r - (by == 0 ? 1 : -1);
    for (int df = -1; df <= 1; df += 2)
        if (on_board(pr, f + df) && sq[pr * 8 + f + df] == make_cell(Pawn, by)) return true;
    for (const auto& d : kKnight)
        if (on_board(r + d[0], f + d[1]) && sq[(r + d[0]) * 8 + f + d[1]] == make_cell(Knight, by)) return true;
    for (const auto& d : kKingD)
        if (on_board(r + d[0], f + d[1]) && sq[(r + d[0]) * 8 + f + d[1]] == make_cell(King, by)) return true;
    for (int pass = 0; pass < 2; ++pass) {
        const int (*dirs)[2] = pass == 0 ? kDiag : kLine;
        const uint8_t a = make_cell(pass == 0 ? Bishop : Rook, by), q = make_cell(Queen, by);
        for (int k = 0; k < 4; ++k) {
            int rr = r + dirs[k][0], ff = f + dirs[k][1];
            while (on_board(rr, ff)) {
                const uint8_t c = sq[rr * 8 + ff];
                if (c) { if (c == a || c == q) return true; break; }
                rr += dirs[k][0]; ff += dirs[k][1];
            }
        }
    }
    return false;
}

void Position::make(const Move& m)
{
    const uint8_t c = sq[m.from];
    const int s = side_of(c), pc = piece_of(c);
    const bool capture = sq[m.to] != 0;
    // En passant: the captured pawn is beside the target square
    if (pc == Pawn && m.to == ep && !sq[m.to]) sq[m.to + (s == 0 ? -8 : 8)] = 0;
    sq[m.to] = m.promo ? make_cell(m.promo, s) : c;
    sq[m.from] = 0;
    if (pc == King) {
        king[s] = m.to;
        if (m.to == m.from + 2) { sq[m.from + 1] = sq[m.from + 3]; sq[m.from + 3] = 0; }   // O-O
        if (m.to + 2 == m.from) { sq[m.from - 1] = sq[m.from - 4]; sq[m.from - 4] = 0; }   // O-O-O
    }
    // Rights go when a king or rook moves or a rook is taken
    auto touch = [this](int square) {
        if (square == 4)  castle &= ~3;
        if (square == 7)  castle &= ~1;
        if (square == 0)  castle &= ~2;
        if (square == 60) castle &= ~12;
        if (square == 63) castle &= ~4;
        if (square == 56) castle &= ~8;
    };
    touch(m.from);
    touch(m.to);
    ep = (pc == Pawn && (m.to - m.from == 16 || m.from - m.to == 16)) ? static_cast<int8_t>((m.from + m.to) / 2) : -1;
    halfmove = (pc == Pawn || capture) ? 0 : static_cast<uint8_t>(halfmove < 255 ? halfmove + 1 : 255);
    side ^= 1;
}

void generate(const Position& p, MoveList& out)
{
    pseudo(p, out, false);
    keep_legal(p, out, p.side);
}

void moves_from(const Position& p, int from, MoveList& out)
{
    out.n = 0;
    if (!p.sq[from]) return;
    Position q = p;
    const int s = side_of(p.sq[from]);
    if (s != q.side) { q.side = static_cast<uint8_t>(s); q.ep = -1; }
    pseudo(q, out, false, from);
    keep_legal(q, out, s);
}

// ---- Game ------------------------------------------------------------------------------
bool Game::play(int index)
{
    if (result() != -1) return false;
    MoveList l;
    legal(l);
    if (index < 0 || index >= l.n) return false;
    pos.make(l.m[index]);
    pos.hash = pos.compute_hash();
    last = l.m[index];
    has_last = true;
    history[plies++] = static_cast<uint8_t>(index);
    hashes[plies] = pos.hash;
    return true;
}

End Game::end() const
{
    MoveList l;
    generate(pos, l);
    if (l.n == 0) return pos.in_check(pos.side) ? End::Checkmate : End::Stalemate;
    if (pos.halfmove >= 100) return End::FiftyMoves;
    // The same position (same side to move) three times
    int same = 0;
    for (int k = plies; k >= 0 && k >= plies - pos.halfmove; k -= 2) same += hashes[k] == pos.hash;
    if (same >= 3) return End::Repetition;
    // Only kings, or kings and a single knight or bishop
    int minors = 0;
    bool heavy = false;
    for (int s = 0; s < 64; ++s) {
        const int pc = piece_of(pos.sq[s]);
        if (pc == Pawn || pc == Rook || pc == Queen) heavy = true;
        if (pc == Knight || pc == Bishop) ++minors;
    }
    if (!heavy && minors <= 1) return End::Material;
    if (plies >= kMaxPlies) return End::TooLong;
    return End::None;
}

int Game::result() const
{
    switch (end()) {
        case End::None:      return -1;
        case End::Checkmate: return pos.side ^ 1;
        default:             return 2;
    }
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    memcpy(buf, "CHS1", 4);
    buf[4] = plies & 0xFF;
    buf[5] = plies >> 8;
    memcpy(buf + 6, history, sizeof history);
    return kSaveBytes;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "CHS1", 4) != 0) return false;
    const int n = buf[4] | (buf[5] << 8);
    if (n > kMaxPlies) return false;
    Game* t = new (std::nothrow) Game();  // ~5 KB: not on the stack
    if (!t) return false;
    bool ok = true;
    for (int k = 0; k < n && ok; ++k) ok = t->play(buf[6 + k]);
    if (ok) *this = *t;
    delete t;
    return ok;
}

const char* end_text(End e)
{
    switch (e) {
        case End::None:       return "";
        case End::Checkmate:  return "Checkmate";
        case End::Stalemate:  return "Stalemate";
        case End::FiftyMoves: return "Draw: 50 moves";
        case End::Repetition: return "Draw: repetition";
        case End::Material:   return "Draw: no mating force";
        case End::TooLong:    return "Draw: game too long";
    }
    return "";
}

// ---- Computer ------------------------------------------------------------------------------
namespace {

constexpr int kValue[7] = {0, 100, 320, 330, 500, 900, 0};
constexpr int kMate = 100000, kInf = 1000000;

// Piece-square bonuses from White's side (index = rank * 8 + file); Black
// reads them mirrored. Simple shapes: pawns advance and hold the centre,
// knights and bishops centralise, rooks like the 7th rank and open files
// (approximated by the 7th rank only), the king hides early, centralises late.
const int8_t kPawnT[64] = {
     0,  0,  0,   0,   0,  0,  0,  0,
     5,  8,  8, -10, -10,  8,  8,  5,
     4, -2, -4,   2,   2, -4, -2,  4,
     2,  2,  6,  18,  18,  6,  2,  2,
     6,  6, 12,  22,  22, 12,  6,  6,
    14, 14, 20,  28,  28, 20, 14, 14,
    40, 40, 40,  40,  40, 40, 40, 40,
     0,  0,  0,   0,   0,  0,  0,  0,
};
const int8_t kKnightT[64] = {
    -40, -28, -20, -18, -18, -20, -28, -40,
    -26, -10,   0,   4,   4,   0, -10, -26,
    -18,   4,  10,  14,  14,  10,   4, -18,
    -16,   6,  14,  18,  18,  14,   6, -16,
    -16,   6,  14,  18,  18,  14,   6, -16,
    -18,   4,  10,  14,  14,  10,   4, -18,
    -26, -10,   0,   4,   4,   0, -10, -26,
    -40, -28, -20, -18, -18, -20, -28, -40,
};
const int8_t kBishopT[64] = {
    -14, -8, -10, -8, -8, -10, -8, -14,
     -6,  8,   2,  4,  4,   2,  8,  -6,
     -4,  6,   8,  8,  8,   8,  6,  -4,
     -4,  4,  10, 10, 10,  10,  4,  -4,
     -4,  4,  10, 10, 10,  10,  4,  -4,
     -4,  6,   8,  8,  8,   8,  6,  -4,
     -6,  2,   2,  2,  2,   2,  2,  -6,
    -14, -8,  -8, -8, -8,  -8, -8, -14,
};
const int8_t kRookT[64] = {
     0,  0,  4,  8,  8,  4,  0,  0,
    -4,  0,  0,  0,  0,  0,  0, -4,
    -4,  0,  0,  0,  0,  0,  0, -4,
    -4,  0,  0,  0,  0,  0,  0, -4,
    -4,  0,  0,  0,  0,  0,  0, -4,
    -4,  0,  0,  0,  0,  0,  0, -4,
    12, 16, 16, 16, 16, 16, 16, 12,
     0,  0,  0,  4,  4,  0,  0,  0,
};
const int8_t kQueenT[64] = {
    -12, -8, -6, -2, -2, -6, -8, -12,
     -8,  0,  2,  0,  0,  0,  0,  -8,
     -6,  2,  4,  4,  4,  4,  2,  -6,
     -2,  0,  4,  6,  6,  4,  0,  -2,
     -2,  0,  4,  6,  6,  4,  0,  -2,
     -6,  2,  4,  4,  4,  4,  2,  -6,
     -8,  0,  2,  2,  2,  2,  0,  -8,
    -12, -8, -6, -2, -2, -6, -8, -12,
};
const int8_t kKingMidT[64] = {
     18,  26,  10,  -4,   0,  8,  28,  18,
      8,   8,  -6, -12, -12, -6,   8,   8,
    -12, -18, -20, -24, -24, -20, -18, -12,
    -24, -28, -32, -36, -36, -32, -28, -24,
    -30, -34, -38, -42, -42, -38, -34, -30,
    -34, -38, -42, -46, -46, -42, -38, -34,
    -36, -40, -44, -48, -48, -44, -40, -36,
    -38, -42, -46, -50, -50, -46, -42, -38,
};
const int8_t kKingEndT[64] = {
    -40, -26, -18, -14, -14, -18, -26, -40,
    -26, -10,  -2,   2,   2,  -2, -10, -26,
    -18,  -2,  10,  14,  14,  10,  -2, -18,
    -14,   2,  14,  22,  22,  14,   2, -14,
    -14,   2,  14,  22,  22,  14,   2, -14,
    -18,  -2,  10,  14,  14,  10,  -2, -18,
    -26, -10,  -2,   2,   2,  -2, -10, -26,
    -40, -26, -18, -14, -14, -18, -26, -40,
};

int evaluate(const Position& p)
{
    int mid[2] = {0, 0}, phase = 0;
    int king_mid[2] = {0, 0}, king_end[2] = {0, 0};
    int bishops[2] = {0, 0};
    for (int s = 0; s < 64; ++s) {
        const uint8_t c = p.sq[s];
        if (!c) continue;
        const int pc = piece_of(c), side = side_of(c);
        const int i = side == 0 ? s : (7 - s / 8) * 8 + s % 8;   // mirror for Black
        int v = kValue[pc];
        switch (pc) {
            case Pawn:   v += kPawnT[i]; break;
            case Knight: v += kKnightT[i]; phase += 1; break;
            case Bishop: v += kBishopT[i]; phase += 1; ++bishops[side]; break;
            case Rook:   v += kRookT[i]; phase += 2; break;
            case Queen:  v += kQueenT[i]; phase += 4; break;
            case King:   king_mid[side] = kKingMidT[i]; king_end[side] = kKingEndT[i]; break;
        }
        mid[side] += v;
    }
    if (phase > 24) phase = 24;
    int score[2];
    for (int s = 0; s < 2; ++s)
        score[s] = mid[s] + (king_mid[s] * phase + king_end[s] * (24 - phase)) / 24
                 + (bishops[s] >= 2 ? 30 : 0);
    const int w = score[0] - score[1];
    return p.side == 0 ? w : -w;
}

struct Search {
    uint32_t (*clock)();
    uint32_t deadline;
    volatile bool* stop;
    bool     stopped = false;
    uint32_t nodes = 0;
};

bool out_of_time(Search& S)
{
    if (S.stopped) return true;
    if ((++S.nodes & 1023) == 0) {
        if ((S.stop && *S.stop) || (S.clock && S.deadline && int32_t(S.clock() - S.deadline) > 0))
            S.stopped = true;
    }
    return S.stopped;
}

// Captures first, most valuable victim / least valuable attacker; then
// promotions; then quiet moves.
void order(const Position& p, MoveList& l)
{
    int16_t key[kMaxMoves];
    for (int k = 0; k < l.n; ++k) {
        const Move& m = l.m[k];
        int s = 0;
        const int victim = piece_of(p.sq[m.to]);
        if (victim) s = 1000 + 10 * kValue[victim] / 10 - piece_of(p.sq[m.from]);
        else if (piece_of(p.sq[m.from]) == Pawn && m.to == p.ep) s = 1000 + 10;
        if (m.promo) s += 500 + kValue[m.promo] / 10;
        key[k] = static_cast<int16_t>(s);
    }
    for (int i = 1; i < l.n; ++i) {               // insertion sort, stable
        const Move m = l.m[i];
        const int16_t kk = key[i];
        int j = i - 1;
        while (j >= 0 && key[j] < kk) { l.m[j + 1] = l.m[j]; key[j + 1] = key[j]; --j; }
        l.m[j + 1] = m;
        key[j + 1] = kk;
    }
}

int quiesce(Search& S, const Position& p, int alpha, int beta, int qply)
{
    if (out_of_time(S)) return 0;
    const int stand = evaluate(p);
    if (stand >= beta) return stand;
    if (stand > alpha) alpha = stand;
    if (qply >= 8) return stand;
    MoveList l;
    pseudo(p, l, true);
    order(p, l);
    for (int k = 0; k < l.n; ++k) {
        Position q = p;
        q.make(l.m[k]);
        if (q.attacked(q.king[p.side], p.side ^ 1)) continue;
        const int v = -quiesce(S, q, -beta, -alpha, qply + 1);
        if (S.stopped) return 0;
        if (v >= beta) return v;
        if (v > alpha) alpha = v;
    }
    return alpha;
}

int negamax(Search& S, const Position& p, int depth, int alpha, int beta, int ply)
{
    if (out_of_time(S)) return 0;
    if (p.halfmove >= 100) return 0;
    const bool check = p.in_check(p.side);
    if (depth <= 0 && !check) return quiesce(S, p, alpha, beta, 0);
    MoveList l;
    pseudo(p, l, false);
    order(p, l);
    int best = -kInf, legal = 0;
    for (int k = 0; k < l.n; ++k) {
        Position q = p;
        q.make(l.m[k]);
        if (q.attacked(q.king[p.side], p.side ^ 1)) continue;
        ++legal;
        const int v = -negamax(S, q, depth - 1, -beta, -alpha, ply + 1);
        if (S.stopped) return 0;
        if (v > best) best = v;
        if (v > alpha) alpha = v;
        if (alpha >= beta) break;
    }
    if (!legal) return check ? -(kMate - ply) : 0;
    return best;
}

} // namespace

int best_move(const Game& g, int level, uint32_t seed, uint32_t (*clock)(), volatile bool* stop)
{
    static const int      kDepth[3] = {2, 3, 6};
    static const uint32_t kTime[3]  = {0, 2000, 6000};
    level = level < 0 ? 0 : level > 2 ? 2 : level;
    MoveList l;
    g.legal(l);
    if (l.n <= 1) return l.n ? 0 : -1;
    // Root moves carry their index in the legal list
    int idx[kMaxMoves], score[kMaxMoves];
    for (int k = 0; k < l.n; ++k) { idx[k] = k; score[k] = 0; }
    // Shuffle first, so equal moves come in a different order each game
    uint32_t rng = seed ? seed : 0x9E3779B9u;
    for (int k = l.n - 1; k > 0; --k) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const int j = rng % (k + 1);
        const int t = idx[k]; idx[k] = idx[j]; idx[j] = t;
    }
    Search S{clock, 0, stop};
    if (clock && kTime[level]) S.deadline = clock() + kTime[level];
    int best_idx = idx[0];
    for (int depth = 1; depth <= kDepth[level]; ++depth) {
        int alpha = -kInf, iter_best = -1;
        for (int k = 0; k < l.n; ++k) {
            Position q = g.pos;
            q.make(l.m[idx[k]]);
            const int v = -negamax(S, q, depth - 1, -kInf, -alpha, 1);
            if (S.stopped) break;
            score[k] = v;
            if (v > alpha) { alpha = v; iter_best = k; }
        }
        if (S.stopped) break;                     // keep the last finished depth's choice
        best_idx = idx[iter_best];
        // Next depth: best first, the rest by this depth's scores (stable)
        for (int i = 1; i < l.n; ++i) {
            const int ti = idx[i], ts = score[i];
            int j = i - 1;
            while (j >= 0 && score[j] < ts) { idx[j + 1] = idx[j]; score[j + 1] = score[j]; --j; }
            idx[j + 1] = ti; score[j + 1] = ts;
        }
        if (alpha >= kMate - 64) break;           // a forced mate found
    }
    return best_idx;
}

} // namespace chess
