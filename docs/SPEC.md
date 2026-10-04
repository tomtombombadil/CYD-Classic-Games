# CYD Classic Games — Project Specification (draft)

A collection of classic games for ESP32 "Cheap Yellow Display" boards, with
CYD-to-CYD play over WiFi for the two-player games. Seeded from CYD-Sudoku
v1.0.0; all of its board support, touch handling and tooling carries over.

## 1. Target hardware & constraints

Same boards as CYD-Sudoku. Firmware files carry the `TTB-CYD-CG_` prefix
(Tom, 2026-10-02) so they can't be mixed up with CYD-Sudoku's:

| Firmware file | Env | Tested on hardware |
|---|---|---|
| `TTB-CYD-CG_2.8in_ILI9341_Resistive.bin` | `cyd28_ili9341_res` | No |
| `TTB-CYD-CG_2.8in_ST7789_Resistive.bin` | `cyd28_st7789_res` | Yes (CYD-Sudoku) |
| `TTB-CYD-CG_3.2in_ST7789_Resistive.bin` | `cyd32_st7789_res` | Yes (CYD-Sudoku) |
| `TTB-CYD-CG_3.5in_ST7796_Resistive.bin` | `cyd35_st7796_res` | No |
| `TTB-CYD-CG_4.0in_ST7796_Resistive.bin` | `cyd40_st7796_res` | Yes (CYD-Sudoku) |

Budget (CYD-Sudoku v1.0.0 as the baseline):
- Program flash 3 MB. Shared base (Arduino core, LVGL, fonts, LovyanGFX)
  ~650 KB; Sudoku ~150 KB; WiFi/ESP-NOW stack ~500-700 KB. Small games are
  a few KB each, chess with AI ~50-80 KB. Plenty of room for 15+ games.
- RAM ~320 KB, ~55 KB static. One game alive at a time; each frees its
  memory on exit. WiFi needs ~40-60 KB while on, so it's on only for
  network play.
- LittleFS ~900 KB: settings, one save + stats per game.
- Everything ships inside the firmware. No SD-card game content for now.

