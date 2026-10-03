# CYD Classic Games — Project Specification (draft)

A collection of classic games for ESP32 "Cheap Yellow Display" boards, with
CYD-to-CYD play over WiFi for the two-player games. Seeded from CYD-Sudoku
v1.0.0; all of its board support, touch handling and tooling carries over.

## 1. Target hardware & constraints

Same boards and firmware names as CYD-Sudoku:

| Firmware file | Env | Tested on hardware |
|---|---|---|
| `CYD_2.8in_ILI9341_Resistive.bin` | `cyd28_ili9341_res` | No |
| `CYD_2.8in_ST7789_Resistive.bin` | `cyd28_st7789_res` | Yes (CYD-Sudoku) |
| `CYD_3.2in_ST7789_Resistive.bin` | `cyd32_st7789_res` | Yes (CYD-Sudoku) |
| `CYD_3.5in_ST7796_Resistive.bin` | `cyd35_st7796_res` | No |
| `CYD_4.0in_ST7796_Resistive.bin` | `cyd40_st7796_res` | Yes (CYD-Sudoku) |

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
- **Game picker** at boot: tiles grouped by category (Puzzles, Strategy,
  Word, Dice), paged if needed. Top of the screen: "Continue <last game>".
- Inside a game, the ☰ menu has: new game, restart, stats, Display & touch,
  All games.
- **Display & touch** is shared: theme (Light/Dark), brightness, invert
  colors, swap red/blue, recalibrate, touch test, player name (once
  multiplayer exists).
- **Stats** per game, same style as Sudoku (CSV on SD where usable).

## 4. Game list

Candidates, grouped. Order of building is in section 6.

**Puzzles (solo)**
| Game | Notes |
|---|---|
| Sudoku | Port of CYD-Sudoku v1.0.0 as-is |
| Minesweeper | Flag mode toggle (like Sudoku's Notes) |
| Nonograms | Generated puzzles, checked for one solution |
| Lights Out | Tiny |
| Sliding Tiles (15-Puzzle) | Tap a tile next to the gap |
| Peg Solitaire | Tap peg, tap hole |
| Mastermind | Colors or symbols |
| Memory Match | Symbols, not cards |
| 2048 | Tap the board edge to slide (no swipes) |
| Sokoban | Needs freely licensed or generated levels |
| Kakuro / KenKen | Reuse Sudoku's grid UI |
| Sokoban | |


**Strategy (vs computer, pass-and-play, CYD vs CYD)**
| Game | Notes |
|---|---|
| Chess | Own MIT engine; levels by depth/time |
| Checkers | Strong AI is cheap |
| Othello | Strong AI is cheap |
| Four in a Row (Connect Four) | Can play perfectly; easy levels hold back |
| Mancala (Kalah) | Two rows of six pits |
| Tic-Tac-Toe / Ultimate Tic-Tac-Toe | Ultimate is the interesting one |
| Nine Men's Morris | Big targets |
| Gomoku | 15x15, 16 px cells on 240-wide boards |
| Battleship | Two grids, flip between them |

**Word**
| Game | Notes |
|---|---|
| Word Guess (Wordle-style) | Public-domain word list; on-screen keyboard |
| Trivia | Question bank license to check (Open Trivia DB is CC BY-SA) |
| Hangman | word guess by letter with limit to bad guesses |
| Wheel of Fortune | phrase guess with spinning reward/fail aspect |

**Dice**
| Game | Notes |
|---|---|
| Yaht-CYD | Yahtzee by a non-trademarked name (a.k.a. "Five Dice") |
| Farkle | Push-your-luck |

**Maybe later / poor fit:** Go 9x9 (weak AI), Solitaire/Klondike/FreeCell/Blackjack/Poker (cards too small on 2.8" perhaps if the card only shows the number and a symbol of the suit),
Chinese checkers, Dots and Boxes (thin tap targets).

## 5. Multiplayer (CYD to CYD)
- Transport: **ESP-NOW** (direct, no router, no setup). Range: same room or
  house. Home-network play and internet play are not planned.
- Flow: ☰ → Play nearby → list of boards in range by player name and
  game → tap to invite → other board accepts → game starts. Who goes first
  is decided at random and shown on both screens.
- Protocol: small binary messages - hello (protocol + firmware version,
  name, game), invite/accept/decline, move, resign, draw offer, resume,
  ack. Sequence numbers + acks + retries. Each board validates every move
  with its own rules engine.
- Disconnects: the game pauses and is saved on both boards; either can
  resume when the other reappears, or end it.
- Games: chess, checkers, Othello, Four in a Row first; later Mancala,
  Ultimate Tic-Tac-Toe, Nine Men's Morris, Battleship, Gomoku.

## 6. Plan
1. **Repo seeded from CYD-Sudoku:** boards, HAL, CI, flasher, preview,
   tests. Game picker + registry. Sudoku moved in as the first game.
   Saves/stats per game. *Done 2026-10-02.*
2. **Quick games** to prove the registry and picker: Lights Out,
   Tic-Tac-Toe, 15-Puzzle, Four in a Row (with AI).
3. **Strategy:** Othello, Checkers, then Chess. Shared "tap piece, tap
   square" board UI, AI task on core 0, pass-and-play.
4. **Multiplayer** (ESP-NOW) for Four in a Row, Othello, Checkers, Chess.
5. **More puzzles:** Minesweeper, Nonograms, Mastermind, Memory Match,
   2048, Peg Solitaire.
6. **Word and dice games:** Word Guess, Five Dice, Farkle; Trivia if a
   suitable question bank is found.
7. Remaining candidates as Tom picks them.

## 7. Open decisions (Tom)

Decided: repo is `tomtombombadil/CYD-Classic-Games`.

- Final game lineup and order (section 6 is a proposal).
- Pass-and-play on every two-player game, in addition to vs computer and
  CYD vs CYD? (Proposed: yes.)
- Firmware file names: same as CYD-Sudoku (proposed; the release page tells
  them apart) or with a `CYD_Games_` prefix?
- Names for trademarked games (Four in a Row, Word Guess, Five Dice).
- OTA updates: proposed no (keeps 3 MB for code; web flasher is the update
  path).

## 8. Distribution
- Web flasher (ESP Web Tools on GitHub Pages), updated on every push to main
  with a `dev-<sha>` build; releases on request.
- GitHub Releases: one merged (factory) `.bin` per board, flashed at 0x0.

## 9. Licensing
MIT. See `THIRD_PARTY_NOTICES.md`. Game data (word lists, questions,
levels) needs a license compatible with MIT; share-alike data needs Tom's
OK.
