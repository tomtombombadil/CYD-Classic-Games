#include "sudoku_stats.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sudoku_core.h"

namespace sudoku {
namespace stats {

const char* const kCsvHeader = "#,Difficulty,Result,Seconds,Time,Hints\n";

void Summary::add(const Record& r)
{
    ++total;
    if (r.difficulty < 4) {
        PerDifficulty& p = level[r.difficulty];
        if (r.result == Result::Solved) {
            ++p.solved;
            p.solved_s += r.seconds;
            if (p.best_s == 0 || r.seconds < p.best_s) p.best_s = r.seconds;
        } else {
            ++p.gave_up;
        }
    }
    recent[recent_head] = r;
    recent_head = (recent_head + 1) % kRecent;
    if (recent_n < kRecent) ++recent_n;
}

const Record& Summary::newest(int i) const
{
    return recent[(recent_head - 1 - i + 2 * kRecent) % kRecent];
}

uint32_t Summary::average_s(int d) const
{
    if (d < 0 || d > 3 || level[d].solved == 0) return 0;
    return static_cast<uint32_t>((level[d].solved_s + level[d].solved / 2) / level[d].solved);
}

void format_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

const char* result_name(Result r) { return r == Result::Solved ? "Solved" : "Gave up"; }

size_t format_body(char* buf, size_t cap, const Record& r)
{
    char t[16];
    format_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%s,%s,%lu,%s,%u\n",
                           sudoku::difficulty_name(static_cast<sudoku::Difficulty>(r.difficulty & 3)),
                           result_name(r.result), (unsigned long)r.seconds, t, (unsigned)r.hints);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

size_t format_line(char* buf, size_t cap, uint32_t seq, const Record& r)
{
    const int n = snprintf(buf, cap, "%lu,", (unsigned long)seq);
    if (n <= 0 || size_t(n) >= cap) return 0;
    const size_t b = format_body(buf + n, cap - n, r);
    return b ? n + b : 0;
}

bool parse_line(const char* line, Record& out)
{
    // seq,Difficulty,Result,Seconds,Time,Hints
    char diff[16] = {}, res[16] = {}, tm[16] = {};
    unsigned long seq = 0, secs = 0;
    unsigned hints = 0;
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%15[^,],%15[^,],%lu,%15[^,],%u", &seq, diff, res, &secs, tm, &hints) != 6)
        return false;
    int d = -1;
    for (int k = 0; k < 4; ++k)
        if (strcmp(diff, sudoku::difficulty_name(static_cast<sudoku::Difficulty>(k))) == 0) d = k;
    if (d < 0) return false;
    Result r;
    if (strcmp(res, "Solved") == 0) r = Result::Solved;
    else if (strcmp(res, "Gave up") == 0) r = Result::GaveUp;
    else return false;
    out.difficulty = static_cast<uint8_t>(d);
    out.result = r;
    out.seconds = static_cast<uint32_t>(secs);
    out.hints = static_cast<uint8_t>(hints > 255 ? 255 : hints);
    return true;
}

} // namespace stats
} // namespace sudoku
