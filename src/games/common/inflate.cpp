#include "inflate.h"

#include <cstring>

namespace inflate {

namespace {

struct Reader {
    const uint8_t* in;
    size_t   len, pos = 0;
    uint32_t bits = 0;
    int      count = 0;
    bool     bad = false;

    int bit()
    {
        if (count == 0) {
            if (pos >= len) { bad = true; return 0; }
            bits = in[pos++];
            count = 8;
        }
        const int b = int(bits & 1);
        bits >>= 1;
        --count;
        return b;
    }
    uint32_t take(int n)            // n bits, least significant first
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= uint32_t(bit()) << i;
        return v;
    }
};

// A canonical Huffman code: how many codes of each length, and the symbols
// in code order
struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

bool build(Huffman& h, const uint8_t* lengths, int n)
{
    memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
    if (h.count[0] == n) return true;                 // no codes (allowed for an empty distance tree)
    int left = 1;                                     // over-subscribed codes are bad data
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = uint16_t(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i) if (lengths[i]) h.symbol[offs[lengths[i]]++] = uint16_t(i);
    return true;
}

int decode(Reader& r, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= r.bit();
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (r.bad) return -1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t  kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t  kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Reader& r, const Huffman& lit, const Huffman& dist, uint8_t* out, size_t cap, size_t& n)
{
    for (;;) {
        const int sym = decode(r, lit);
        if (sym < 0 || r.bad) return false;
        if (sym < 256) {
            if (n >= cap) return false;
            out[n++] = uint8_t(sym);
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) return false;
            const size_t len = kLenBase[li] + r.take(kLenExtra[li]);
            const int ds = decode(r, dist);
            if (ds < 0 || ds >= 30 || r.bad) return false;
            const size_t d = kDistBase[ds] + r.take(kDistExtra[ds]);
            if (d > n || n + len > cap) return false;
            for (size_t k = 0; k < len; ++k, ++n) out[n] = out[n - d];
        }
    }
}

} // namespace

long raw(const uint8_t* in, size_t in_len, uint8_t* out, size_t out_cap)
{
    Reader r{in, in_len};
    size_t n = 0;
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huffman lit_h, dist_h, len_h;                          // ~2 KB of stack
    Huffman* lit = &lit_h;
    Huffman* dist = &dist_h;
    bool ok = true, last = false;
    while (ok && !last) {
        last = r.bit() != 0;
        const uint32_t type = r.take(2);
        if (r.bad) { ok = false; break; }
        if (type == 0) {                                   // stored
            r.count = 0;
            if (r.pos + 4 > r.len) { ok = false; break; }
            const size_t len = size_t(in[r.pos] | in[r.pos + 1] << 8);
            const size_t nlen = size_t(in[r.pos + 2] | in[r.pos + 3] << 8);
            r.pos += 4;
            if (len != (~nlen & 0xFFFF) || r.pos + len > r.len || n + len > out_cap) { ok = false; break; }
            memcpy(out + n, in + r.pos, len);
            r.pos += len;
            n += len;
        } else if (type == 1) {                            // fixed codes
            uint8_t l[320];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            build(*lit, l, 288);
            for (i = 0; i < 30; ++i) l[i] = 5;
            build(*dist, l, 30);
            ok = codes(r, *lit, *dist, out, out_cap, n);
        } else if (type == 2) {                            // dynamic codes
            const int nlen = int(r.take(5)) + 257, ndist = int(r.take(5)) + 1, ncode = int(r.take(4)) + 4;
            if (nlen > 286 || ndist > 30) { ok = false; break; }
            uint8_t l[320] = {};
            for (int i = 0; i < ncode; ++i) l[kOrder[i]] = uint8_t(r.take(3));
            Huffman* lencode = &len_h;
            ok = build(*lencode, l, 19);
            int i = 0;
            while (ok && i < nlen + ndist) {
                int sym = decode(r, *lencode);
                if (sym < 0 || r.bad) { ok = false; break; }
                if (sym < 16) { l[i++] = uint8_t(sym); continue; }
                uint8_t v = 0;
                int rep;
                if (sym == 16) { if (i == 0) { ok = false; break; } v = l[i - 1]; rep = 3 + int(r.take(2)); }
                else if (sym == 17) rep = 3 + int(r.take(3));
                else rep = 11 + int(r.take(7));
                if (i + rep > nlen + ndist) { ok = false; break; }
                while (rep--) l[i++] = v;
            }
            if (!ok) break;
            if (l[256] == 0) { ok = false; break; }        // no end-of-block code
            ok = build(*lit, l, nlen) && build(*dist, l + nlen, ndist);
            if (ok) ok = codes(r, *lit, *dist, out, out_cap, n);
        } else {
            ok = false;
        }
    }
    return ok ? long(n) : -1;
}

} // namespace inflate
