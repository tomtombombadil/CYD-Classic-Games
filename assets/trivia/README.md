# Trivia questions - CC BY-SA 4.0

`opentdb_raw.json` holds the verified questions of [Open Trivia DB](https://opentdb.com),
licensed under [Creative Commons Attribution-ShareAlike 4.0 International](https://creativecommons.org/licenses/by-sa/4.0/)
(fetched 2026-10-06 by `tools/fetch_trivia.py`).

`drop.txt` lists the questions CYD Classic Games leaves out (screened for young players).
`tools/make_trivia.py` builds `src/games/common/trivia_data.cpp` from these two files:
changed - screened, and the text turned into plain ASCII.

Everything in this folder, and `trivia_data.cpp`, is shared under CC BY-SA 4.0. The rest of
the firmware is MIT (see `THIRD_PARTY_NOTICES.md`).
