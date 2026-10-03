// Host-side checks for src/games/sudoku/stats.*  (built and run by CI)
#include "../../src/games/sudoku/sudoku_stats.h"
#include <cstdio>
#include <cstring>

using namespace sudoku::stats;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main()
{
    char line[96];
    Record r; r.difficulty = 1; r.result = Result::Solved; r.seconds = 512; r.hints = 2;
    CHECK(format_line(line, sizeof line, 12, r) > 0);
    CHECK(strcmp(line, "12,Medium,Solved,512,8:32,2\n") == 0);
    CHECK(format_body(line, sizeof line, r) > 0);           // stats store adds "seq,"
    CHECK(strcmp(line, "Medium,Solved,512,8:32,2\n") == 0);
    format_line(line, sizeof line, 12, r);

    Record back;
    CHECK(parse_line(line, back));
    CHECK(back.difficulty == 1 && back.result == Result::Solved && back.seconds == 512 && back.hints == 2);

    Record g; g.difficulty = 3; g.result = Result::GaveUp; g.seconds = 3723;
    format_line(line, sizeof line, 13, g);
    CHECK(strcmp(line, "13,Expert,Gave up,3723,1:02:03,0\n") == 0);
    CHECK(parse_line(line, back) && back.result == Result::GaveUp && back.difficulty == 3);

    CHECK(!parse_line(kCsvHeader, back));
    CHECK(!parse_line("garbage", back));
    CHECK(!parse_line("1,Impossible,Solved,10,0:10,0", back));

    Summary s;
    for (int k = 0; k < 10; ++k) {
        Record x; x.difficulty = 0; x.result = Result::Solved; x.seconds = 100 + k; s.add(x);
    }
    Record gu; gu.difficulty = 0; gu.result = Result::GaveUp; gu.seconds = 999; s.add(gu);
    CHECK(s.total == 11);
    CHECK(s.level[0].solved == 10 && s.level[0].gave_up == 1);
    CHECK(s.level[0].best_s == 100);
    CHECK(s.average_s(0) == 105);           // (100..109) avg 104.5 -> 105
    CHECK(s.average_s(2) == 0);
    CHECK(s.recent_n == kRecent);
    CHECK(s.newest(0).result == Result::GaveUp);
    CHECK(s.newest(1).seconds == 109);
    CHECK(s.newest(kRecent - 1).seconds == 103);

    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
