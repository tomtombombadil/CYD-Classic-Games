# CLAUDE.md — working rules for this repo

CYD Classic Games: a collection of classic games for ESP32 "Cheap Yellow Display"
boards, with CYD-to-CYD play over WiFi. Seeded from
[CYD-Sudoku](https://github.com/tomtombombadil/CYD-Sudoku) v1.0.0, which
stays a separate, finished project. Copy from it freely (Tom's code, MIT);
don't link it as a submodule.

Repo: `tomtombombadil/CYD-Classic-Games`. Web flasher:
https://tomtombombadil.github.io/CYD-Classic-Games/

## The developer
- Tom works on **Windows** with VS Code + PlatformIO. Never give Linux/bash
  commands in instructions for him; use the PlatformIO GUI or PowerShell.
- If a Python tool is ever needed on his side: always a venv, and
  `python -m pip install`, never bare `pip install`.
- He does not want to repeat instructions. Anything decided goes in this file,
  `docs/SPEC.md`, or `THIRD_PARTY_NOTICES.md`.
- Tom plays with a Nintendo DS Lite stylus with firm presses - NOT a finger.
  Don't explain touch problems with finger size or light pressure.

## Board naming (Tom's rule)
- Name boards by what a user can identify: **screen size, display driver,
  touch type**. Never by an information site (LCDwiki is a datasheet
  resource, not a maker or seller) and not by vendor model codes.
- Firmware files: `CYD_<size>in_<DRIVER>_<Resistive|Capacitive>.bin`,
  e.g. `CYD_3.2in_ST7789_Resistive.bin`. No version in the file name (the
  release tag carries it), so links to the latest release stay stable.
- PlatformIO env names can't contain dots, so envs are short
  (`cyd32_st7789_res`) and carry `custom_firmware_name`,
  `custom_board_title`, `custom_board_hint` and, for boards nobody has run
  the build on, `custom_board_tested = no` (flasher + README show
  "untested"). CI, releases and the web flasher all read these -
  platformio.ini is the single source of truth.
- Adding a board = new env with those options + board_select.h entry. Only
  give an env `custom_firmware_name` once its board file exists.

## Architecture (inherited from CYD-Sudoku - keep)
- One PlatformIO env per board; each sets exactly one `CYD_BOARD_*` flag.
- `src/boards/board_select.h` maps the flag to a board file providing
  `class LGFX`, `BOARD_NAME`, `BOARD_TOUCH_RESISTIVE`, `BOARD_PIN_BOOT_BTN`,
  `BOARD_SD_USABLE`. No other file may test `CYD_BOARD_*` flags.
- LVGL only talks to hardware through `src/hal/lvgl_port.cpp`.
  LovyanGFX applies touch calibration + rotation; LVGL rotation stays 0.
- Layout must derive from `lv_display_get_horizontal/vertical_resolution()`,
  never hard-coded sizes. Every screen must work at 240x320 and 320x480.
- Memory: LVGL allocates from the ESP32 heap (LV_STDLIB_CLIB); a fixed
  LV_MEM_SIZE pool overflowed static DRAM. The PC preview uses a fixed pool
  (-DCYD_PREVIEW) and prints per-screen usage - check it when adding
  screens. Tables are one label per column, not objects per cell.
- PC tools (Claude's side, Linux): `tools/preview/build.sh <lvgl 9.6 dir>
  <out>` builds `preview` (picker + game screens at any size) and
  `preview_paging`; `tools/preview/readme_shots.py` turns renders into
  `docs/screenshots/*.png`. Re-render when a shown screen changes.
- Toolchain: pioarduino platform 55.03.312-1 (Arduino-ESP32 3.3.x),
  LovyanGFX 1.2.x, LVGL 9.6. Partition table `huge_app.csv` (3 MB app,
  ~900 KB LittleFS, no OTA - the web flasher is the update path).

## Architecture (new for the collection)
- Each game lives in `src/games/<id>/`:
  - `<id>_core.*` - rules, move generation, AI, save format. Plain C++, no
    Arduino/LVGL, host-tested in `tools/host_tests/`.
  - `<id>_screen.*` - LVGL UI. Hardware actions go through the app shell
    (`ui::Shell` in `src/ui/shell.h`, filled in by main.cpp), so it also
    builds in `tools/preview/`.
  - `<id>_app.cpp` - the registry entry (`games::GameOps <id>_ops`: open,
    close, save, tick, restyle, summary for "Continue", icon) and the
    save/stats glue through the shell.
  - Device-only parts (FreeRTOS tasks, LittleFS) in their own file with a
    no-op stub in `tools/preview/preview_stubs.cpp` (e.g. `sudoku_stock.*`).
- The game list is `src/games/games.def` (one `GAME(id, "Title", Category,
  modes, "blurb")` line each). `src/games/registry.*` builds the table from
  it and `tools/make_site.py` reads it for the web page; nothing else
  hard-codes the game list. Adding a game = folder + `games.def` line.
- Shared UI in `src/ui/`: `widgets.*` (keys, hamburger, overlays, tables,
  screen metrics), `app_shell.cpp` (picker, game switching),
  `settings_screen.cpp` (Display & touch, touch test), `theme.*`.
- Game picker (boot screen): "Classic Games" title bar with ☰ (Display &
  touch), a "Continue <last game>" card (icon, title, the game's summary
  line), then tiles 2 per row grouped by category. Extra games go on pages
  switched with big < > keys (no scrolling); a category that runs onto the
  next page repeats its heading. `preview_paging` renders a long fake list.
- Only one game is alive at a time. Leaving a game saves it and frees its
  screen, AI tables and tasks (e.g. Sudoku's puzzle-stock task runs only
  while Sudoku is open).
- Shared across games: theme, brightness, touch calibration, panel fixes
  (`/ui_settings.bin` format UIS2 = CYD-Sudoku's UIS1 + last game id,
  `/panel_prefs.bin`, `/touch_cal.bin`).
- Stats store (`src/app/stats_store.*`) is game-agnostic: it numbers lines
  ("seq,body"), the game formats/parses the body and owns the header.
- `src/app/legacy_import.*`: flashing over CYD-Sudoku without erasing moves
  its `/game.bin`, `/stats.csv`, `/puzzle_stock.bin` to the per-game names
  and copies SD `/CYD-Sudoku/stats.csv` to `/CYD-Classic-Games/sudoku.csv`.
- Firmware version define: `CYD_GAMES_VERSION` (CI sets it).
- Per game: save `/games/<id>.bin` (temp file + rename, 4-char format tag
  + version, older versions still load) and stats
  (SD `/CYD-Classic-Games/<id>.csv` when usable, else LittleFS `/stats/<id>.csv`).
- Computer opponents think on a core-0 task so the UI never freezes; each
  has difficulty levels limited by depth or time, not by making random
  blunders.

## UI rules (Tom's, all games)
- Portrait by default (comfortable one-handed). Tom is not married to it:
  a game where landscape clearly works better may use it - a CYD is easy to
  rotate in the hand. Rotation is fixed at boot today (`CYD_ROTATION`,
  LovyanGFX applies it to touch too); a landscape game needs a runtime
  rotation switch in `src/hal/lvgl_port.*` first, and must lay out from the
  display size like every screen.
- Resistive touch: big targets; tap, never drag, swipe or long-press.
  Moving a piece = tap the piece, then tap the destination. Swipe games
  (2048) slide by tapping the board edge in that direction.
- No "tap again" / "are you sure" confirmations, ever. Buttons act on the
  first tap.
- Strong highlight tints with distinct hues (cheap TN panels wash out pale
  tints at an angle). No shrinking fonts to squeeze labels in.
- Each game: top bar with a 3-line hamburger menu (new game, restart,
  stats, Display & touch, "All games"). The picker is the boot screen and
  reopens the last game in one tap. "All games" saves the game and frees it.
- Colors come from `src/ui/theme.cpp` palettes (Light/Dark), never
  hard-coded elsewhere.
- Game clocks count only while the game screen is up AND there was a touch
  in the last 2 minutes. Times feed the stats, so nothing may count
  unattended time.
- Solve/win flash toggles the panel invert bit, no overlay animations.

## Sudoku (ported from CYD-Sudoku v1.0.0 - keep its behavior)
- Files: `sudoku_core` (grid, solver, generator), `sudoku_grader`,
  `sudoku_game` (rules, undo, save), `sudoku_stats`, `sudoku_board_view`,
  `sudoku_screen`, `sudoku_app`, `sudoku_stock` (device only). Namespaces
  `sudoku`, `sudoku::stats`, `sudoku_ui`, `sudoku_app`.
- Difficulty = hardest technique needed (L1 singles, L2 locked candidates,
  L3 pairs/triples/X-Wing, L4 Swordfish/XY/XYZ-Wing). `generate()` must
  return a puzzle that grades exactly its level; host tests enforce it.
  Never go back to clue counts. Calibration tool: `tools/grader_check/`.
- Puzzle stock: 2 per level on a core-0 idle-priority task while Sudoku is
  open (`/games/sudoku_stock.bin`).
- Save format 'SUD2' (adds hints); 'SUD1' still loads. Input mode (Digit
  1st default) lives in the shared settings file. Hint = first tap points,
  second tap fills. Brightness slider floor `kMinBrightness`.
- The PC preview stages the same screens as CYD-Sudoku's; game, notes,
  hint, stats and solved renders matched v1.0.0 pixel for pixel at the port.

## Multiplayer (CYD to CYD)
- ESP-NOW, peer to peer, no router or password. WiFi radio is OFF except
  while the player is in "Play nearby" or a network game.
- Each board has a player name (set in settings, default from its MAC).
- Before a game both boards exchange protocol version + firmware version;
  mismatches get a plain message, no game.
- Messages are tiny, sequence-numbered and acknowledged; each board checks
  every received move against its own rules engine and never trusts the
  other side. A dropped link pauses the game and lets either player resume
  or end it; the game is saved on both boards.
- Internet play is out of scope (needs a server).

## Known hardware issues (from CYD-Sudoku - all still apply)
- Supported boards: 2.8" ESP32-2432S028 in ILI9341 and ST7789 versions
  (`src/boards/esp32_2432s028.hpp`), and "ESP32-32E" display boards 3.2"
  ST7789, 3.5" ST7796, 4.0" ST7796, all resistive
  (`src/boards/esp32_32e_display.hpp`).
- Tom owns the 2.8" ST7789, 3.2" and 4.0" only. The 2.8" ILI9341 and 3.5"
  ST7796 carry `custom_board_tested = no` until someone reports them working.
- The ESP32-32E boards have no model number on the PCB - only text like
  "3.2" LCD Display, ESP32-32E, 240x320, Resistive Touch". They are NOT
  ESP32-3248S0xx boards; never use that pinout. Datasheets on lcdwiki.com.
- 2.8" ESP32-2432S028: touch (XPT2046) on VSPI pins 25/32/39/33; SD also
  VSPI on 18/19/23/5. LovyanGFX's XPT2046 driver has no software SPI, so
  SD on this board needs a bit-banged touch driver first
  (`BOARD_SD_USABLE 0`; stats go to LittleFS).
- ESP32-32E boards: touch shares the display's HSPI bus (CS 33); SD is on
  VSPI by itself and works (`BOARD_SD_USABLE 1`).
- Touch filter (fixed a first-tap offset on the 4.0"): a press starts only
  after two consecutive readings agree within 8 px and ends after two empty
  readings; ESP32-32E touch clock 1 MHz. Keep both. Diagnostic: Display &
  touch > Touch test; `-D CYD_TOUCH_DEBUG` logs raw touches to serial.
- Panel inversion / red-blue order differ between production runs. Fixed
  per unit on the device (`src/hal/panel_prefs.*`), not with build flags.
- Before the screen comes up, quiet the RGB LED and audio amp
  (`quiet_peripherals()` in main.cpp).
- Check WiFi against the board pins when it's added: ADC2 pins can't be
  read while WiFi is on.

## Licensing and content
MIT. Only copy code from MIT/BSD/Apache/zlib/public-domain sources and keep
their headers. GPL projects (QQwing, OpenSudoku, LibreSudoku, GNU
Backgammon, most chess engines) are reference only. Update
`THIRD_PARTY_NOTICES.md` whenever code or data is brought in.
- Game data counts too: word lists, trivia questions and puzzle levels need
  a license that sits with MIT (public domain, CC0, or permissive with
  attribution). Share-alike data (e.g. CC BY-SA) needs Tom's OK first.
- No trademarked game names: "Word Guess", not Wordle; "Four in a Row"
  unless Tom decides otherwise for Connect Four. Classic public-domain games
  (chess, checkers, mancala, ...) are fine by name.

## Releases and web flasher
- Every push to main: CI runs the host tests, builds every env that has
  `custom_firmware_name`, and redeploys the web flasher (GitHub Pages) with
  that build (version `dev-<sha>`). This is how Tom gets builds for
  testing - keep it that way.
- Don't publish a release unless Tom asks for one.
- Releases: Actions tab -> Build -> "Run workflow" on main with
  `release_tag` = `vX.Y.Z` (hyphen = pre-release). The workflow creates the
  tag and a GitHub release with the merged factory `.bin` files (flash at
  0x0). Claude's sessions cannot push tags (proxy returns 403), so Claude
  releases via this dispatch (API `workflow_dispatch`), not `git push --tags`.
- Site source: `web/index.html`; assembled by `tools/make_site.py`.
