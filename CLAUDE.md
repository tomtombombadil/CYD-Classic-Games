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

## HARD RULE: no free-form communication between players (Tom, 2026-10-04)
Tom works in K-12 education; the boards may be used by children, and laws
and school rules restrict children communicating with strangers. **Nothing
a player writes or says may ever travel from one board to another.** What
crosses the air is limited to fixed, coded fields: board presence and
version, the games offered, play requests and their fixed answers (Play,
No Thanks, Other Game + a game key), moves, and game/session state. Never
add chat, messages, emoji/stickers, free-text fields, drawings, voice, or
any other channel a player can fill - not even as a hidden or debug
feature, and not "just for testing". Any new packet field must be a code
from a fixed set defined in the firmware. If a feature would need free
text on the air, it doesn't get built. Player names are two words picked
from fixed, kid-safe lists (Random / Pick From List), sent as numbers -
never typed (SPEC section 5).

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
  <out>` builds `preview` (picker + game screens at any size; `--agent` =
  one board for `duo.py`) and `preview_paging`; `tools/preview/readme_shots.py` turns renders into
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
  `Sound::Error` (the "aww") when it nets a loss, silence on a push),
  Video Poker (id `vpoker`; Jacks or Better, full-pay 9/6 table, Royal
  4000 at 5 credits; bet 1-5, Bet Max deals at once; fresh deck each
  hand; tap a card's column to hold (gold face + HELD); Hint = the
  standard simple strategy (matches exact best play on sampled hands;
  hand frequencies match published figures); 500 credits, New Credits
  +500; pay table in two columns with the held/final hand lit; stats CSV
  "#,Bet,Hand,Win,Credits"; win = Trill, Four of a Kind and up = Fanfare +
  flash, a losing hand is silent - most hands lose), Texas Hold'em (id
  `holdem`; no limit, you + Ada, Max, Zoe; 1000 chips, blinds 5/10,
  button moves; side pots by contribution level, odd chips left of the
  button; computer = Monte Carlo equity (60 / 200 / 500 deals by level,
  never sees your cards) discounted when facing a bet, vs pot odds, with
  per-seat styles (steady / tight / pushy) - tuned so ~54 % of hands see
  a flop, ~40 % a showdown, pots ~12 big blinds; it thinks on the AI task
  (12 KB) and acts every ~0.65 s; keys Fold, Check/Call, Bet-Raise (min),
  Pot, All In / Next Hand, New Chips +1000; computer players buy back in;
  your best hand so far shows by your cards; stats CSV
  "#,Result,Net,Chips" (Won/Lost/Folded/Split); win = Trill, a lost
  showdown = "aww", folds silent). Landscape (decided 2026-10-03): Solitaire
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
  from open ones at a glance), Farkle (id `farkle`; you vs computer or
  pass-and-play (+ wireless later), to 10,000 with a last turn for the
  other player; scoring 1=100, 5=50, triples 100x (1s 1000), 4/5/6 of a
  kind 1000/2000/3000, straight / three pairs / 4+pair 1500, two triples
  2500; dice count only in their own roll; hot dice; tap dice to set aside
  (gold), Roll N Dice / Bank N keys; a Farkle tints the dice red and
  "Pass the Dice"; computer Easy banks at 300, Medium by dice left, Hard
  weighs Farkle odds and the scores (Hard beats Easy ~60 %); picks silent,
  a roll = Move, a bank = Place, Farkle = "aww", game end Win / Lose), RPG Dice (id `rpgdice`, Dice category -
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
  save "RPD1" ~5 KB, written 15 s after a change and on close). Mancala (id `mancala`; Kalah 6 pits x 4 seeds, Strategy; vs
  computer / pass-and-play / wireless later; sides Gold and Blue;
  portrait board: the left side's pits down the left column, stores in
  the middle (left side's at the bottom), the other side's pits up the
  right - sowing runs round that loop; vs computer the board turns so the
  player's pits are on the stylus hand's side (was: always left), pass-and-
  play puts Gold there; each move is shown seed by seed (130 ms a seed,
  taps wait); capture = last seed in an empty own pit with seeds opposite
  (both go to the store); a side out of seeds ends it, the other sweeps
  its own; computer: Easy 1 move ahead, Medium 5, Hard deepening within
  400k nodes (Hard beats Medium every test game); note line "You go
  again" / "Captured N seeds"). Nine Men's Morris (id `morris`, Strategy; White/Black, White first;
  points 0-23 on a 7x7 grid (`kX/kY`), 16 mills; place 9 each, then move
  along lines, fly at 3; a move is one code (from+1 | to<<5 |
  (remove+1)<<10) so the mill's capture is part of it - the screen asks
  for the man to take after a mill (red rings on the takeable men, tap the
  new man again to take the move back); men in mills safe unless all are;
  2 men or blocked = loss; 100 quiet plies after placing = draw; computer
  Easy 1 ply, Medium 3, Hard deepening within 150k nodes, removals
  searched first, move lists on the stack - ai_stack 24 KB). You Sunk My
  CYD! (id `sank`, Strategy; Battleship - Tom's name is "Sank" (2026-10-05: the classic ad line "You
  sank my battleship!"); the id was `sunk` until v0.27.2 (renamed
  everywhere before any public release - Tom; wireless key 8 unchanged); 10x10, Carrier
  5, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2; classic rules (Tom,
  2026-10-05): ships may touch, only not overlap, and nothing is "known"
  round a sunk ship (v1's spacing rule made it too easy - never bring it
  back); a hit doesn't fire again; placement (Tom, 2026-10-05) Options ->
  "Ship Placement" Random (o) Manual, default Manual (`/games/sank_opt.bin`
  "SKO1"; changing it restarts the fleet being placed): Manual = biggest
  ship first, tap one end (gold), the squares it can point to light up,
  tap one = placed (Tom, 2026-10-05: aimed at a nearby edge the ship
  slides in so its end is against the edge - that way lights only the
  squares up to the edge); Undo; Ready when all 5 are in. Random = Shuffle / Ready.
  A ship is a move: plies 0-4 side 0's ships, 5-9 side 1's
  (`ship_key` = cell | down << 7), then shots = cell 0-99, side 0 first
  (wireless version 2; v1 sent a seed a fleet; "SNK1" saves still load
  via `make_fleet()`); Ready sends the 5 ships as moves at once, and may
  be tapped before that side's turn - they go when it comes; the
  computer places a ship a ply anywhere it fits; one sea on screen, Their Waters | My Fleet keys in
  the Play Again row; row numbers on the side away from the hand;
  pass-and-play: a cover "Pass the board to Gold" + Ready after each
  fleet, and after a shot its result then "Pass to Gold"; at the end every
  ship shows and a tap on the sea flips the pages; computer Easy random +
  round a hit, Medium parity + lines, Hard placement counting (~59 / 44 /
  39 shots a fleet); the computer reads only what its side knows (hits,
  misses, sunk); the show (Tom, 2026-10-05: "the anticipation of the hit
  or miss" - it was boring): ships drawn from above (v0.27.1, Tom: the
  first ones "look pretty bad"): a hull outline (dark edge, curved pointed
  bow, rounded-off stern - `hull_half()`), drawn as a solid core of
  rectangles then smooth triangle strips on top (strips alone showed
  seams, rectangles alone jagged bows); grey hull, lighter deck, bridge
  block with a window line, funnel, twin-barrel turrets (Battleship 3,
  Cruiser 2, Destroyer 1); Carrier = dark flight deck, dashed centre line,
  angled landing line, island to one side; Submarine = dark slim hull,
  rounded nose, tapered tail with stern planes, a sail amidships; bow at
  the far end; sunk = the same ship as a dark red wreck; at the end the
  unfound ones faded; a shot = 0.6 s fall (sights + a shrinking shell,
  `Sound::Whistle` 0.6 s, info line "Firing at A1..." - no spoiler), then a
  splash (`Sound::Splash`) or an explosion (`Sound::Boom`) with a big
  banner "B5 MISS!" / "C6 HIT!" (+ "You sank their Cruiser!" / "Computer
  fired") held 1.0 / 1.25 / 1.75 s (sunk), clear of the shot (Tom: a
  third faster than the first 0.9 s fall + 1.5 / 1.9 / 2.6 s); hits are red
  bursts, misses white pegs; the board already holds the result
  (`match::Game::busy` holds the computer and wireless moves meanwhile);
  then the view turns by itself: Their Waters for your shot, My Fleet
  while they "aim" 0.6 s ("Computer is aiming...") and fire; the last
  shot lands at once (the win / lose sounds); pass-and-play shows the
  shot on the shooter's view, then Pass. The keys still switch views. A wireless board knows
  the other fleet's seed - the screen never shows it before the end). Wheel
  of CYD (id `wheel`, Word; Wheel of Fortune; you vs computer players Max
  and Zoe, or 2-player pass-and-play (menu_two_player, no wireless); 3
  rounds, round r starts with player r; wheel of 16 wedges 300-1000 + BUST
  (round money lost) + SKIP; consonant pays value x count and keeps the
  turn; vowel $250; solve = tap letters into the blanks in reading order
  (only uncalled letters), Delete / Cancel / Solve; a call showing the last
  letter solves it; a solve banks round money, at least $500; most banked
  wins; ties = Draw in the two-player stats CSV (Side1 = you / Player 1);
  the wheel fills the screen while it turns (3.5-7 s, 1.1 s hold)
  and is decided by the core first (the save never waits on it); letters
  reveal one tile at a time; puzzles `assets/wheel/phrases.txt` ->
  `tools/make_phrases.py` (checks A-Z ' - & ., words <= 12, 4 rows of 12,
  no repeats) -> `wheel_phrases.cpp`: 502 kid-safe originals in 11
  categories, no brands - never avoid-listed words (Tom's names-review
  words, e.g. Monkey / Raccoon / Beaver / Woodpecker, stay out); a played
  bit per puzzle in the save (1024 max) so they don't repeat; computers
  never read the answer: they see the board and called letters, solve with
  a puzzle from the list that fits (so a guess can be wrong), at 30 / 38 /
  45 % hidden (+-10 %), Hard also at 55 % when only one puzzle fits and
  calls the letter most fitting puzzles have; Easy picks among the 8 most
  common letters, Medium the most common; sounds: your letter found Place,
  not found "aww", BUST "aww", a computer's find Turn, your solve Trill,
  game Win / Lose; log (v0.26.1, Tom's 3.5": a Spin seemed to pass straight
  to Max - not reproduced in 12 simulated games): each of your spins
  ("spun BUST/SKIP/money", how often the wheel was drawn and the longest
  gap), letters not in the puzzle, wrong solves; key taps to log_step;
  Tom then saw NO wheel at all on the 3.5" - v0.26.2 draws wedges as
  triangle fans, not thick arcs, invalidates only the wheel's square, and
  a spin ends only after its last position was drawn + 1.1 s (or 10 s
  late), so the result is always seen; v0.27.3, Tom: the spin "flashes
  more than spins" - at ~10-20 frames a second a fast wheel jumped
  several wedges a frame (wagon-wheel effect). v0.27.3's motion blur:
  Tom "not a fan" - never blur it. v0.27.5: the spin is a show (the
  result is decided first), planned so the wheel never turns more than
  7 deg (a third of a wedge) between frames: a short push (10 %), a
  steady turn (25 %), an even slow-down like friction (65 %); distance =
  whole turns as far as that allows in ~4.5 s at the measured frame time
  (`frame_ms`, kept between spins), 3.5-7 s. v0.27.6, Tom: that looked
  like a rolling shutter - the panel gets each frame top to bottom over
  ~25 ms, so a round wheel's top showed a newer angle than its bottom; no
  TE pin to sync to. So the wheel lies flat like on TV: an ellipse (height
  42 % of width) with a front edge in darkened wedge colours, a gold hub,
  the pointer at the front, the value under it in big type above (redrawn
  only when it changes) - under half the rows, so under half the slant;
  never draw it as a round face-on wheel again (Tom, 2026-10-06: the
  slant is less but still there - paused, come back to it later); v0.27.4: the core passes
  the turn on BUST / SKIP the moment the spin is decided, so while the
  wheel turns the header and the gold player box show the spinner
  (`shown_turn()`), not `g.turn` - Tom saw Max named during his spin), Ultimate Tic-Tac-Toe (id `ultimate`, Strategy; X / O,
  X first anywhere; the square played sends the other player to that
  board, a won or full board = play anywhere; a full board counts for
  nobody, all boards done without three = draw; move = cell 0-80 (board *
  9 + square), wireless key 9; lit = the boards you may play in, a won
  board gets one big mark, a full one greys, last mark on a gold square;
  note "Play in the top right board"; computer negamax with a lines eval,
  Easy depth 2, Medium 4, Hard deepening within 40k nodes (~1-2 s on the
  board; beats Easy 15/16), ai_stack 12 KB), Gomoku (id `gomoku`,
  Strategy; 15x15, Black first, freestyle: five or more wins, full board
  = draw; points on line crossings, a tap = the nearest point where the
  stylus came down; move = point 0-224, wireless key 10; computer: five-
  point windows scored by stones in them, kept incrementally with "fours"
  (a window one short of five); Easy greedy, mostly its own lines (blocks
  a five), Medium depth 4 on the 5 best points, Hard depth 6 on the 7
  best (within 200k nodes); all take a five and block one; odd depths
  played worse (horizon) - keep them even), Pipe Race (id `piperace`,
  Puzzles; Tom's name for Pipe Mania, 2026-10-06; 8x8, a tank inside the
  edges, queue of 5 (2 of each straight / bend, 1 cross in 13), tap empty =
  lay, tap an unreached pipe = swap -50; countdown 25 s - 1.5 s a level
  (min 10), 3.2 s a square x 0.88 a level (min 0.7), half for the tank;
  goal 10 + 2 a level (max 26); rocks from level 3 (level - 2, max 8);
  100 a pipe, 200 fast, +400 a cross crossed both ways, -50 a laid pipe
  never reached; one key: Water Now / Fast Flow / Next Level / Play
  Again; water only runs with the game on screen and no overlay, a game
  reopened (or restyled mid-flow) starts paused - Continue; each frame
  invalidates only the water's square; Place / Move / Turn (water starts)
  / Trill (level) / Lose; stats "#,Score,Level,Pipes,Seconds,Time", a game
  left after level 1 is recorded), Acquisitions (id `acquisitions`,
  Strategy, kVsComputer; Tom's name for Acquire, 2026-10-06; you vs Ada,
  Max, Zoe; full classic rules: 108 tiles 1A-12I, $6000, 6 in hand, found
  (free share) / grow / merge (bonuses 10x / 5x price, ties share rounded
  up to $100, sell / trade 2:1 / keep from the merging player round),
  safe at 11, dead tiles swapped automatically, an eighth chain waits, end
  callable at 41 or all safe (or no tile left); hotels Sunrise, Oakwood
  (cheap), Harbor, Meadow, Lagoon (middle), Royal, Crimson (dear), colours
  from palette roles + their letter on every tile; portrait: board on top
  (your playable tiles edged gold, last tile ringed), 7 hotel chips (tap
  to buy while buying, else the Stocks page), a line of what just
  happened, hand tiles / Undo | End Game | Done at the bottom; Found /
  Survivor / Dispose pages for your choices; computers one step every
  0.3-0.9 s, deciding only from the table (Hard: net worth + majority
  stakes - 0.3 x best rival, beat 3 Easy 36/40); Place / Turn / Trill
  (your bonus) / Error / Win / Lose; stats "#,Place,Money,Level,Seconds,
  Time", a game left after a round counts at your place then), Strategy
  Go! (id `strategygo`, Strategy; Tom's name for Stratego, 2026-10-06;
  Red / Blue, Red first; 10x10, lakes C-D / G-H rows 5-6; 40 a side
  (10 Marshal ... 2 Scout, Spy, 6 Bombs, Flag); Scouts any distance and
  may strike, a long Scout move shows it; Spy beats Marshal only
  striking, Miner defuses, equal both go; flag taken or no move = loss,
  2000 plies = draw; two-squares rule 5; moves: plies 0-39 Red's setup,
  40-79 Blue's (`setup_key` = cell | rank << 7), then from | to << 7,
  wireless key 11; setup = a sensible random army (Flag on the back row
  behind Bombs, Scouts forward, Miners back) - tap two to swap, Shuffle,
  Ready; `match::Game::no_pause` lets the computer lay its 40 at once;
  board drawn from the viewer's side (turned for Blue); theirs plain until
  they fight, a dot = has moved; your shown pieces get a gold corner dot;
  battle banner 1.7 s (`busy`); Pieces page = losses by rank; computer
  reads only what its side knows (seen ranks, moved, lost-by-rank pool),
  all levels one move ahead by outcome chances: Easy material, Medium +
  threats from seen pieces, Hard + from unseen ones by chance (Hard beat
  Easy 18/20; 2-ply and hunting/flag-guard terms tested - no stronger,
  dropped); sounds: battle won Trill / lost "aww" / both Draw / bomb Boom),
  Deal or No CYD (id `dealcyd`, Game Shows, solo; 26 cases $0.01 -
  $1,000,000 in cents; pick yours, open 6-5-4-3-2-1-1-1-1, the Banker's
  offer after each round = 12 / 22 ... 92 % of the average in play +-5 %,
  rounded to $10 / $100 / $1,000; last: keep or swap; screen like the
  show: amounts down both sides (grey once opened), 26 briefcases 4 wide
  in the middle (yours gold), an opened case shows its amount 1.3 s,
  then the Banker calls (Sound::Call) - Deal | No Deal keys; big amount
  gone "aww", tiny gone Trill, end Win if you got at least your case's
  amount; stats "#,Won,Deal Round,Case Held,Seconds,Time"), Press Your
  CYD (id `presscyd`, Game Shows, kVsComputer; you vs Max, Zoe; 18
  squares round a 5 x 6 ring, 3 slots each (money / money + 1 spin /
  Gremlin - our own green imp, never a Whammy), Gremlins at most one a
  square, 9 then 12 of 54 slots, 7 / 6 spin slots, round 2 pays more;
  3 then 4 spins; least money goes first and spins till out; Pass =
  your own spins left to the leader (only when someone's ahead), who
  takes them at once; passed spins can't be passed on and turn into
  your own on a Gremlin; 4 Gremlins = out; light jumps every 140 ms,
  squares change one at a time (700 ms round), only changed squares
  redrawn; result banner 1.4 s; computers stop after 1.2-3 s and pass
  by a risk rule - the game is luck (that rule won no more than never
  passing), so no levels; Place / Hint / "aww" / Turn / Win / Lose;
  stats "#,Place,Money,Seconds,Time"), Card Sharks CYD (id `cardsharks`,
  Game Shows, kVsComputer | kPassAndPlay via match.*, sides Gold / Blue,
  no wireless yet; each side a row of 5, only the base up; moves 0
  Higher, 1 Lower, 2 Freeze (after a right call), 3 Change (base only,
  once a turn, before the first call); equal = wrong; a miss clears back
  to the freeze and shows the beating card crossed out; aces high; first
  to the 5th card wins the round, rounds alternate who starts, 2 rounds
  win; one deck, the table's cards kept out when it reshuffles; computer
  Easy calls by the card and freezes after 2, Medium + changes 7-9 and
  freezes under 55 %, Hard counts unseen cards and pushes when the other
  side is 1-2 from winning (beat Easy 225/400 - it is mostly luck); each
  call holds 750 ms (`busy`); keys Freeze | Change over Higher | Lower;
  rows: the other side on top, yours (pass-and-play Gold) at the bottom;
  the gold freeze bar only once frozen past the base; sounds right Place /
  Turn, miss "aww", round Trill, Freeze / Change silent; save "CSC1"),
  Sorry-CYD! (id `sorrycyd`, Strategy, kVsComputer; Tom's name for Sorry!,
  2026-10-06; classic rules: 45 cards (five 1s, four each of 2 3 4 5 7 8
  10 11 12 Sorry!), 60-square track, side s = colour s (Red, Blue,
  Yellow, Green = piece_a, frame, piece_b, win), slides on each side r1->4
  and r9->13 (not your own colour's), start onto r4, Safety from r2, 5
  Safety squares, exact Home; a 2 draws again; 7 splits over two pawns; 11
  move or switch (pass only when 11 forward can't); 4 back / 10 one back;
  progress -1 = r3 behind your start; you vs Max, Zoe, Ada at Easy (own
  progress) / Medium (+ others' losses x0.3 and the chance each pawn is
  hit) / Hard (Medium + expected best next card) - luck-heavy: Hard ~31 %
  vs three Easy (25 % = chance); or Pass and Play (menu Options key) for
  2-4 people, leftover colours Medium, not recorded; board turned so Red
  is at the bottom; movable pawns gold-ringed, picked pawn orange, targets
  = framed squares (a dot looked like a green pawn); a single movable pawn
  is picked for you; taps act on release at the press point; computers
  draw 0.65 s, show the card 0.9 s, move; sounds your move Place, theirs
  Turn, your pawn sent back "aww", your pawn Home Hint, Win / Lose; save
  "SRY1"; stats "#,Place,Home,Level,Seconds,Time", a vs-computer game left
  after 4 turns counts at your place then).
  The match
  info line shows a game's note on its own, and drops the level when
  score + level would be cut short. Computer ties between equal moves are broken by a random
  seed; no deliberate blunders. Board games use `common/board8.*` (8x8
  view: tap, target dots). Checkers: when a jump is
  compulsory (American rules - yes, a jump must be taken; Tom checked and
  it was right) the pieces that can jump light up (warn tint) with the
  "A jump must be taken" note. board8 taps (Tom, 2026-10-03:
  Chess taps were "very bad"): a tap acts on release (CLICKED, any
  length) at the square where the stylus came DOWN (lift-off readings
  drift). Seeing moves (Tom, 2026-10-03, replaces the long-press peek):
  tap your own piece = pick it, dots show its moves, second tap = the
  destination; tap one of the other side's pieces (or any piece while
  it isn't your turn) = only show its moves; the next tap clears that.
  Chess/Checkers use no long-press now (board8 still supports one: own
  750 ms timer - LVGL's 400 ms turned firm taps into long-presses).
- Game options (Tom, 2026-10-05): a two-player game with settings gives
  `match::Game::options` - an "Options" key in its menu
  (`menu_two_player` / `menu_wireless` take the label) opens its page:
  a heading line, then a two-word choice as `ui::overlay_choice()` (the
  same switch as Play's Left Hand / Right Hand), a muted line explaining
  it; the header's < goes back to the menu. Solo games: `menu_solo`'s
  options key.
- Shared UI in `src/ui/`: `widgets.*` (keys, hamburger, overlays, tables,
  screen metrics, `scratch_table()`: two shared heap tables for stats
  screens - never `static Table`, each costs ~1 KB of static RAM), `app_shell.cpp` (picker, game switching),
  `settings_screen.cpp` (Settings, themes + palette editor, touch test), `sound.*`, `theme.*`.
- Header bar (Tom, 2026-10-04; `src/ui/sysbar.*`): one bar on
  `lv_layer_sys()` on every screen, games included, at the games' old bar
  height (28 / 40 px; Tom: NOT a thin extra bar - icons must stay
  finger-tappable). Built once (games keep pointers to its labels), then
  only restyled. Left to right: **<** back (`sysbar_back()`: a page's back
  key via `overlay_back()` / `sysbar_page_back()`, else the screen's back -
  picker category -> list, game -> `app_go_home`, but a live wireless game
  forfeits via `set_game_back_hook`), middle, **2P** (two head-and-
  shoulders, filled = wireless session to resume/in progress, tap =
  `wplay::resume_session()`), **wifi** (empty = 1P; dot + 0-3 arcs from
  RSSI: >= -60 / -70 / -80 dBm; tap = Play settings), **gear** furthest
  right (Settings; in a game it opens the game's menu - no ☰ anywhere,
  Tom). The middle shows a page's title (picker home: "Classic Games" + the
  version only if it fits - it doesn't on these widths; About has it), or
  in a game (`kit::top_bar()` -> `sysbar_game()`) the left label and the
  status: Tom - no game name, no difficulty, no clock in most games. Left
  label: 0 hidden (default), 1 while room (Sudoku, Minesweeper clocks), 2
  always (Blackjack / Video Poker chips). `top_bar_status(bar, text,
  short_text)`: games give a short form where the full one is long ("Your
  turn (Black)" -> "Your turn"). A better difficulty indicator is still
  to be designed (Tom). metrics().h = height under the bar; the screen and
  top layer get pad_top = the bar height. No big Back keys on pages: the
  header's < is the back.
- Game picker (after the splash): a "Continue <last game>" card (icon, title, the game's
  summary line), then the categories as a text list (Tom, 2026-10-02):
  Puzzle Games, Strategy Games, Card Games (added 2026-10-03 for the card
  games), Word Games, Dice Games (Other Games dropped - Tom, 2026-10-03),
  Game Shows (added 2026-10-06), each
  with its game count. A category opens its own page of icon tiles, 2 per
  row (the header's < goes back). Extra tiles go on pages switched
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
  Rewrites check every write and keep the old file on failure; a
  leftover `<file>.tmp` with no `<file>` is put back.
- `src/app/legacy_import.*`: flashing over CYD-Sudoku without erasing moves
  its `/game.bin`, `/stats.csv`, `/puzzle_stock.bin` to the per-game names
  and copies SD `/CYD-Sudoku/stats.csv` to `/CYD-Classic-Games/sudoku.csv`.
- Firmware version define: `CYD_GAMES_VERSION` (CI sets it).
- Per game: save `/games/<id>.bin` (temp file + rename, 4-char format tag
  + version, older versions still load; loaders validate every card /
  index they read - a corrupt save is rejected, never indexed) - after
  each move, plus every 30 s via `kit::save_due()` only while the game's
  clock moves (idle or finished games aren't rewritten) - and stats
  (SD `/CYD-Classic-Games/<id>.csv` when usable, else LittleFS `/stats/<id>.csv`).
- Computer opponents think on a core-0 task so the UI never freezes; each
  has difficulty levels limited by depth or time, not by making random
  blunders.
- match.* (code review 2026-10-03): leaving a vs-computer game counts as a
  loss only once the player has moved (`State::human_moved`, saved as bit 1
  of the recorded byte - the computer often moves first); a finished
  computer move waits while a menu is open; `match::Game::busy` holds the
  computer while a game animates its last move (Mancala's sowing), and the
  think pause starts after; if the AI task can't start (no memory) it
  retries every 3 s, logs once, and the status says "Low on memory...".
- Rules settled in the code review (2026-10-03): Hold'em - an uncalled bet
  handed back is `Seat::returned`, not winnings (Split/"wins" ignore it),
  and Exit Game mid-hand records nothing (the hand resumes); Solitaire -
  leaving while "Shuffling..." saves a pending flag (bit 1 of next_ok),
  reopening keeps shuffling without re-recording; FreeCell hints move only
  whole runs (no back-and-forth); Video Poker hint holds 4 to a straight
  flush over two pair / a high pair; Blackjack 3:2 rounds half chips up;
  Yaht-CYD forced joker (upper box, else an open lower box, else an upper
  box for 0); Farkle scores each face's dice as one set (four 1s = 1000);
  CYD-dle has no Restart (the word is known); Light Switch / Sliding Tiles
  Restart of a solved puzzle doesn't record again (Sudoku keeps v1.0.0's
  behaviour); chess repetition ignores an en passant square nobody can use.
- Taps on boards with small cells act on release at the PRESS point with
  an own 750 ms long-press (board8, Minesweeper's flag) - never
  SHORT_CLICKED + LVGL's 400 ms long-press.

## UI rules (Tom's, all games)
- Handedness (Tom, 2026-10-04): the hand holding the stylus covers the
  screen below and to that side of the tap. Settings -> Play -> the Left
  Hand / Right Hand switch (`UiSettings::left_handed`, UIS2 `flags` bit 1, default right;
  `ui::right_handed()`; changing it rebuilds the open game via restyle).
  Put what's tapped most on the hand's side or along the bottom edge, so
  the hand doesn't hide what the player is looking at; read-only info goes
  on the other side / the top. Key rows at the bottom aren't mirrored
  (nothing below them to cover). Every new layout must follow this.
  Done so far (review 2026-10-04): Solitaire (RH: foundations left, waste,
  stock in the top right corner; the waste's top card stays next to the
  stock and the older two fan to its left - a card's index is on its left;
  LH: the old layout, stock top left), Golf and Pyramid (stock on the hand
  side of centre, waste inside it), Spider (stock in the hand-side top
  corner, finished runs opposite), FreeCell (free cells on the hand side,
  foundations opposite), Mancala (your pits in the hand-side column vs
  computer; pass-and-play puts Gold there), Nonograms (row clues on the
  side away from the hand), Yaht-CYD (score card on top, dice and Roll on
  the bottom - both hands). The other games were checked and need nothing:
  symmetric boards with keys/palettes/keyboards along the bottom. The
  preview renders the changed games left-handed (`*_70_lh_*`).
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
  a tap in a zone = a swipe that way. Under the board, in the Play Again
  key's place while the game is on: "Tap the direction of your move." (Tom).
- Long-press is not forbidden, just not preferred: never the only way to do
  something. Propose each use to Tom and ask before adding it. Approved
  (2026-10-02): Minesweeper (long-press = flag, besides the Flag toggle);
  Chess and Checkers long-press peek (2026-10-02) was replaced by a tap
  on the other side's piece (2026-10-03). Declined: Sudoku long-press to
  clear.
- No "tap again" / "are you sure" confirmations, ever. Buttons act on the
  first tap.
- Strong highlight tints with distinct hues (cheap TN panels wash out pale
  tints at an angle). No shrinking fonts to squeeze labels in.
- Each game: the header's gear opens its menu (new game, restart,
  **How To Play** (Tom, 2026-10-03: every game; full-width key above
  Stats | Settings, opens `kit::how_to_play()`: pages with < > keys, the
  header's < back to the menu), stats, Settings). Its LAST row, pinned to the bottom, is always
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
  Dark = black screen background (Tom, 2026-10-04) with night-sky navy
  keys/cells and cream text; accents coat gold, hat blue,
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
  picking up a piece, holding a die, looking at a piece's moves. Sounding: a move or
  placement, the other side's reply, mistakes, hints, game end, and one
  sample when the Volume slider is released.
- Settings (Tom, 2026-10-04: a page per area, header gear): **Display,
  Sound, Touch, Play, About** (no Back key - the header's <).
  Display: Brightness slider, Invert Colors, Swap Red/Blue, Rotate 180,
  Themes (`theme_screen.cpp`, back -> `settings_open_display()`). Rotate 180
  (Tom, 2026-10-03: USB cord out either end; a toggle, lit while on; UIS2
  `flags` bit 0, applied before the splash; `lvgl_port_set_rotation()`
  turns panel + touch at run time, calibration is rotation-independent).
  Sound: Volume slider + Mute toggle (remembers the volume to go back to).
  Touch: Touch Test, Recalibrate. Play (= `wplay::open_menu`): a switch
  "Left Hand  (o)  Right Hand" (an lv_switch pointing at the hand, both
  sides the same colour - a choice, not on/off; small text words - Tom),
  then "Play Mode" with "1P (o) 2P" (2P = available to play = radio on),
  then Find Players / Games I'll Play / Change Name (dimmed and inert in
  1P). About (`src/ui/diagnostics_screen.cpp`): [Device Log | Send Log],
  board, firmware (version + build commit), free memory, uptime. Device
  Log: paged with < > (opens on the newest page), [Clear Log | Copy To SD].
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
  segment, ECC L, under it "Point your phone's camera at the QR code to get
  a link that sends the log to the developer." (Tom; small print -
  montserrat_12 - so the code keeps its full size), encoder = LVGL's bundled Nayuki qrcodegen
  (`LV_USE_QRCODE 1`), modules >= 2 px (240 wide: ~150 lines / 5 KB of
  text; 320 wide: ~380 lines / 12 KB). The page `web/l/index.html` decodes
  it (DecompressionStream "deflate-raw") and offers Email (mailto to
  **cyd.classic.games.logs@gmail.com** - Tom's address for logs), Copy,
  Download log.txt. The same page reads the whole log over USB (Web
  Serial, Chrome/Edge): it sends "log\n"; the firmware (`serial_commands()`
  in main.cpp, also polled while the splash waits for its tap; requests
  that piled up get one answer) prints it between "---- device log ----"
  and "---- end of log ----". Only that command prints the whole log to
  serial (a 16 KB dump blocks ~1.4 s): the Device Log / Send Log screens
  read it quietly. The flasher page links to it. CI keeps each build's
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

## Sudoku (ported from CYD-Sudoku v1.0.0 - keep its behavior, but fix real issues: Tom, 2026-10-03)
- Restart This Puzzle: an unsolved puzzle keeps its clock and hint count;
  a solved one restarts as a replay (`Game::replay()`, saved in spare bit
  87 of the hint bits) whose solve isn't recorded again.
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
  new-game menu lists them; the Wireless key (-> Play settings) is greyed
  in a game without it (Farkle, for now).
- Tom, 2026-10-02: finish more games before building wireless play.

## Multiplayer (CYD to CYD) - redesign built 2026-10-05 (v0.21.0, Tom's design in SPEC section 5)
- HARD RULE above: only fixed codes on the air. Names = two word numbers
  (`src/net/names.*`, two APPEND-ONLY lists - 106 describing words, 100
  things/creatures - kid-safe and silly, <= 8 letters each; Random / Pick
  From List, 2 or 3 columns by the widest word, paged); a legacy (link < 3)
  board shows as "Older board", never its typed name. Tom's review
  (2026-10-04): every pair checked for innuendo, slang, slurs and bullying
  meanings; 18 words removed (listed in names.cpp - never add them back);
  frozen from v0.22.0. Check every new word against the whole other list.
- `src/games/common/net_games.h`: each wireless game's fixed KEY (on the
  air, never a list position) and VERSION. Moves travel as move keys that
  describe the move (Chess `chess::move_key` from|to<<6|promo<<12, Checkers
  `checkers::move_key` from + landings + 2-bit directions; others column /
  square / pit / Morris code). `tools/host_tests/test_movekeys.cpp` plays
  fixed games and compares with `net_moves.txt`: a changed move stream
  needs a version bump + a new line (never edit old lines). match::Game
  moves are these keys everywhere (`list` gives them for tests).
- Protocol `src/net/wireless.*` link version 3 (`kLink`): frozen Call (0x10)
  / Hello (0x11) layouts - later versions may only append; Request / Answer
  (Play, No Thanks, Other Game, Busy, Game Off, Cancel, Ringing) / Status
  (u32 moves, flags again/forfeit/done/gone/void/suspended/continue/
  by-time/closed, the asked board's move timer). 2P = listen; Calls only
  while the Find Players / player / version / requesting pages are open;
  Hellos answer Calls. First asker priority; crossed requests: the lower
  address keeps asking. The asked player moves first (game 0), then
  alternates. Out-of-step = that game void (not counted), Play Again goes
  on. Suspended sessions (lost touch waited out, or loaded after a restart)
  send a status every 2 s; meeting again asks both "Continue?", both yes =
  resume. `send_end` + wplay tombstones (2 last ended sessions, saved)
  answer a returning partner, so a forfeit reaches a board that was away.
- `wplay.*`: profile "PLR3" (name words, games mask by key-1, 2P, Move
  Timer 30/60/120/300/0), session "WLS2" (link "LNK3" + tombstones; kept
  through a restart). Busy = a live session not over (incl. put away).
  Pages: Play, Find Players, <name> (games, lit = playable, "needs update"
  small under a grey game), version page (flasher address), Requesting,
  request popup, Other Game pick, Connecting (10 s), back-in-range, Your
  Name, First/Second Word, Move Timer, a notice page for news off the Play
  pages. One-player game of the session's game: copied to `1p_<id>` before
  the session and put back when the game closes after the session ended
  (and at boot); the game then shows "Resuming your previous one player
  game." Clear 2P Sessions = forfeit if the partner is up and the game is
  going, else leave (not counted). Radio on while 2P or a session exists.
- `match.*`: header "Respond in Ns" / "Waiting... Ns" (Move Timer; time
  restarts when the link comes back), at 0 the grace popup, at -10 s a
  by-time forfeit (shown over the board). No reply for a move time (60 s
  with Timer Off): "No Reply" [Keep Waiting | Close Game]; another move
  time = `wplay::suspend_session()`. Leaving (back arrow, Exit Game via
  `MenuHandlers::exit_game`) = the one confirmation; Forfeit Game acts at
  once; no Forfeit after game over. Game over: [Again | New Game | Goodbye];
  ended: [Done]. A partner's forfeit / closed / gone pops a notice (the
  info line is one line). Leaving a finished game = Goodbye.
- Tests: `test_net.cpp` (fake lossy air), `test_movekeys.cpp`, and
  `tools/preview/duo.py` (agent names must be list names, e.g. "Jolly
  Llama"; scenarios: request/play/rematch, link loss, leave confirm, menu
  forfeit, No Thanks / Other Game / no answer / cancel, the one-player game
  coming back, priority + busy + Clear 2P = forfeit, restart + Continue,
  No Reply + Close, Keep Waiting + meet + move timer forfeit, forfeit while
  away, crossed requests, New Game, every game random) clean and 30 % loss.
  Run it after any wireless change.
- Games: FourConnect, Tic-Tac-Toe, Reversi, Checkers, Chess, Mancala,
  Morris, You Sank My CYD! (key 8), Ultimate Tic-Tac-Toe (9), Gomoku (10),
  Strategy Go! (11).
  Farkle not yet. Internet play out of scope.
- Battery (Tom, 2026-10-04, v0.22.0): the radio is never stopped and
  started to save power (esp_wifi init + start takes tens to hundreds of ms
  with RF calibration and would cost more than it saves); instead an idle
  2P board DOZES with the driver's ESP-NOW power saving
  (`radio_doze()`: modem sleep, wake window `kDozeWindowMs` 120 ms every
  `kDozeIntervalMs` 2 s = 6 % listening). Awake whenever something goes
  on: searching, requesting, asked, a Call heard in the last 8 s, a live
  session, connecting, a meeting question, a put-away session's burst.
  Anything sent to a maybe-dozing board repeats every `kWakeSendMs` 100 ms
  (Calls every `kCallMs` 50 ms, a request until it rings, statuses while the link is down,
  put-away bursts of 2.6 s every 10 s), so a window always catches one:
  found / asked within ~2 s. About shows "Radio: Off (1P) / Dozing (2P) /
  Listening"; the log says how long radio_on took. The preview agent drops
  packets outside the wake window while dozing, so duo.py tests it.
- Radio memory (v0.22.1, after Tom's 4.0" had ~50 KB free with the radio
  on): `radio_on()` trims the WiFi init config (4 static RX buffers, at
  most 16 dynamic RX/TX, no AMPDU/AMSDU).
- Wireless log lines (v0.22.1, after a stuck Reversi game on Tom's boards):
  each game's start (side, timer), the first 4 plies (played here / from
  the partner), link down/back with the radio counters (`radio_counts`:
  sent, failed + last error, received, dropped; free heap), and "behind"
  when the partner is ahead for 3 s without this board playing its move
  (why: move waiting/missing, overlay, busy, turn). The header's short
  status for a lost link is "Out of range" (was "Waiting...", which looked
  like the Move Timer's "Waiting... 30s").
  The stuck Reversi game didn't happen again on v0.22.0 (Tom tried):
  tabled until a log with these lines shows it.
- Code review 2026-10-05 (v0.22.3): a New Game request from the partner
  could arrive a moment before this board saw the game end and was
  answered "can't play right now" - now a request from the session's
  partner while busy gets no answer yet (it repeats and pops up once the
  game here is over; `Profile::partner`). The one-player game is put aside
  through a 1 KB stack buffer (`kStashMax`; test_movekeys checks every
  wireless save fits), not a 4 KB heap block a low-memory board might not
  have. duo.py `scenario_switching` ends games every way and checks moves
  still flow after each switch.
- Memory note: the 4.0" boots with ~139 KB free, largest block 75 KB
  (v0.19: 147 / 107). The 8 KB is v0.20's 16 KB main-task stack (the
  chess crash fix); where that stack lands splits the biggest block.
  Static RAM barely moved (57.5 -> 58 KB). Watch big contiguous needs
  (Checkers' 40 KB AI stack) with the radio on.

## Known hardware issues (from CYD-Sudoku - all still apply)
- Supported boards: 2.8" ESP32-2432S028 in ILI9341 and ST7789 versions
  (`src/boards/esp32_2432s028.hpp`), and "ESP32-32E" display boards 3.2"
  ST7789, 3.5" ST7796, 4.0" ST7796, all resistive
  (`src/boards/esp32_32e_display.hpp`).
- Tom owns and has tested every supported board (the 2.8" ILI9341 and the
  3.5" ST7796 since 2026-10-05). A new board's env gets
  `custom_board_tested = no` until it has been run on the hardware.
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
  Settings > Touch > Touch Test; `-D CYD_TOUCH_DEBUG` logs raw touches to serial.
- Panel inversion / red-blue order differ between production runs. Fixed
  per unit on the device (`src/hal/panel_prefs.*`), not with build flags.
- Before the screen comes up, quiet the RGB LED and audio amp
  (`quiet_peripherals()` in main.cpp).
- Speaker: `BOARD_PIN_SPEAKER` 26 on both board families (2.8": straight
  to its amp; ESP32-32E: DAC pin into the amp, enabled by
  `BOARD_PIN_AUDIO_EN` 4, low = on; the driver turns the amp on only while
  a sound plays). LEDC square wave. Each note is timed by an esp_timer,
  never by loop() (v0.27.2, Tom: the computer's shells whistled slower
  than his - a heavy redraw stretched loop-timed notes). Animations
  invalidate only the area that changes, so frames stay cheap.
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
  Tom OK'd Open Trivia DB (opentdb.com, CC BY-SA 4.0, ~5,300 verified
  questions) for the trivia games (2026-10-06). Keep it a separate data
  file (e.g. `assets/trivia/`) marked CC BY-SA 4.0 with attribution and
  "changed: screened / edited" in THIRD_PARTY_NOTICES.md, the README and
  the trivia games' About / How To Play; our edits of it stay CC BY-SA;
  the code stays MIT (the data is part of a collection, not an adaptation
  of the code). Screening (Tom, 2026-10-06): drop only subjects that
  are inappropriate for minors (and answers that have gone stale); keep
  hard, specialised or "grown-up knowledge" questions on kid-safe
  subjects - it is trivia.
- No trademarked game names. Tom's names: **FourConnect** (Connect Four),
  **CYD-dle** (Wordle), **Yaht-CYD** (Yahtzee), **Light Switch** (Lights
  Out), **MasterCYD** (Mastermind), **SokoCYD** (Sokoban), **KenCYD**
  (KenKen), **You Sank My CYD!** (Battleship - "Sank", not "Sunk"), **Wheel of CYD** (Wheel of
  Fortune), **Reversi** (Othello), **Pipe Race** (Pipe Mania - Tom: it
  is a race against the water), **Acquisitions** (Acquire), **Strategy
  Go!** (Stratego) (Tom, 2026-10-06), **Deal or No CYD** (Deal or No
  Deal), **Press Your CYD** (Press Your Luck - the bad square is a
  "Gremlin", never a Whammy), **Card Sharks CYD** (Card Sharks), **Sorry-CYD!**
  (Sorry!), **Escape from CYD** (Escape from Atlantis) (Tom,
  2026-10-06). Game shows live in their own picker category, **Game
  Shows** (`Category::Shows`; Wheel of CYD moved there - Tom,
  2026-10-06). Classic public-domain games (chess,
  checkers, mancala, ...) are fine by name.

## Versions (Tom, 2026-10-03)
- Semantic versioning, `vX.Y.Z`, from the `VERSION` file (just `0.9.0`).
  A fix or small change bumps Z, a new feature (game, setting, screen)
  bumps Y, a major revision bumps X. **Bump VERSION in every push to main
  that changes the firmware** - each push is a build Tom flashes, so each
  gets its own number. Started at 0.9.0 (2026-10-03); 1.0.0 is Tom's call.
- `tools/version.py` (PlatformIO pre-script) defines `CYD_GAMES_VERSION`
  ("v0.9.0") and `CYD_GAMES_BUILD` (git commit) for every build, local or
  CI. About (Settings), the log and the flasher show the version (log /
  About add the commit); the picker's header adds it only where it fits.

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
- Site source: `web/index.html`; assembled by `tools/make_site.py` (copies
  `assets/splash/splash1_320x480.jpg` as `splash.jpg`). Layout (Tom,
  2026-10-04): header (navy + gold from the splash art, the splash image),
  the kid-safe note ("safe for kids, still fun for grown-ups, like LEGO";
  no chat), then Install right away: steps (Chrome or Edge on a computer
  first), ONE board drop-down and ONE Install button (disabled until a
  board is picked; it sets the esp-web-install-button `manifest`), the
  picked board's hint + file download. Below: Which board do I have?,
  the games by category, after installing, logs.
