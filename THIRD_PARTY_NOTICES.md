# Third-party components and sources

CYD Classic Games is MIT licensed. Everything it builds on is listed here with
its license. **Rule: only MIT, BSD, Apache-2.0, zlib or public-domain code may
be copied into this repository.** GPL projects can be studied for UX ideas but
no code from them may be copied. Game data (word lists, questions, levels)
needs a license that sits with MIT (public domain, CC0, or permissive with
attribution); share-alike data needs Tom's OK first.

The repository was seeded from Tom's own
[CYD-Sudoku](https://github.com/tomtombombadil/CYD-Sudoku) v1.0.0 (MIT): board
support, display/touch glue, tools and the Sudoku game.

## In the firmware

| Component | Use | License |
|---|---|---|
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | Display, touch, backlight drivers | MIT and BSD-2-Clause |
| [LVGL](https://github.com/lvgl/lvgl) 9.x | UI widgets and rendering | MIT |
| Montserrat font (bundled with LVGL) | All on-screen text | SIL Open Font License 1.1 |
| [DejaVu Sans](https://dejavu-fonts.github.io/) chess symbols U+2654-265F, converted with [lv_font_conv](https://github.com/lvgl/lv_font_conv) (MIT) to `src/games/chess/chess_font_*.c` | Chess pieces | Bitstream Vera license (permissive; DejaVu changes public domain) |
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) via [pioarduino](https://github.com/pioarduino/platform-espressif32) | Framework / core | LGPL-2.1 (core), Apache-2.0 (ESP-IDF) |

The Arduino core is LGPL-2.1. Because this project's full source is public,
anyone can rebuild the firmware against a modified core, which satisfies the
LGPL for the binaries published in Releases.

## Game data

| Data | Use | License |
|---|---|---|
| [ENABLE2K](https://github.com/dolph/dictionary) word list (five-letter words, `assets/words/enable_5.txt`) | CYD-dle: accepted guesses | Public domain |
| [ESDB / SCOWL](https://github.com/en-wl/wordlist) by Kevin Atkinson, size 35 five-letter words (`assets/words/scowl35_5.txt`) | CYD-dle: answers (common words) | Permissive; notice below and in `assets/words/SCOWL-Copyright.txt` |

CYD-dle never uses the words in `assets/words/blocklist.txt` and never
picks those in `assets/words/answers_exclude.txt` as answers.

> Copyright 2000-2026 by Kevin Atkinson
>
> Permission to use, copy, modify, distribute, and sell any part of the English
> Speller Database (ESDB, previously known as SCOWLv2), or word lists created
> from it, is hereby granted without fee, provided that the above copyright
> notice appears in all copies and that both the above copyright notice and
> this notice appear in supporting documentation. Kevin Atkinson makes no
> representations about the suitability of this database for any purpose. It
> is provided "as is" without express or implied warranty.

## Art

| Item | Use | License |
|---|---|---|
| Splash screens (`assets/splash/`) | Title images shown at boot | Tom's own art for this project, MIT with the rest |

## On the web flasher page (loaded from CDNs, not copied into the repo)

| Component | Use | License |
|---|---|---|
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) | Browser flashing | Apache-2.0 |
| [Barlow / Barlow Condensed](https://fonts.google.com/specimen/Barlow) (Google Fonts) | Page typography | SIL Open Font License 1.1 |

## Reference material (no code copied)

| Source | What we took | License |
|---|---|---|
| [witnessmenow/ESP32-Cheap-Yellow-Display](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) | 2.8" pinouts, panel settings | MIT |
| lcdwiki.com datasheets for the ESP32-32E display boards | 3.2"/3.5"/4.0" pinouts | Documentation |

## Sudoku code

The solver, generator and game rules in `src/games/sudoku/` were written for
CYD-Sudoku; no third-party Sudoku code was copied in. These were considered and remain
options for later:

| Source | Possible use | License | OK to copy code? |
|---|---|---|---|
| Simon Tatham's Portable Puzzle Collection — `solo.c` | Difficulty grading by human solving techniques | MIT | Yes |
| [t-dillon/tdoku](https://github.com/t-dillon/tdoku) | Faster solver | BSD-2-Clause | Yes (keep notice) |
| [grantm/sudoku-exchange-puzzle-bank](https://github.com/grantm/sudoku-exchange-puzzle-bank) | Pre-rated puzzle packs on LittleFS | Public domain (Unlicense-style) | Yes |
| [stephenostermiller/qqwing](https://github.com/stephenostermiller/qqwing) | — | GPL-2.0 | **No** — reference only |
| OpenSudoku, [LibreSudoku](https://github.com/kaajjo/LibreSudoku) (Android) | UX ideas only | GPL-3.0 | **No** — reference only |

When code is copied in, keep its original copyright header in the file and
move its row to the "In the firmware" table.
