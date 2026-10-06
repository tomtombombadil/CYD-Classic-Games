#include "trivia_bank.h"

#include <cstring>
#include <new>
#include "inflate.h"

namespace trivia {

namespace {

const char* const kNames[kCategories] = {
    "General Knowledge", "Books", "Film", "Music", "Musicals & Theatre", "Television",
    "Video Games", "Board Games", "Science & Nature", "Computers", "Mathematics", "Mythology",
    "Sports", "Geography", "History", "Politics", "Art", "Celebrities", "Animals", "Vehicles",
    "Comics", "Gadgets", "Anime & Manga", "Cartoons",
};

uint8_t* buf = nullptr;
int      buf_block = -1;
long     buf_len = 0;

bool unpack(int b)
{
    if (b == buf_block && buf) return true;
    if (!buf) {
        buf = new (std::nothrow) uint8_t[size_t(kMaxBlockText) + 1];
        if (!buf) return false;
    }
    buf_block = -1;
    const uint32_t a = kBlockOffset[b], e = kBlockOffset[b + 1];
    buf_len = inflate::raw(kData + a, e - a, buf, size_t(kMaxBlockText));
    if (buf_len < 0) return false;
    buf_block = b;
    return true;
}

void copy(char* dst, size_t cap, const char* s, size_t n)
{
    if (n >= cap) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

} // namespace

int  count() { return kQuestions; }
int  category(int i) { return (i < 0 || i >= kQuestions) ? 0 : kMeta[i] & 0x1F; }
int  difficulty(int i) { return (i < 0 || i >= kQuestions) ? 0 : (kMeta[i] >> 5) & 3; }
bool true_false(int i) { return i >= 0 && i < kQuestions && (kMeta[i] & 0x80); }
const char* category_name(int c) { return (c >= 0 && c < kCategories) ? kNames[c] : ""; }
const char* category_short(int c) { return c == 10 ? "Math" : c == 4 ? "Theatre" : category_name(c); }

bool get(int i, Question& q)
{
    if (i < 0 || i >= kQuestions || !unpack(i / kBlockSize)) return false;
    // find record i % kBlockSize (records end with 0x1E, fields split by 0x1F)
    const char* p = reinterpret_cast<const char*>(buf);
    const char* end = p + buf_len;
    for (int k = i % kBlockSize; k > 0 && p < end; --k) {
        while (p < end && *p != 0x1E) ++p;
        ++p;
    }
    if (p >= end) return false;
    const char* fields[6];
    size_t lens[6];
    int n = 0;
    const char* s = p;
    while (p < end && *p != 0x1E && n < 6) {
        if (*p == 0x1F) { fields[n] = s; lens[n] = size_t(p - s); ++n; s = p + 1; }
        ++p;
    }
    if (p >= end || n >= 6) return false;
    fields[n] = s; lens[n] = size_t(p - s); ++n;
    if (n != 3 && n != 5) return false;
    copy(q.text, sizeof q.text, fields[0], lens[0]);
    for (int a = 1; a < n; ++a) copy(q.answer[a - 1], sizeof q.answer[0], fields[a], lens[a]);
    q.answers = uint8_t(n - 1);
    q.category = uint8_t(category(i));
    q.difficulty = uint8_t(difficulty(i));
    return true;
}

void release()
{
    delete[] buf;
    buf = nullptr;
    buf_block = -1;
}

bool verify()
{
    uint32_t sum = 0;
    for (int b = 0; b < kBlocks; ++b) {
        if (!unpack(b)) return false;
        for (long k = 0; k < buf_len; ++k) {
            const uint8_t c = buf[k];
            if (c == 0x1E || c == 0x1F) continue;
            sum = sum * 31u + c;
        }
    }
    release();
    return sum == kChecksum;
}

} // namespace trivia
