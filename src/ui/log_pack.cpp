#include "log_pack.h"

#include <cstring>
#include <new>

namespace logpack {

namespace {

constexpr int kWindowBits = 12;
constexpr int kWindow = 1 << kWindowBits;      // 4 KB back-references
constexpr int kHashBits = 12;
constexpr int kChain = 96;                      // match candidates tried per position
constexpr int kMinMatch = 3, kMaxMatch = 258;
constexpr uint16_t kNone = 0xFFFF;

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                               3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

struct Bits {
    uint8_t* out;
    size_t cap, pos = 0;
    uint32_t acc = 0;
    int n = 0;
    bool full = false;
    void put(uint32_t v, int count)               // LSB first
    {
        acc |= v << n;
        n += count;
        while (n >= 8) {
            if (pos < cap) out[pos++] = uint8_t(acc);
            else full = true;
            acc >>= 8;
            n -= 8;
        }
    }
    void put_code(uint32_t code, int len)         // Huffman codes go MSB first
    {
        uint32_t r = 0;
        for (int k = 0; k < len; ++k) r |= ((code >> k) & 1) << (len - 1 - k);
        put(r, len);
    }
    void flush() { if (n) put(0, 8 - n); }
};

void literal(Bits& b, int v)                    // fixed literal/length codes
{
    if (v < 144)      b.put_code(0x30 + v, 8);
    else if (v < 256) b.put_code(0x190 + v - 144, 9);
    else if (v < 280) b.put_code(v - 256, 7);
    else              b.put_code(0xC0 + v - 280, 8);
}

void match(Bits& b, int len, int dist)
{
    int lc = 28;
    while (kLenBase[lc] > len) --lc;
    literal(b, 257 + lc);
    if (kLenExtra[lc]) b.put(len - kLenBase[lc], kLenExtra[lc]);
    int dc = 29;
    while (kDistBase[dc] > dist) --dc;
    b.put_code(dc, 5);
    if (kDistExtra[dc]) b.put(dist - kDistBase[dc], kDistExtra[dc]);
}

inline uint32_t hash3(const uint8_t* p)
{
    return ((uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2]) * 2654435761u) >> (32 - kHashBits);
}

} // namespace

size_t deflate(const uint8_t* in, size_t n, uint8_t* out, size_t cap)
{
    if (n >= kNone) return 0;                   // positions are 16-bit
    uint16_t* head = new (std::nothrow) uint16_t[1 << kHashBits];
    uint16_t* prev = new (std::nothrow) uint16_t[kWindow];
    if (!head || !prev) { delete[] head; delete[] prev; return 0; }
    for (int k = 0; k < (1 << kHashBits); ++k) head[k] = kNone;

    Bits b{out, cap};
    b.put(1, 1);                                // last block
    b.put(1, 2);                                // fixed Huffman codes
    auto insert = [&](size_t i) {
        if (i + kMinMatch > n) return;
        const uint32_t h = hash3(in + i);
        prev[i & (kWindow - 1)] = head[h];
        head[h] = uint16_t(i);
    };
    size_t i = 0;
    while (i < n) {
        int best = 0, best_d = 0;
        if (i + kMinMatch <= n) {
            const int limit = int(n - i < size_t(kMaxMatch) ? n - i : kMaxMatch);
            uint16_t c = head[hash3(in + i)];
            for (int tries = 0; c != kNone && tries < kChain; ++tries) {
                const size_t d = i - c;
                if (c >= i || d >= size_t(kWindow)) break;
                if (in[c + best] == in[i + best]) {
                    int l = 0;
                    while (l < limit && in[c + l] == in[i + l]) ++l;
                    if (l > best) { best = l; best_d = int(d); if (l == limit) break; }
                }
                const uint16_t nx = prev[c & (kWindow - 1)];
                if (nx != kNone && nx >= c) break;      // stale slot from an older window
                c = nx;
            }
        }
        if (best >= kMinMatch) {
            match(b, best, best_d);
            for (int k = 0; k < best; ++k) insert(i + k);
            i += best;
        } else {
            literal(b, in[i]);
            insert(i);
            ++i;
        }
    }
    literal(b, 256);                            // end of block
    b.flush();
    delete[] head;
    delete[] prev;
    return b.full ? 0 : b.pos;
}

size_t base43(const uint8_t* in, size_t n, char* out, size_t cap)
{
    const size_t need = n / 2 * 3 + (n & 1) * 2 + 1;
    if (cap < need) return 0;
    size_t o = 0;
    for (size_t k = 0; k + 1 < n; k += 2) {
        uint32_t v = uint32_t(in[k]) << 8 | in[k + 1];
        out[o++] = kAlphabet[v % 43]; v /= 43;
        out[o++] = kAlphabet[v % 43]; v /= 43;
        out[o++] = kAlphabet[v];
    }
    if (n & 1) {
        const uint32_t v = in[n - 1];
        out[o++] = kAlphabet[v % 43];
        out[o++] = kAlphabet[v / 43];
    }
    out[o] = 0;
    return o;
}

size_t pack(const char* text, size_t n, uint8_t* work, size_t work_cap, char* out, size_t cap)
{
    if (work_cap < 2) return 0;
    work[0] = kFormat;
    const size_t z = deflate(reinterpret_cast<const uint8_t*>(text), n, work + 1, work_cap - 1);
    if (!z) return 0;
    return base43(work, z + 1, out, cap);
}

} // namespace logpack
