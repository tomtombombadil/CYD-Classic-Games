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
- Firmware files: `TTB-CYD-CG_<size>in_<DRIVER>_<Resistive|Capacitive>.bin`,
  e.g. `TTB-CYD-CG_3.2in_ST7789_Resistive.bin` (Tom, 2026-10-02: the prefix
  says which firmware it is; the rest says which board). No version in the
  file name (the release tag carries it), so links to the latest release
  stay stable.
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
  ~900 KB LittleFS, no OTA - the web flasher is the update path; Tom
  confirmed no OTA on 2026-10-02).

## Architecture (new for the collection)
- Each game lives in `src/games/<id>/`:
  - `<id>_core.*` - rules, move generation, AI, save format. Plain C++, no
    Arduino/LVGL, host-tested in `tools/host_tests/`.
  - `<id>_screen.*` - LVGL UI. Hardware actions go through the app shell
    (`ui::Shell` in `src/ui/shell.h`, filled in by main.cpp), so it also
    builds in `tools/preview/`.
  - `<id>_help.cpp` - How To Play pages (`CYD_HELP(<id>, pages)`, plain
    data, see `src/games/help.h`): the idea, the rules, how this game's
    screen works, levels. Shared two-player page `kHelpTwoPlayer` in
    `common/help_common.*`. Each page must fit a 240x320 screen with no
    scrolling; the preview renders every page at both sizes and prints
    `HELP OVERFLOW` if text runs into the keys. Keep it current when a
    game's rules or controls change.
  - `<id>_app.cpp` - the registry entry (`games::GameOps <id>_ops`: open,
    close, save, tick, restyle, summary for "Continue", icon) and the
    save/stats glue through the shell.
  - Device-only parts (FreeRTOS tasks, LittleFS) in their own file with a
    no-op stub in `tools/preview/preview_stubs.cpp` (e.g. `sudoku_stock.*`).
- The game list is `src/games/games.def` (one `GAME(id, "Title", Category,
  modes, "blurb")` line each). `src/games/registry.*` builds the table from
  it and `tools/make_site.py` reads it for the web page; nothing else
  hard-codes the game list. Adding a game = folder (incl. `<id>_help.cpp`)
  + `games.def` line.
- Shared game code in `src/games/common/`:
  - `two_player.*`, `puzzle_stats.*` (plain C++, host-tested): stats CSV
    formats for two-player games and solo puzzles.
  - `match.*`: the two-player controller (turns, computer on the AI task,
    pass-and-play, status line, sounds, win flash, menu, stats, saved
    `match::State` after the board). A two-player game supplies rules
    callbacks (`match::Game`) and draws its board. Reversi, Checkers and
    Chess should use it too.
  - `game_kit.*`: top bar, clock (2-minute idle pause), win flash, the
    standard menus (`menu_two_player`, `menu_solo`) and stats screens,
    drawing helpers for custom-drawn boards.
  - `ai_task.*`: one background job on a core-0 FreeRTOS task with a stop
    flag (device); the preview stub runs it at once.
- Games so far: Sudoku, Light Switch (5x5, par via GF(2) solve, two-tap
  hint), Sliding Tiles (3x3/4x4/5x5), FourConnect (bitboard negamax,
  depth 1/3/8), Tic-Tac-Toe (negamax, depth 1/2/9 = perfect), Reversi
  (depth 2/4/6, Hard solves the last 10 empties exactly - decided at the
  root only), Checkers (depth 2/5/7, ply cap 24, ai_stack 40 KB), Chess
  (perft-verified; depth 2 / 3+2 s / 6+6 s; ai_stack 32 KB; position keys
  computed, no static Zobrist tables - static RAM matters; save buffers on
  the heap; pieces = DejaVu glyphs `chess_font_30/42.c`, solid then
  outline), CYD-dle (word lists built by `tools/make_words.py` from
  `assets/words/`: ENABLE2K guesses, SCOWL-35 answers minus
  `blocklist.txt`/`answers_exclude.txt`; Easy 7 / Normal 6 / Hard must use
  hints; out of guesses = "Lost" in stats), Minesweeper (8x10/10,
  9x11/15, 10x11/20; mines placed on the first tap; generator retries until
  a logic solver clears the board - never a guess; long-press flags; no
  Restart; stats Won/Lost via `stats_solo(..., win_loss)`; puzzle CSV
  result "Lost" added), 2048 (id `twenty48` - C names can't start
  with a digit; 4x4, 2 or 4 (10 %) after each slide, play on after 2048;
  one custom-drawn object below the top bar = board + 4 diagonal tap
  zones; preview checks each zone against the rules engine; stats only
  for finished games or games that reached 2048; tile colors are a ramp
  over palette roles so custom themes recolor them), Yaht-CYD (official joker rules; boxes "Run of 4"/"Run of 5" so
  the card fits). Computer ties between equal moves are broken by a random
  seed; no deliberate blunders. Board games use `common/board8.*` (8x8
  view: tap, long-press peek, target dots).
