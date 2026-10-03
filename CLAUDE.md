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
    game's rules or controls change. The texts live in
    `tools/make_help.py` (run it to regenerate every `<id>_help.cpp`):
    edit there, not in the .cpp.
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
  - `cards.*` + `cards/card_font_*.c`: playing cards for every card game
    (Tom, 2026-10-03): a face is just the rank and a suit, as large as fit -
    index strip at the top (rank, suit right after it, sized for "10" so all
    match; this is what shows in cascades and fans) and one big suit below.
    Faces cream in every theme, red = `piece_a`, black = `stone_dark`,
    table = `cards::felt()` (the theme's felt 15 % darker - Tom), selected
    = amber edge + gold-tinted face (Tom: the pick must be obvious). 12
    backs (Tom, 2026-10-03): Bombadil (a soft gold DejaVu Serif Bold "B"
    with a dark outline on blue, like the splash title), Moon (a crescent
    on black, one star in the top right corner), Tree (green
    crown, brown trunk), then Lattice / Stripes / Dots in blue, red, green.
    Default back: Blue Lattice (Tom). Card games make no tap/move/hint
    sounds (Tom); an invalid move plays `Sound::Error` (Tom, 2026-10-03:
    OK if not annoying - a two-tone high-then-low "aww", 660 then 440 Hz);
    a won solitaire-type game plays `Sound::Fanfare`, a playful tune.
    Keep pictures simple: small cards. The player's back is shared by every
    card game (`UiSettings::card_back`, UIS2 byte 9 = back + 1);
    `cards::back_screen()` is the picker (Options -> Card Back). Memory
    Match uses it too. `cards::celebrate()` = the win show: cards leave the
    foundations and bounce off the screen leaving trails, Windows-style;
    only the flying card's rect is invalidated each frame, so trails cost
    no RAM (the preview's `shot(..., keep=true)` keeps them). Fonts from
    DejaVu via lv_font_conv (ranks Sans Bold - clearer than Condensed on
    the 2.8", Tom; suits Sans). Index strip 36 % of the card, rank as big
    as fits ("10" drawn tight), suit a size smaller; sizes cached per card
    size.
    `tools/preview/card_mockups.cpp` renders table mockups (Klondike,
    FreeCell, Blackjack, video poker).
  - `ai_task.*`: one background job on a core-0 FreeRTOS task with a stop
    flag (device); the preview stub runs it at once. The task runs at
    IDLE priority: at priority 1 a search over 5 s starved core 0's idle
    task and the task watchdog rebooted the board (Tom's 2.8", Solitaire
    "Shuffling..."). Keep it there. It logs a job's unused stack when
    under 1/8 ("AI stack tight") - raise that job's stack if seen.
  - Screens with a clock must not redraw the board each second: a
    `ticking` flag makes the clock tick update only the top bar (a
    full-table redraw every second made Solitaire taps feel sluggish).
- Games so far: Sudoku, Light Switch (5x5, par via GF(2) solve, two-tap
  hint), Sliding Tiles (3x3/4x4/5x5), FourConnect (bitboard negamax,
  depth 1/3/8), Tic-Tac-Toe (negamax, depth 1/2/9 = perfect), Reversi
  (depth 2/4/6, Hard solves the last 10 empties exactly - decided at the
  root only), Checkers (depth 2/5/7, ply cap 24, ai_stack 40 KB), Chess
  (perft-verified; depth 2 / 3+2 s / 6+6 s; ai_stack 32 KB; position keys
  computed, no static Zobrist tables - static RAM matters; save buffers on
  the heap; pieces = glyphs in `chess_font_33/46.c`, solid then lines:
  Knight and Rook from DejaVu (Tom likes them), King, Queen, Bishop, Pawn
  our own from `tools/make_chess_font.py` -> `assets/chess/CYDChessPieces.ttf`
  (Tom, 2026-10-03: King and Queen must be obvious - King tallest with a
  cross, Queen a five-ball crown, Bishop mitre + slit, Pawn short); black
  pieces use thinner light lines (U+E000..) so they read as black; the
  glyph is centred on its shape, not the line box; glyph left side
  bearings must equal xMin or lv_font_conv shifts them off-centre), CYD-dle (word lists built by `tools/make_words.py` from
  `assets/words/`: ENABLE2K guesses, SCOWL-35 answers minus
  `blocklist.txt`/`answers_exclude.txt`; Easy 7 / Normal 6 / Hard must use
  hints; out of guesses = "Lost" in stats), Minesweeper (8x10/10,
  9x11/15, 10x11/20; mines placed on the first tap; generator retries until
  a logic solver clears the board - never a guess; long-press flags; no
  Restart; stats Won/Lost via `stats_solo(..., win_loss)`; puzzle CSV
  result "Lost" added), MasterCYD (4 pegs no repeats / 4 / 5 pegs,
  6 colors from the palette (red, yellow, green, blue, white, black, each
  with a rim), 10 guesses; answers are shapes - filled dot = exact, ring =
  near - in one line), Peg Solitaire (id `pegs`; Triangle 15 / English
  33 / European 37 on a 7x7 grid; European starts with hole 10 empty -
  the centre start is unsolvable; tests replay stored one-peg solutions
  found offline (a live search is far too slow under sanitizers);
  unlimited undo; stuck = Lost only when you start another game),
  Memory Match (id `memory`; 4x4 / 4x5 / 5x6, LVGL symbols as pictures,
  each with a color too; backs = shared Starry Night card back; a miss
  stays up until the next tap - no timers), Nonograms (id `nonogram`; 5x5 / 8x8 / 10x10;
  random left-right mirrored pictures kept only when a line solver solves
  them - unique, no guessing; Fill | Mark modes, no long-press; clue turns
  grey when its line matches; fill/mark taps silent), Solitaire (Klondike, called Solitaire - Tom; Options
  menu key -> Draw 1 / Draw 3 (default 3; applies to the deal in play at
  once - Tom expected that), Standard / Vegas (balance carries over) /
  None scoring (from the next deal), Card Back; tap card then destination, tap
  the picked card again = best target; auto-finish; Undo + Hint keys;
  stats per draw mode, an unfinished deal = Lost; the win show; only
  winnable deals (Tom: fun for kids on a car ride): `solitaire_solve.*`
  searches each candidate deal seeing every card (20k positions, safe
  foundation moves forced, a 16 KB lossy position table, moves in a heap
  pool, at most `kMaxTries` = 300 candidates) and only proven
  wins are dealt; the next deal is found in the background on the AI task
  (20 KB stack) while one is played, else "Shuffling..."; foundations
  fixed Spades, Hearts, Clubs, Diamonds left to right with their suit on
  empty piles, and the whole foundation row is one drop target; a second
  tap (double tap) on the picked card sends it to its foundation), Spider
  (1 / 2 / 4 suits; 10 columns of 21 px cards on 240 wide - card_font_10;
  Windows scoring 500 -1/move +100/run; no deal with an empty column),
  Pyramid (pairs to 13, Kings alone, waste top pairs too, 3 passes),
  Golf (no wrap, nothing on a King; Hint; cards left = score), FreeCell
  (supermoves (cells+1) x 2^empty, safe cards go up by themselves with
  exact undo, foundations S H C D, free-cell row / foundation row are
  whole-row drop targets; random deals - nearly all winnable), Blackjack
  (you vs the house only; 6-deck shoe reshuffled at 3/4, dealer stands on
  all 17s and peeks, 3:2, double any two, one split, split Aces one card;
  500 chips, New Chips +500 when broke; stats per hand CSV
  "#,Bet,Result,Net,Chips"; dealer's cards revealed one at a time;
  once all are shown (Tom, 2026-10-03): Fanfare on a blackjack,
  `Sound::Trill` (a happy little victory trill) when the round nets a win,
  `Sound::Error` (the "aww") when it nets a loss, silence on a push). Landscape (decided 2026-10-03): Solitaire
  stays portrait (Tom agreed: too short for late-game columns, mockup
  `card_mockups` case 5); Tom OK'd landscape for card games where it is
  truly better - none is on these 3:4 screens (Golf/Pyramid would gain
  ~10-15 % card size only), so all stay portrait and no runtime rotation
  switch was built. Card
  games keep their undo history only while open (not in the save). A
  stuck deal is recorded as Lost only when the next deal starts (Undo can
  still save it). Golf/Pyramid/Spider menus: "Card Back" key -> the
  shared back screen, 2048 (id `twenty48` - C names can't start
  with a digit; 4x4, 2 or 4 (10 %) after each slide, play on after 2048;
  one custom-drawn object below the top bar = board + 4 diagonal tap
  zones; preview checks each zone against the rules engine; stats only
  for finished games or games that reached 2048; tile colors are a ramp
  over palette roles so custom themes recolor them), Yaht-CYD (official joker rules; boxes "Run of 4"/"Run of 5" so
  the card fits; used boxes filled solid `frame` blue - Tom: tell them
  from open ones at a glance), RPG Dice (id `rpgdice`, Dice category -
  Tom, 2026-10-03: "not a game but fits"; Coin d4 d6 d8 d10 d12 d20 d100;
  tap die keys to build a pool + one modifier (-1/+1), Roll repeats it,
  the next die tap after a roll starts a new pool; the tray (felt) draws
  each die as its shape with its number - d4 triangle, d6 with pips, d8
  diamond, d10 kite, d12 pentagon, d20 hexagon with a light front face,
  d100 = two d10s (tens "40" + ones), coin H/T - and the total; natural
  20 gold ring + Trill, natural 1 red ring + "aww"; Presets: 8 slots,
  each a name (on-screen keyboard, auto-capitalised words) and up to 4
  lines of label (Roll/Hit/Damage/Save/Check/Init/Heal) + pool + modifier,
  rolled with one tap, one tray row per line; a sample "Fighter Attacks"
  preset on first open; History: 40 rolls as text, newest first, paged;
  save "RPD1" ~5 KB, written 15 s after a change and on close). Computer ties between equal moves are broken by a random
  seed; no deliberate blunders. Board games use `common/board8.*` (8x8
  view: tap, long-press peek, target dots). Checkers: when a jump is
  compulsory (American rules - yes, a jump must be taken; Tom checked and
  it was right) the pieces that can jump light up (warn tint) with the
  "A jump must be taken" note. board8 taps (Tom, 2026-10-03:
  Chess taps were "very bad"): a tap acts on release (CLICKED, any
  length) at the square where the stylus came DOWN (lift-off readings
  drift); long-press = own 750 ms timer (LVGL's 400 ms turned firm taps
  into peeks, and the old peek then ate the next tap too); the peek shows
  only while held and the game restores its previous pick on release.
- Shared UI in `src/ui/`: `widgets.*` (keys, hamburger, overlays, tables,
  screen metrics, `scratch_table()`: two shared heap tables for stats
  screens - never `static Table`, each costs ~1 KB of static RAM), `app_shell.cpp` (picker, game switching),
  `settings_screen.cpp` (Settings, themes + palette editor, touch test), `sound.*`, `theme.*`.
- Game picker (after the splash): "Classic Games" title bar with ☰
  (Settings), a "Continue <last game>" card (icon, title, the game's
  summary line), then the categories as a text list (Tom, 2026-10-02):
  Puzzle Games, Strategy Games, Card Games (added 2026-10-03 for the card
  games), Word Games, Dice Games (Other Games dropped - Tom, 2026-10-03), each
  with its game count. A category opens its own page of icon tiles, 2 per
  row, with a back key in the top bar. Extra tiles go on pages switched
  with big < > keys (no scrolling). `preview_paging` renders a long fake
  list.
- Only one game is alive at a time. Leaving a game saves it and frees its
  screen, AI tables and tasks (e.g. Sudoku's puzzle-stock task runs only
  while Sudoku is open).
- Shared across games: theme, sound, brightness, touch calibration, panel
  fixes (`/ui_settings.bin` format UIS2 = CYD-Sudoku's UIS1 + last game
  id; former reserved bytes hold the next splash image, volume, card back
  and flags (bit 0 = screen turned 180);
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
  slider, Volume slider, Swap Red/Blue, Rotate Screen 180 (Tom,
  2026-10-03: USB cord out either end; a toggle, lit while on; UIS2
  `flags` bit 0, applied before the splash; `lvgl_port_set_rotation()`
  turns panel + touch at run time, calibration is rotation-independent),
  [Recalibrate | Diagnostics], Back. Board/firmware line moved to
  Diagnostics (no room).
  Diagnostics (`src/ui/diagnostics_screen.cpp`): [Touch Test | Device
  Log], [Send Log], board, firmware (version + build commit), free memory,
  uptime, Back. Device Log: paged
  with < > (opens on the newest page), [Clear Log | Copy To SD], [< Back >].
- Device log (Tom, 2026-10-03: "we need a way to pull logs"):
  `src/app/device_log.*` (device only). `/log.txt` + `/log.old` on
  LittleFS, 8 KB each, every line also to serial (115200). Each boot logs
  firmware, board, reset reason (power on / crash / watchdog / brownout)
  and memory. A crash handler (`set_arduino_panic_handler`; needs
  `-Wl,--wrap=esp_panic_handler` in platformio.ini - PlatformIO doesn't
  add it) saves reason, task, PC, 8 backtrace addresses and the last step
  in RTC memory; the next boot writes them to the log. Games log through
  the shell: `ui::log_event()` (written to the file - rare events: game
  opened, search failed) and `ui::log_step()` (serial + "last step" only -
  frequent events). Other tasks' lines are queued for the main loop.
  Copy To SD writes `/CYD-Classic-Games/log.txt`; boards with a usable SD
  slot also copy it there at every boot and on Send Log (Tom: always, when
  a card is in).
  **Send Log** (Tom, 2026-10-03, "BRILLIANT"): the newest log lines that
  fit, plus a header (board, firmware), go into one QR code:
  `log_pack.*` (plain C++, host-tested) = raw DEFLATE (fixed Huffman, 4 KB
  window, ~6x on logs) + base43 (QR alphanumeric set minus space and %,
  2 bytes -> 3 chars); QR = byte segment `logpack::kUrl`
  (`https://tomtombombadil.github.io/CYD-Classic-Games/l/#`) + alphanumeric
  segment, ECC L, encoder = LVGL's bundled Nayuki qrcodegen
  (`LV_USE_QRCODE 1`), modules >= 2 px (240 wide: ~150 lines / 5 KB of
  text; 320 wide: ~380 lines / 12 KB). The page `web/l/index.html` decodes
  it (DecompressionStream "deflate-raw") and offers Email (mailto to
  **cyd.classic.games.logs@gmail.com** - Tom's address for logs), Copy,
  Download log.txt. The same page reads the whole log over USB (Web
  Serial, Chrome/Edge): it sends "log\n"; the firmware (`serial_commands()`
  in main.cpp) prints it between "---- device log ----" and
  "---- end of log ----". The flasher page links to it. CI keeps each build's
  `firmware.elf` (artifact `elf-<env>`, 90 days; releases attach
  `debug-symbols.zip`) to decode backtraces with addr2line.
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
  Diagnostics > Touch Test; `-D CYD_TOUCH_DEBUG` logs raw touches to serial.
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

## Versions (Tom, 2026-10-03)
- Semantic versioning, `vX.Y.Z`, from the `VERSION` file (just `0.9.0`).
  A fix or small change bumps Z, a new feature (game, setting, screen)
  bumps Y, a major revision bumps X. **Bump VERSION in every push to main
  that changes the firmware** - each push is a build Tom flashes, so each
  gets its own number. Started at 0.9.0 (2026-10-03); 1.0.0 is Tom's call.
- `tools/version.py` (PlatformIO pre-script) defines `CYD_GAMES_VERSION`
  ("v0.9.0") and `CYD_GAMES_BUILD` (git commit) for every build, local or
  CI. The picker title shows "Classic Games v0.9.0"; Diagnostics, the
  log and the flasher show the version (log/Diagnostics add the commit).

## Releases and web flasher
- Every push to main: CI runs the host tests, builds every env that has
  `custom_firmware_name`, and redeploys the web flasher (GitHub Pages) with
  that build (version = VERSION). This is how Tom gets builds for
  testing - keep it that way.
- Don't publish a release unless Tom asks for one.
- Releases: Actions tab -> Build -> "Run workflow" on main with
  `release_tag` = `v` + VERSION (a `-beta.1` style suffix = pre-release;
  CI refuses any other tag). The workflow creates the
  tag and a GitHub release with the merged factory `.bin` files (flash at
  0x0). Claude's sessions cannot push tags (proxy returns 403), so Claude
  releases via this dispatch (API `workflow_dispatch`), not `git push --tags`.
- Site source: `web/index.html`; assembled by `tools/make_site.py`.