## 2. Development environment
- VS Code + PlatformIO (Windows), C++17, Arduino framework via pioarduino.
- LovyanGFX (hardware) + LVGL 9 (UI), glued in `src/hal/lvgl_port.cpp`.
- PC tools (Linux, Claude's side): host tests with sanitizers, LVGL preview
  renderer for 240x320 and 320x480 screenshots and memory use.

## 3. Shell
- **Splash** at boot: one of three title images (alternating each boot,
  sized for 240x320 or 320x480), "press anywhere to play": a tap goes on
  to the picker.
- **Game picker**: "Continue <last game>" at the top, then the categories
  as a text list: Puzzle Games, Strategy Games, Card Games, Word Games, Dice Games
  (no Other Games - Tom, 2026-10-03). A category opens a page of game icons (paged if needed).
- **Header bar** on every screen, games included (Tom, 2026-10-04; the
  games' standard bar height, icons as tall as it): **<** back (closes a
  page, leaves a category or a game; in a wireless game in progress =
  Forfeit), the middle (a page's title, or in a game only the live status:
  no game name, no level, a clock only in Sudoku and Minesweeper, chips in
  Blackjack / Video Poker), **2P** (filled = a wireless game in progress or
  to resume; tap = back to it), **wifi** (empty = 1P; dot + 0-3 arcs =
  signal of the boards nearby; tap = Play settings), **gear** (Settings; in
  a game the game's menu - no ☰).
- Inside a game, the gear's menu has: new game, restart, How To Play,
  stats, Settings, Exit Menu | Exit Game.
- **Settings** is shared, a page per area: **Display** (brightness, invert
  colors, swap red/blue, rotate 180, themes), **Sound** (volume slider, 50 %
  default; Mute), **Touch** (touch test, recalibrate), **Play** (Left Hand
  / Right Hand switch, Play Mode 1P / 2P switch; 2P = wireless play: Find
  Players, Games I'll Play, Change Name), **About** (board, firmware,
  memory, device log, send log). No Back keys: the header's < goes back.
- **Device log**: boots, reset reasons, crash reports (reason, address,
  backtrace, last step) kept in flash, shown under About, copied to
  SD on request, and echoed to the serial port.
- **Themes**: Light, Dark, and three Custom themes. A custom theme starts
  from Light or Dark; the player picks colors for 10 roles (background,
  cells, grid lines, ink, your entries, selected, same digit, row/column,
  buttons, accent) from a 48-color palette. Saved on the board.
- **Sound**: short tones through the CYD speaker connector (taps, moves,
  errors, wins). Off = silent play.
- **Stats** per game, same style as Sudoku (CSV on SD where usable).
- **Long-press**: allowed but not preferred, never the only way to do
  something; each use is agreed with Tom first.

## 4. Game list

Candidates, grouped. Order of building is in section 6.

**Puzzles (solo)**
| Game | Notes |
|---|---|
| Sudoku | Port of CYD-Sudoku v1.0.0 as-is |
| Minesweeper | Flag mode toggle (like Sudoku's Notes) |
| Nonograms | Generated puzzles, checked for one solution |
| Light Switch (Lights Out) | Built |
| Sliding Tiles (15-Puzzle) | Tap a tile next to the gap |
| Peg Solitaire | Tap peg, tap hole |
| MasterCYD (Mastermind) | Colors or symbols |
| Memory Match | Symbols, not cards |
| 2048 | Tap the board edge to slide (no swipes) |
| SokoCYD (Sokoban) | Needs freely licensed or generated levels |
| Kakuro / KenCYD (KenKen) | Reuse Sudoku's grid UI |


**Strategy (vs computer, pass-and-play, CYD vs CYD)**
| Game | Notes |
|---|---|
| Chess | Own MIT engine; levels by depth/time |
| Checkers | Strong AI is cheap |
| Reversi (Othello) | Strong AI is cheap |
| FourConnect (Connect Four) | Can play perfectly; easy levels hold back |
| Mancala (Kalah) | Two rows of six pits (*built 2026-10-03*: portrait, pits in two columns, stores in the middle) |
| Tic-Tac-Toe / Ultimate Tic-Tac-Toe | Ultimate is the interesting one |
| Nine Men's Morris | Big targets (*built 2026-10-03*) |
| Gomoku | 15x15, 16 px cells on 240-wide boards |
| You Sunk My CYD! (Battleship) | Two grids, flip between them |

**Word**
| Game | Notes |
|---|---|
| CYD-dle (Wordle-style) | Public-domain word list; on-screen keyboard |
| Trivia | Question bank license to check (Open Trivia DB is CC BY-SA) |
| ~~Hangman~~ | Dropped (Tom, 2026-10-03): needs a keyboard, not much fun vs a computer |
| Wheel of CYD (Wheel of Fortune) | phrase guess with spinning reward/fail aspect |

**Dice**
| Game | Notes |
|---|---|
| Yaht-CYD | Yahtzee by a non-trademarked name |
| Farkle | Push-your-luck |
| RPG Dice | Dice roller (Tom, 2026-10-03): every RPG die drawn as rolled, presets, history. *Built.* |

**Maybe later / poor fit:** Go 9x9 (weak AI), Solitaire/Klondike/FreeCell/Blackjack/Poker (cards too small on 2.8" perhaps if the card only shows the number and a symbol of the suit),
Chinese checkers, Dots and Boxes (thin tap targets).

## 5. Multiplayer (CYD to CYD) - *built 2026-10-04, redesigned the same day (Tom)*
- Transport: **ESP-NOW broadcasts** on WiFi channel 1 (direct, no router,
  no setup, no pairing). Range: same room or house. Home-network and
  internet play are not planned. `src/hal/radio.*`: the bare WiFi driver,
  no IP stack (~20 KB static RAM; its heap use is logged - "Wireless:
  radio on, N KB free").
- Tom's design (2026-10-04): the players may not see or be able to talk to
  each other, so finding a partner and a game is done by the boards.
  Settings -> **Play** (also the header's wifi icon and a two-player
  game's Wireless key):
  - **Play Mode 1P | 2P** switch (2P = available to play): radio on, the board beacons its name, the
    games it will play and whether it is in a game; offers reach it
    anywhere (picker, solo games, menus) as a pop-up with a ding-dong.
    Saved, so it stays on after a restart.
  - **Games I'll Play**: a toggle key per wireless game + All Games; only
    lit games are announced and can be asked for.
  - **Find Players** (2P only, like the keys below): players nearby with their game
    count, or "playing <game>" / "other version" greyed. Tap a player ->
    their games -> tap one = an offer ("Asking Bob to play Chess...",
    Stop Asking). Answers: Play (the game opens on both boards; the asker
    moves first), Not Now ("can't play right now. Thanks for asking!"),
    Other Game ("would rather play another game"), automatic Busy / game
    switched off, no answer in 30 s, or the board went away.
- In a game: "Waiting for <name>..." when the other board is out of range
  or has the game closed (no moves then). Exit Game pauses; the 2P icon /
  Resume (Play page) / Continue carries on while the board stays on
  (`/games/wl_session.bin`). **A restart clears the session** (Tom,
  2026-10-04, after a crash left two boards stuck in a game neither could
  resume or leave): unrecorded, the other board is told "gone" and ends
  its side unrecorded too. **Clear 2P Sessions** on the Play page does the
  same by hand, any time there is a session. A game that finds a session
  it never switched to (a crash in between) clears it; a game left in
  wireless mode with no session shows "Game ended" (New Game in its menu).
  **Forfeit Game** in the menu (or the header's <): a loss for that player,
  a win for the other, the forfeiter goes back to the Play page. Game over: **[Play Again | Done]**
  - the next game starts once both tapped Play Again (first mover
  alternates); Done takes both boards back to the Play page ("Bob is done
  playing. Thanks for the game!"). A board whose game is over isn't busy:
  accepting a new offer ends that session.
- Protocol (`src/net/wireless.*`, plain C++): no acks; every board repeats
  its state twice a second and at once on a change, with a counter so late
  copies can't undo newer ones. Beacons: name, firmware, available / busy
  / paused, games mask, an offer or a "no" (with a reason) for one board.
  Game status: session, game number, moves played, the last 12 moves, a
  hash of all moves, flags again / away / forfeit / done / gone. A board
  behind plays the missed moves, each checked by its own rules; a bad move
  or a differing hash ends the session unrecorded on both. Forfeit and
  Done are repeated for 4 s. Different firmware versions can't play.
- Tests: `tools/host_tests/test_net.cpp` (the protocol over a fake radio
  that loses, repeats and reorders packets) and `tools/preview/duo.py`:
  two (or three) whole boards - the PC preview in agent mode - run in
  lockstep with their packets carried between them, playing scripted
  sessions (offers, answers, rematch, Done, forfeit, pause, link loss, a
  restart, crossed offers, a third board, random games of every wireless
  game) clean and with 30-50 % of packets lost. Both run in CI.
- Games: FourConnect, Tic-Tac-Toe, Reversi, Checkers, Chess, Mancala,
  Nine Men's Morris (kNetwork in games.def + `match::Game::legal`). Farkle
  not yet (its dice would have to be rolled by one board and sent).
- Every two-player game also has vs computer and pass-and-play (one CYD
  handed back and forth).

## 6. Plan
1. **Repo seeded from CYD-Sudoku:** boards, HAL, CI, flasher, preview,
   tests. Game picker + registry. Sudoku moved in as the first game.
   Saves/stats per game. *Done 2026-10-02.*
2. **Shell + quick games:** splash, category picker, Settings with sound
   and custom themes; Light Switch, Tic-Tac-Toe, Sliding Tiles (15-Puzzle),
   FourConnect (vs computer + pass-and-play). *Built 2026-10-02, waiting
   for hardware testing.*
3. **Strategy:** Reversi, Checkers, then Chess. Shared "tap piece, tap
   square" board UI, AI task on core 0, pass-and-play. Then CYD-dle and
   Yaht-CYD (from stage 6). *Built 2026-10-02, waiting for hardware
   testing.*
4. **More puzzles:** Minesweeper, 2048, MasterCYD, Peg Solitaire, Memory
   Match, Nonograms (*built 2026-10-03*). Then the card games (Klondike,
   FreeCell, Blackjack, Poker) on the shared card graphics (mocked up
   2026-10-03). Tom, 2026-10-03: Klondike is called **Solitaire** (built:
   draw 1/3 option default 3, scoring options, Windows-style win show);
   also Spider, Pyramid, Golf, FreeCell, Blackjack (house only) (all
   built 2026-10-03); Poker = two games, Video Poker (Jacks or
   Better, *built 2026-10-03*) and Texas Hold'em vs computer players (*built 2026-10-03*); Blackjack; FreeCell. (Tom, 2026-10-02: more games before wireless.)
5. **Multiplayer** (ESP-NOW) for FourConnect, Tic-Tac-Toe, Reversi,
   Checkers, Chess, Mancala, Nine Men's Morris. *Built 2026-10-04.*
6. **Word and dice games:** (CYD-dle, Yaht-CYD done in stage 3) Farkle (*built 2026-10-03*), RPG Dice roller (*built 2026-10-03*); Trivia if a
   suitable question bank is found.
7. Remaining candidates as Tom picks them.

## 7. Decisions

Decided (Tom, 2026-10-02):
- Repo `tomtombombadil/CYD-Classic-Games`.
- Picker: categories as a text list first (Puzzle, Strategy, Word, Dice,
  Other Games), each opening a page of game icons.
- Two-player games: vs computer, pass-and-play and wireless, all three.
- Firmware files: `TTB-CYD-CG_<size>in_<DRIVER>_<touch>.bin`.
- Names: FourConnect (Connect Four), CYD-dle (Wordle), Yaht-CYD (Yahtzee).
- No OTA; the web flasher is the update path.
- Portrait by default; landscape allowed for a game where it clearly fits.
- Long-press: allowed, not preferred; ask Tom per use.
- Sound with a Volume slider in Settings (default 50 %, far left = muted); custom themes.
- Sounds only for game events, never for plain key taps.

- Long-press: yes for Minesweeper (flag) and Chess/Checkers (show a
  piece's moves); no for Sudoku.
- Names: Light Switch, MasterCYD, SokoCYD, KenCYD, You Sunk My CYD!,
  Wheel of CYD, Reversi.
- Every game menu ends with Exit Menu (bottom left) | Exit Game (bottom
  right). Title Case menu text. Default palettes follow the splash art.
- Order after stage 2: Reversi, Checkers, Chess, then CYD-dle and Yaht-CYD.

Open:
- Final game lineup and order after that.

## 8. Distribution
- Web flasher (ESP Web Tools on GitHub Pages), updated on every push to main
  with a build numbered from the VERSION file (semantic vX.Y.Z, bumped every push); releases on request.
- GitHub Releases: one merged (factory) `.bin` per board, flashed at 0x0.

## 9. Licensing
MIT. See `THIRD_PARTY_NOTICES.md`. Game data (word lists, questions,
levels) needs a license compatible with MIT; share-alike data needs Tom's
OK.