- Shared UI in `src/ui/`: `widgets.*` (keys, hamburger, overlays, tables,
  screen metrics, `scratch_table()`: two shared heap tables for stats
  screens - never `static Table`, each costs ~1 KB of static RAM), `app_shell.cpp` (picker, game switching),
  `settings_screen.cpp` (Settings, themes + palette editor, touch test), `sound.*`, `theme.*`.
- Game picker (after the splash): "Classic Games" title bar with ☰
  (Settings), a "Continue <last game>" card (icon, title, the game's
  summary line), then the categories as a text list (Tom, 2026-10-02):
  Puzzle Games, Strategy Games, Word Games, Dice Games, Other Games, each
  with its game count. A category opens its own page of icon tiles, 2 per
  row, with a back key in the top bar. Extra tiles go on pages switched
  with big < > keys (no scrolling). `preview_paging` renders a long fake
  list.
- Only one game is alive at a time. Leaving a game saves it and frees its
  screen, AI tables and tasks (e.g. Sudoku's puzzle-stock task runs only
  while Sudoku is open).
- Shared across games: theme, sound, brightness, touch calibration, panel
  fixes (`/ui_settings.bin` format UIS2 = CYD-Sudoku's UIS1 + last game
  id; two former reserved bytes hold the next splash image and sound-off;
  `/themes.bin` custom themes, `/panel_prefs.bin`, `/touch_cal.bin`).
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
- Resistive touch: big targets; tap, never drag or swipe. Moving a piece =
  tap the piece, then tap the destination. Swipe games (2048) slide by
  tapping the board edge in that direction. 2048 (Tom, 2026-10-03): the
  board's two diagonals, extended to the screen edges, split the whole
  play area into 4 invisible tap zones (top = up, right = right, ...);
  a tap in a zone = a swipe that way.
- Long-press is not forbidden, just not preferred: never the only way to do
  something. Propose each use to Tom and ask before adding it. Approved
  (2026-10-02): Minesweeper (long-press = flag, besides the Flag toggle);
  Chess and Checkers (long-press any piece, either side, to see where it
  can move). Declined: Sudoku long-press to clear.
- No "tap again" / "are you sure" confirmations, ever. Buttons act on the
  first tap.
- Strong highlight tints with distinct hues (cheap TN panels wash out pale
  tints at an angle). No shrinking fonts to squeeze labels in.
- Each game: top bar with a 3-line hamburger menu (new game, restart,
  **How To Play** (Tom, 2026-10-03: every game; full-width key above
  Stats | Settings, opens `kit::how_to_play()`: pages with < > keys and
  Back To Menu), stats, Settings). Its LAST row, pinned to the bottom, is always
  **Exit Menu** (bottom left, primary; closes the menu) | **Exit Game**
  (bottom right; saves and frees the game, back to the picker) - Tom's
  rule, use `ui::overlay_exit_row()`. The picker reopens the last game in
  one tap.
- Title Case for all menu text: button labels, menu titles and headings,
  table headers ("Exit Menu", "Restart This Puzzle", "Pass and Play").
  Status lines and help sentences stay in sentence case. CSV stats files
  keep their existing wording (parsers depend on it).
