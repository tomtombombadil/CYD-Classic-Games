#include "cyddle_core.h"

#include <cstring>
#include "cyddle_words.h"

namespace cyddle {

namespace {

uint32_t pack(const char w[kLen])
{
    uint32_t v = 0;
    for (int k = 0; k < kLen; ++k) v = (v << 5) | uint32_t(w[k] - 'a');
    return v;
}

bool lower_word(const char w[kLen])
{
    for (int k = 0; k < kLen; ++k) if (w[k] < 'a' || w[k] > 'z') return false;
    return true;
}

bool find(const uint32_t* list, int n, uint32_t v)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (list[mid] == v) return true;
        if (list[mid] < v) lo = mid + 1; else hi = mid - 1;
    }
    return false;
}

} // namespace

int answer_count() { return kAnswersCount; }

void answer_word(int index, char out[kLen])
{
    const uint32_t v = kAnswers[index < 0 || index >= kAnswersCount ? 0 : index];
    for (int k = kLen - 1, s = 0; k >= 0; --k, s += 5) out[k] = static_cast<char>('a' + ((v >> s) & 31));
}

bool is_word(const char w[kLen])
{
    if (!lower_word(w)) return false;
    const uint32_t v = pack(w);
    return find(kAnswers, kAnswersCount, v) || find(kGuesses, kGuessesCount, v);
}

void score(const char guess[kLen], const char answer[kLen], Mark out[kLen])
{
    int left[26] = {};
    for (int k = 0; k < kLen; ++k) {
        if (guess[k] == answer[k]) out[k] = Correct;
        else { out[k] = Absent; ++left[answer[k] - 'a']; }
    }
    for (int k = 0; k < kLen; ++k) {
        if (out[k] == Correct) continue;
        int& n = left[guess[k] - 'a'];
        if (n > 0) { out[k] = Present; --n; }
    }
}

bool Game::solved() const
{
    if (!rows) return false;
    char a[kLen];
    answer_word(answer, a);
    return memcmp(guess[rows - 1], a, kLen) == 0;
}

void Game::marks(int row, Mark out[kLen]) const
{
    char a[kLen];
    answer_word(answer, a);
    score(guess[row], a, out);
}

Mark Game::key_mark(char letter) const
{
    Mark best = Unknown;
    for (int r = 0; r < rows; ++r) {
        Mark m[kLen];
        marks(r, m);
        for (int k = 0; k < kLen; ++k)
            if (guess[r][k] == letter && m[k] > best) best = m[k];
    }
    return best;
}

void Game::type(char letter)
{
    if (over() || typed >= kLen || letter < 'a' || letter > 'z') return;
    typing[typed++] = letter;
}

void Game::back()
{
    if (!over() && typed) --typed;
}

Submit Game::submit()
{
    if (over()) return Submit::Ok;
    if (typed < kLen) return Submit::TooShort;
    if (!is_word(typing)) return Submit::NotWord;
    if (level == 2) {
        // Hard: greens stay where they were, golds are used again
        for (int r = 0; r < rows; ++r) {
            Mark m[kLen];
            marks(r, m);
            int need[26] = {};
            for (int k = 0; k < kLen; ++k) {
                if (m[k] == Correct && typing[k] != guess[r][k]) return Submit::MustUseHints;
                if (m[k] == Correct || m[k] == Present) ++need[guess[r][k] - 'a'];
            }
            int have[26] = {};
            for (int k = 0; k < kLen; ++k) ++have[typing[k] - 'a'];
            for (int c = 0; c < 26; ++c) if (have[c] < need[c]) return Submit::MustUseHints;
        }
    }
    memcpy(guess[rows++], typing, kLen);
    typed = 0;
    return Submit::Ok;
}

void Game::start(int lvl, Rng& rng)
{
    const int n = answer_count();
    int unplayed = 0;
    for (int k = 0; k < n; ++k) unplayed += !((played[k / 8] >> (k % 8)) & 1);
    if (unplayed == 0) { memset(played, 0, sizeof played); unplayed = n; }
    int pick = static_cast<int>(rng.next() % static_cast<uint32_t>(unplayed));
    for (int k = 0; k < n; ++k) {
        if ((played[k / 8] >> (k % 8)) & 1) continue;
        if (pick-- == 0) { answer = static_cast<uint16_t>(k); break; }
    }
    played[answer / 8] |= static_cast<uint8_t>(1u << (answer % 8));
    level = static_cast<uint8_t>(lvl < 0 ? 0 : lvl > 2 ? 2 : lvl);
    restart();
}

size_t Game::serialize(uint8_t* buf, size_t cap) const
{
    if (cap < kSaveBytes) return 0;
    uint8_t* p = buf;
    memcpy(p, "CYD1", 4); p += 4;
    *p++ = answer & 0xFF;
    *p++ = answer >> 8;
    *p++ = level;
    *p++ = rows;
    memcpy(p, guess, sizeof guess); p += sizeof guess;
    memcpy(p, typing, sizeof typing); p += sizeof typing;
    *p++ = typed;
    memcpy(p, played, sizeof played);
    return kSaveBytes;
}

bool Game::deserialize(const uint8_t* buf, size_t len)
{
    if (len < kSaveBytes || memcmp(buf, "CYD1", 4) != 0) return false;
    Game t;
    const uint8_t* p = buf + 4;
    t.answer = static_cast<uint16_t>(p[0] | (p[1] << 8)); p += 2;
    t.level = *p++;
    t.rows = *p++;
    memcpy(t.guess, p, sizeof t.guess); p += sizeof t.guess;
    memcpy(t.typing, p, sizeof t.typing); p += sizeof t.typing;
    t.typed = *p++;
    memcpy(t.played, p, sizeof t.played);
    if (t.answer >= answer_count() || t.level > 2 || t.rows > t.max_rows() || t.typed > kLen) return false;
    for (int r = 0; r < t.rows; ++r) if (!lower_word(t.guess[r])) return false;
    *this = t;
    return true;
}

} // namespace cyddle
