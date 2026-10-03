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
  as a text list: Puzzle Games, Strategy Games, Word Games, Dice Games,
  Other Games. A category opens a page of game icons (paged if needed).
- Inside a game, the ☰ menu has: new game, restart, stats, Settings,
  All games.
- **Settings** is shared: Theme, Sound On/Off (silent play), brightness,
  invert colors, swap red/blue, recalibrate, touch test, player name (once
  multiplayer exists).
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
| Mancala (Kalah) | Two rows of six pits |
| Tic-Tac-Toe / Ultimate Tic-Tac-Toe | Ultimate is the interesting one |
| Nine Men's Morris | Big targets |
| Gomoku | 15x15, 16 px cells on 240-wide boards |
| You Sunk My CYD! (Battleship) | Two grids, flip between them |

**Word**
| Game | Notes |
|---|---|
| CYD-dle (Wordle-style) | Public-domain word list; on-screen keyboard |
| Trivia | Question bank license to check (Open Trivia DB is CC BY-SA) |
| Hangman | word guess by letter with limit to bad guesses |
| Wheel of CYD (Wheel of Fortune) | phrase guess with spinning reward/fail aspect |

**Dice**
| Game | Notes |
|---|---|
| Yaht-CYD | Yahtzee by a non-trademarked name |
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
- Games: chess, checkers, Reversi, FourConnect first; later Mancala,
  Ultimate Tic-Tac-Toe, Nine Men's Morris, You Sunk My CYD!, Gomoku.
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
   square" board UI, AI task on core 0, pass-and-play.
4. **Multiplayer** (ESP-NOW) for FourConnect, Tic-Tac-Toe, Reversi,
   Checkers, Chess.
5. **More puzzles:** Minesweeper, Nonograms, MasterCYD, Memory Match,
   2048, Peg Solitaire.
6. **Word and dice games:** CYD-dle, Yaht-CYD, Farkle; Trivia if a
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
- Sound with a Settings toggle for silent play; custom themes.

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
  with a `dev-<sha>` build; releases on request.
- GitHub Releases: one merged (factory) `.bin` per board, flashed at 0x0.

## 9. Licensing
MIT. See `THIRD_PARTY_NOTICES.md`. Game data (word lists, questions,
levels) needs a license compatible with MIT; share-alike data needs Tom's
OK.