- Colors come from `src/ui/theme.cpp` palettes (Light/Dark), never
  hard-coded elsewhere. Both default palettes are drawn from the splash
  art (Tom, 2026-10-02): Light = parchment/cream with night-sky navy ink,
  Dark = night-sky navy with cream text; accents coat gold, hat blue,
  glade teal-green, wood brown. Subtle and cohesive, never garish or
  childish. Tom plans game icons in the splash's style.
- Game clocks count only while the game screen is up AND there was a touch
  in the last 2 minutes. Times feed the stats, so nothing may count
  unattended time.
- Solve/win flash toggles the panel invert bit, no overlay animations.
- Sound: the CYD speaker connector (GPIO26 via the board's amp) plays short
  tones through `ui::sound()` (`src/ui/sound.h`, device driver
  `src/hal/speaker.*`). Settings has a **Volume slider** like Brightness
  (Tom, 2026-10-02): 0-100 %, tapping left of the track or the "Volume" label
  = 0 % = muted
  (label shows "Muted"), **default 50 %** (100 % distorts tiny speakers).
  Volume = PWM duty (amplitude (v/100)^2, 100 % = 50 % duty). Stored in
  UIS2 byte 8 as volume+1; 0 = unset -> 50 %, old "sound off" 1 -> 0 %.
  Games use the named sounds (place, move, error, win, lose...), never raw
  tones. **Go easy on sounds** (Tom): they add to the game, never narrate
  taps. Silent: every key press (picker, menus, Settings, keyboards),
  picking up a piece, holding a die, long-press peeks. Sounding: a move or
  placement, the other side's reply, mistakes, hints, game end, and one
  sample when the Volume slider is released.
- Settings (☰ → Settings, shared): [Theme | Invert Colors], Brightness
  slider, Volume slider, Swap Red/Blue, [Recalibrate | Touch Test], Back.
- Themes: Light, Dark and 3 Custom slots. A custom theme starts from Light
  or Dark and overrides 10 color roles, each picked from a 48-color palette
  (`/themes.bin`). Games take every color from `ui::pal()`, so a custom
  theme recolors every game.
- Boot: a splash image (3 designs, alternating each boot, sized per board)
  drawn straight to the panel with LovyanGFX's JPEG decoder, then wait for
  a tap, then the picker. Source art in `assets/splash/`, headers made by
  `tools/make_splash.py`.

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

## Two-player games (Tom, 2026-10-02)
- Every two-player game offers all three: vs computer (levels), pass-and-play
  (one CYD, two people take turns), and wireless CYD to CYD (below). The
  new-game menu lists them; wireless shows as "coming" until stage 5 (multiplayer).
- Tom, 2026-10-02: finish more games before building wireless play.

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
  readings; ESP32-32E touch clock 1 MHz. Keep both. Diagnostic: Settings >
  Touch test; `-D CYD_TOUCH_DEBUG` logs raw touches to serial.
- Panel inversion / red-blue order differ between production runs. Fixed
  per unit on the device (`src/hal/panel_prefs.*`), not with build flags.
- Before the screen comes up, quiet the RGB LED and audio amp
  (`quiet_peripherals()` in main.cpp).
- Speaker: `BOARD_PIN_SPEAKER` 26 on both board families (2.8": straight
  to its amp; ESP32-32E: DAC pin into the amp, enabled by
  `BOARD_PIN_AUDIO_EN` 4, low = on; the driver turns the amp on only while
  a sound plays). LEDC square wave, 50 % duty. Not yet heard on hardware.
- `BOARD_PORTRAIT_W` (240/320) in board_select.h picks which splash images
  are built in.
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
- No trademarked game names. Tom's names: **FourConnect** (Connect Four),
  **CYD-dle** (Wordle), **Yaht-CYD** (Yahtzee), **Light Switch** (Lights
  Out), **MasterCYD** (Mastermind), **SokoCYD** (Sokoban), **KenCYD**
  (KenKen), **You Sunk My CYD!** (Battleship), **Wheel of CYD** (Wheel of
  Fortune), **Reversi** (Othello). Classic public-domain games (chess,
  checkers, mancala, ...) are fine by name.

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
