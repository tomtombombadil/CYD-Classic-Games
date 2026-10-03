#include "puzzle_stats.h"

#include <cstdio>
#include <cstring>
#include "two_player.h"

namespace puzzle {

const char* const kCsvHeader = "#,Level,Result,Moves,Seconds,Time,Par\n";

size_t format_body(char* buf, size_t cap, const Record& r, const char* const names[kLevels])
{
    char t[16];
    twoplayer::format_time(t, sizeof t, r.seconds);
    const int n = snprintf(buf, cap, "%s,%s,%u,%lu,%s,%u\n", names[r.level < kLevels ? r.level : 0],
                           r.solved ? "Solved" : "Gave up", (unsigned)r.moves,
                           (unsigned long)r.seconds, t, (unsigned)r.par);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out, const char* const names[kLevels])
{
    char lvl[16] = {}, res[16] = {}, tm[16] = {};
    unsigned long seq = 0, secs = 0;
    unsigned moves = 0, par = 0;
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%15[^,],%15[^,],%u,%lu,%15[^,],%u", &seq, lvl, res, &moves, &secs, tm, &par) != 7)
        return false;
    int l = -1;
    for (int k = 0; k < kLevels; ++k) if (strcmp(lvl, names[k]) == 0) l = k;
    if (l < 0) return false;
    Record r;
    if (strcmp(res, "Solved") == 0) r.solved = true;
    else if (strcmp(res, "Gave up") == 0) r.solved = false;
    else return false;
    r.level = static_cast<uint8_t>(l);
    r.moves = static_cast<uint16_t>(moves);
    r.par = static_cast<uint16_t>(par);
    r.seconds = static_cast<uint32_t>(secs);
    out = r;
    return true;
}

void Summary::add(const Record& r)
{
    ++total;
    if (r.level < kLevels) {
        if (r.solved) {
            ++solved[r.level];
            if (!best_s[r.level] || r.seconds < best_s[r.level]) best_s[r.level] = r.seconds;
            if (!best_moves[r.level] || r.moves < best_moves[r.level]) best_moves[r.level] = r.moves;
        } else {
            ++gave_up[r.level];
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

} // namespace puzzle
