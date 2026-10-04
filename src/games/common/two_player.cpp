#include "two_player.h"

#include <cstdio>
#include <cstring>

namespace twoplayer {

const char* const kCsvHeader = "#,Mode,Level,Result,Moves,Seconds,Time\n";

const char* mode_name(Mode m)
{
    switch (m) {
        case Mode::Computer:    return "Computer";
        case Mode::PassAndPlay: return "Pass and play";
        case Mode::Wireless:    return "Wireless";
    }
    return "";
}

const char* level_name(Level l)
{
    switch (l) {
        case Level::Easy:   return "Easy";
        case Level::Medium: return "Medium";
        case Level::Hard:   return "Hard";
    }
    return "";
}

void format_time(char* buf, size_t cap, uint32_t s)
{
    if (s >= 3600) snprintf(buf, cap, "%lu:%02lu:%02lu", (unsigned long)(s / 3600),
                            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    else           snprintf(buf, cap, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

namespace {

void result_text(char* out, size_t cap, const Record& r, const Sides& s)
{
    if (r.result == Result::Draw) { snprintf(out, cap, "Draw"); return; }
    if (r.mode == Mode::Computer || r.mode == Mode::Wireless) {      // side 1 = this board's player
        snprintf(out, cap, "%s", r.result == Result::Side1 ? "Won" : "Lost");
        return;
    }
    snprintf(out, cap, "%s won", r.result == Result::Side1 ? s.side1 : s.side2);
}

} // namespace

size_t format_body(char* buf, size_t cap, const Record& r, const Sides& s)
{
    char t[16], res[32];
    format_time(t, sizeof t, r.seconds);
    result_text(res, sizeof res, r, s);
    const int n = snprintf(buf, cap, "%s,%s,%s,%u,%lu,%s\n", mode_name(r.mode),
                           r.mode == Mode::Computer ? level_name(r.level) : "-", res,
                           (unsigned)r.moves, (unsigned long)r.seconds, t);
    return (n > 0 && size_t(n) < cap) ? size_t(n) : 0;
}

bool parse_line(const char* line, Record& out, const Sides& s)
{
    char mode[24] = {}, level[12] = {}, res[32] = {}, tm[16] = {};
    unsigned long seq = 0, secs = 0;
    unsigned moves = 0;
    if (!line || line[0] == '#') return false;
    if (sscanf(line, "%lu,%23[^,],%11[^,],%31[^,],%u,%lu,%15[^,\r\n]", &seq, mode, level, res,
               &moves, &secs, tm) != 7)
        return false;
    Record r;
    bool ok = false;
    for (int m = 0; m < 3; ++m)
        if (strcmp(mode, mode_name(static_cast<Mode>(m))) == 0) { r.mode = static_cast<Mode>(m); ok = true; }
    if (!ok) return false;
    if (r.mode == Mode::Computer) {
        ok = false;
        for (int l = 0; l < kLevels; ++l)
            if (strcmp(level, level_name(static_cast<Level>(l))) == 0) { r.level = static_cast<Level>(l); ok = true; }
        if (!ok) return false;
    }
    for (int k = 0; k < 3; ++k) {
        Record probe = r;
        probe.result = static_cast<Result>(k);
        char want[32];
        result_text(want, sizeof want, probe, s);
        if (strcmp(want, res) == 0) {
            r.result = probe.result;
            r.moves = static_cast<uint16_t>(moves);
            r.seconds = static_cast<uint32_t>(secs);
            out = r;
            return true;
        }
    }
    return false;
}

void Summary::add(const Record& r)
{
    ++total;
    if (r.mode == Mode::Computer) {
        const int l = static_cast<int>(r.level);
        if (l < 0 || l >= kLevels) return;
        if (r.result == Result::Side1)      ++won[l];
        else if (r.result == Result::Side2) ++lost[l];
        else                                ++drawn[l];
    } else if (r.mode == Mode::Wireless) {
        if (r.result == Result::Side1)      ++wl_won;
        else if (r.result == Result::Side2) ++wl_lost;
        else                                ++wl_drawn;
    } else {
        if (r.result == Result::Side1)      ++side1;
        else if (r.result == Result::Side2) ++side2;
        else                                ++draws;
    }
}

} // namespace twoplayer
