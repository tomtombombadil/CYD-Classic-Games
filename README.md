# CYD Classic Games

Classic games for the ESP32 "Cheap Yellow Display" family: touch-friendly,
portrait, built for a stylus on a resistive screen. Two-player games will
play CYD to CYD over WiFi (ESP-NOW, no router needed).

## WEB FLASHER!!!
I know you just want to flash this to your CYD right now, so here's the web flasher:
**[CYD Classic Games Web Flasher](https://tomtombombadil.github.io/CYD-Classic-Games/)**
(Chrome or Edge on a computer).

> **Status:** growing, built and tested on the PC preview; hardware testing
> under way. Ten games so far: **Sudoku** (from
> [CYD-Sudoku](https://github.com/tomtombombadil/CYD-Sudoku) v1.0.0),
> **Light Switch**, **Sliding Tiles**, **FourConnect**, **Tic-Tac-Toe**,
> **Reversi**, **Checkers**, **Chess**, **CYD-dle** and **Yaht-CYD**.
> More are on the way; see [docs/SPEC.md](docs/SPEC.md) for the plan.

<img src="assets/splash/splash1_240x320.jpg" width="240" alt="Tom Tom Bombadil's CYD Classic Games splash screen">

## Screenshots

**2.8" and 3.2" boards (240×320)**: the game picker, the Strategy Games
page, Sudoku and its menu, Chess (and a long-press showing where a piece can
go), Checkers, Reversi, FourConnect, Tic-Tac-Toe, CYD-dle, Yaht-CYD,
Sliding Tiles, Light Switch, Settings and the custom theme editor.

<p>
<img src="docs/screenshots/small_picker_light.png" width="240" alt="Game picker: Continue card and categories">
<img src="docs/screenshots/small_strategy.png" width="240" alt="Strategy Games page">
<img src="docs/screenshots/small_sudoku_light.png" width="240" alt="Sudoku in progress">
<img src="docs/screenshots/small_sudoku_menu.png" width="240" alt="Sudoku menu with Exit Menu and Exit Game">
<img src="docs/screenshots/small_chess.png" width="240" alt="Chess against the computer">
<img src="docs/screenshots/small_chess_peek.png" width="240" alt="Chess: long-press shows where a piece can move">
<img src="docs/screenshots/small_checkers.png" width="240" alt="Checkers">
<img src="docs/screenshots/small_reversi.png" width="240" alt="Reversi with legal moves shown as dots">
<img src="docs/screenshots/small_fourconnect.png" width="240" alt="FourConnect against the computer">
<img src="docs/screenshots/small_tictactoe.png" width="240" alt="Tic-Tac-Toe">
<img src="docs/screenshots/small_cyddle.png" width="240" alt="CYD-dle word game">
<img src="docs/screenshots/small_yahtcyd.png" width="240" alt="Yaht-CYD dice game">
<img src="docs/screenshots/small_sliding.png" width="240" alt="Sliding Tiles 4x4">
<img src="docs/screenshots/small_lightswitch.png" width="240" alt="Light Switch">
<img src="docs/screenshots/small_settings_dark.png" width="240" alt="Settings, dark theme">
<img src="docs/screenshots/small_theme_editor.png" width="240" alt="Custom theme editor">
</p>

**3.5" and 4.0" boards (320×480)**: the picker, Chess, CYD-dle and Yaht-CYD
(dark theme), the two-player menu and Reversi.

<p>
<img src="docs/screenshots/large_picker_light.png" width="320" alt="Game picker on a 320x480 board">
<img src="docs/screenshots/large_chess_dark.png" width="320" alt="Chess, dark theme">
<img src="docs/screenshots/large_cyddle_dark.png" width="320" alt="CYD-dle solved, dark theme">
<img src="docs/screenshots/large_yahtcyd_dark.png" width="320" alt="Yaht-CYD, dark theme">
<img src="docs/screenshots/large_twoplayer_menu.png" width="320" alt="Two-player menu: computer levels, pass and play">
<img src="docs/screenshots/large_reversi_dark.png" width="320" alt="Reversi, dark theme">
</p>

## Games

| Game | Category | Play |
|---|---|---|
| Sudoku | Puzzle Games | Solo, four levels graded by solving technique |
| Light Switch | Puzzle Games | Solo, Easy / Medium / Hard, with par and hints |
| Sliding Tiles | Puzzle Games | Solo, 3x3, 4x4 (the 15-Puzzle) or 5x5 |
| Chess | Strategy Games | vs computer (Easy / Medium / Hard) or pass-and-play |
| Checkers | Strategy Games | vs computer (Easy / Medium / Hard) or pass-and-play |
| Reversi | Strategy Games | vs computer (Easy / Medium / Hard) or pass-and-play |
| FourConnect | Strategy Games | vs computer (Easy / Medium / Hard) or pass-and-play |
| Tic-Tac-Toe | Strategy Games | vs computer (Hard never loses) or pass-and-play |
| CYD-dle | Word Games | Solo, guess the five-letter word: Easy / Normal / Hard |
| Yaht-CYD | Dice Games | Solo, five dice, 13 boxes, beat your best score |

Two-player games will also play **wireless, CYD to CYD** (the menu shows it,
greyed out, until it's built). Coming next: more games (Minesweeper,
MasterCYD, ...), then wireless play. The plan is in
[docs/SPEC.md](docs/SPEC.md).

## Starting up and the game picker

The board shows one of three title screens (a different one each time it
starts). Tap anywhere to go on to the game picker.

- **Continue** (the yellow card) reopens the last game you played, where you
  left off, in one tap. It shows how far along that game is.
- Below it, the categories: **Puzzle Games, Strategy Games, Word Games, Dice
  Games, Other Games**, each with how many games it has. Tap one to see its
  games as icons, and tap a game to play. The **<** key in the top bar goes
  back. Categories without games yet say "soon".
- **☰ menu:** Settings, shared by every game.
- Every game's ☰ menu ends with two keys side by side: **Exit Menu**
  (bottom left) closes the menu and goes back to the game; **Exit Game**
  (bottom right) saves the game and comes back here.

Every game saves itself and keeps its own stats. Theme, sound, brightness
and touch calibration are shared.

## Settings

**☰ → Settings** (from the picker or any game):

- **Theme:** Light, Dark, or one of three **Custom** themes. Pick Custom 1, 2
  or 3, then **Edit** it: each of the 10 color buttons (background, board,
  grid lines, text, your marks, selected, matching, row/column, buttons,
  accent) opens a palette of 48 colors. Tap one and the change shows at
  once, in every game. **Default** puts a color back; **Reset: Light /
  Dark** starts the theme over from Light or Dark.
- **Volume:** a slider, like Brightness. It starts at 50 % (tiny speakers
  distort near the top). Tap left of the slider for **Muted**, silent
  play. A sound plays at the new level when you let go. Sounds come
  through a speaker on the board's speaker connector, and only for what
  matters in a game: moves, the computer's reply, mistakes, hints and the
  end of a game. Plain button taps are silent.
- **Brightness**, **Invert Colors**, **Swap Red/Blue**, **Recalibrate** and
  **Touch Test** (see below).

## Two-player games

Chess, Checkers, Reversi, FourConnect and Tic-Tac-Toe share one menu:
**New Game vs Computer** (Easy, Medium, Hard), **Pass and Play** (two people
share one board and take turns), and Wireless (coming). Against the computer
you and the computer take turns going first, game by game. The computer
thinks on the board's second processor core, so the screen never freezes.
Its levels look ahead fewer or more moves (and think longer); it never
throws a game on purpose. Leaving a started game against the computer for a
new one counts as a loss.

- **Chess:** tap one of your pieces (dots show where it can go), then tap
  the square. Castling, en passant and promotion are all there; a pawn
  reaching the far side asks what it becomes. Checkmate, stalemate,
  threefold repetition, the 50-move rule and too little material all end
  the game. Easy looks 2 moves ahead, Medium 3 (up to 2 seconds), Hard up to
  6 (up to 6 seconds a move).
- **Checkers:** tap a piece, then where it lands. Jumps are compulsory, so
  when one is possible only the pieces that can jump respond. A double or
  triple jump is tapped one landing at a time. A piece reaching the far row
  is crowned and moves both ways.
- **Reversi:** your legal moves show as dots; tap one. If you have no move
  your turn passes. Hard plays the last 10 squares perfectly.
- **FourConnect:** tap anywhere in a column to drop a disc there. Four in a
  row (across, up or diagonal) wins; the four get a ring. A dot marks the
  last disc played.
- **Tic-Tac-Toe:** tap a square. Hard plays perfectly: the best you can do
  is a draw.
- **Long-press** (Chess and Checkers): hold a piece, yours or the other
  side's, to see where it could move. The next tap clears it.
- **Play Again** appears when a game ends.

## CYD-dle

Guess the five-letter word. Type a guess on the keyboard and tap ✓. Each
letter turns green (right letter, right spot), gold (in the word, wrong
spot) or grey (not in the word); the keyboard keeps track of every letter.
**Easy** gives 7 guesses, **Normal** 6, and **Hard** 6 where every green
and gold letter must be used in the next guesses. Guesses must be real
words. When the game is over, ✓ starts a new word. Stats keep your solves
and guess counts per level.

## Yaht-CYD

Five dice, 13 turns. Tap **Roll**, tap dice to hold them (held dice turn
gold) and roll again, up to three rolls. Then tap an empty box on the score
card: after each roll the empty boxes show what they would score.

- Upper boxes (Ones to Sixes) score the matching dice; 63 or more there
  earns a 35 bonus.
- 3 / 4 of a Kind (sum of all dice), Full House 25, Run of 4 30, Run of 5
  40, **Yaht-CYD** (five alike) 50, Chance (sum of all dice).
- Every extra Yaht-CYD after a scored 50 is worth 100 more and is a joker
  (it goes in its number's upper box if that's empty, otherwise anywhere,
  and Full House and the runs count in full).
- Stats keep every game's score, your best and your average.

## Light Switch

Tapping a light flips it and its four neighbours. Turn every light off. The
top bar shows your moves and **par**, the fewest presses that can solve this
board. **Hint** works like Sudoku's: the first tap outlines a light that is
part of a shortest solution, the second tap presses it.

## Sliding Tiles

Put the tiles in order with the gap last. Tap any tile in the gap's row or
column: it and the tiles between it and the gap slide over. Tiles already in
their home spot are tinted. 3x3 (Easy), 4x4 (the classic 15-Puzzle) and
5x5 (Hard).

## Sudoku

The screen, top to bottom: clock, difficulty and the **☰ menu**; the board;
the tool row (**Undo**, **Notes**, input mode, **Hint**); the digits 1-9.

- **Input mode** — the third button shows the current mode; tap it to switch.
  Remembered between games. **Digit 1st** is the default.
  - **Digit 1st:** tap a digit, then tap cells to place it. Tap a cell that
    already holds that digit to clear it; a different digit is replaced.
    Tap a given (black) digit to pick that digit. Picking a digit clears the
    cell highlight. When the last of a digit is placed it greys out and is
    deselected.
  - **Cell 1st:** tap a cell, then a digit. Tap the same digit again to
    clear it.
- **Notes:** while on, digits add or remove pencil marks instead of answers.
  Placing a digit removes it from the notes in its row, column and box.
- **Undo** steps back one action. Undoing a placement also brings back the
  notes it cleared.
- **Hint:** the first tap points at a cell (the top bar says "Tap Hint to
  fill"); a second tap fills in the right digit, shown in green and locked.
  It points at your selected cell if that one is empty or wrong, else at a
  wrong entry if there is one, else at the easiest empty cell. Hints are
  counted in your stats.
- On the 3.5" and 4.0" boards the small number under each digit is how many
  of that digit are left to place. The 2.8"/3.2" boards leave it out so the
  board can be bigger.
- Clashing digits are tinted red. A digit button greys out once all nine are
  placed correctly.
- **The clock** only runs while you play. After 2 minutes with no touch it
  stops and the top bar says **Paused**; the next touch starts it again. It
  stops while a menu is open.
- **Solving** flashes the screen (colors invert a few times) and leaves the
  finished board on screen. Open the ☰ menu when you're ready for a new game.
- **☰ menu:** new game (Easy, Medium, Hard, Expert), **Restart This
  Puzzle**, **Stats**, **Settings**, then **Exit Menu** | **Exit Game**.
  Buttons act on the first tap; nothing asks "are you sure".

### Difficulty

Every puzzle has exactly one solution, and its level is set by the hardest
solving technique it needs (checked by a built-in solver that works the way
a person does):

| Level | Needs | Never needs |
|---|---|---|
| Easy | naked and hidden singles only (36+ clues) | anything else |
| Medium | pointing / claiming (locked candidates) | pairs, triples, fish |
| Hard | naked/hidden pairs and triples, X-Wing | Swordfish, wings |
| Expert | Swordfish, XY-Wing or XYZ-Wing | chains or guessing |

No puzzle ever needs guessing. While Sudoku is open the board keeps two
puzzles of each level ready in the background, so new games start at once.

### Coming from CYD-Sudoku?

Flash this over CYD-Sudoku **without** erasing and your Sudoku game in
progress, stats, touch calibration, color fixes and settings carry over.

## Stats

Each game keeps its own history. For Sudoku: every solved puzzle with its
difficulty, time and hints used, and every puzzle you leave for a new game
after playing it (marked "Gave up"). The Stats screen shows solves, average
and best time per difficulty, and your most recent games. **Delete Last**
removes the most recent entry and **Clear All** wipes that game's history.
The puzzles record level, moves and time (best time and fewest moves per
level). CYD-dle records guesses per level and Yaht-CYD every
score. Two-player games record wins, losses and draws against each
computer level, and who won each pass-and-play game.

- **With a microSD card** (3.2", 3.5" and 4.0" boards): saved to
  `CYD-Classic-Games/<game>.csv` on the card (e.g. `sudoku.csv`), full
  history, opens in Excel. Anything recorded before the card went in is
  moved onto it.
- **Without a card**, and on the 2.8" boards for now: saved in the board's
  memory (the most recent 250 games per game). The 2.8" board's SD slot
  shares a controller with its touch screen and needs a driver change first.

## Install

**Easiest:** open the [web flasher](https://tomtombombadil.github.io/CYD-Classic-Games/)
in Chrome or Edge, plug in the board, pick it from the list and click Install.

Or download the `.bin` for your board from [Releases](../../releases) and
flash it at address **0x0** with Espressif's Flash Download Tool.

## Supported boards

Boards are named by screen size, display driver and touch type. Check the
text printed on the back of the board. The `TTB-CYD-CG_` prefix marks the
file as CYD Classic Games.

| Firmware file | Printed on the back | Tested |
|---|---|---|
| `TTB-CYD-CG_2.8in_ILI9341_Resistive.bin` | ESP32-2432S028 (often with R) — usually single micro-USB | **Untested** |
| `TTB-CYD-CG_2.8in_ST7789_Resistive.bin` | ESP32-2432S028 (often with R) — usually micro-USB + USB-C | Yes |
| `TTB-CYD-CG_3.2in_ST7789_Resistive.bin` | 3.2" LCD Display, ESP32-32E, 240x320, Resistive Touch | Yes |
| `TTB-CYD-CG_3.5in_ST7796_Resistive.bin` | 3.5" LCD Display, ESP32-32E, 320x480, Resistive Touch | **Untested** |
| `TTB-CYD-CG_4.0in_ST7796_Resistive.bin` | 4.0" LCD Display, ESP32-32E, 320x480, Resistive Touch | Yes |

**Untested:** the 2.8" ILI9341 and 3.5" ST7796 builds use the same code and
pin maps as their tested siblings but haven't been run on that hardware.
They should work; please [open an issue](../../issues) either way.

Not sure which 2.8" you have? Try ILI9341 first. Wrong colors: see below.
Garbled or blank screen: install the other 2.8" version.

## Building (Windows, VS Code + PlatformIO)

1. Install VS Code and the **PlatformIO IDE** extension.
2. Clone this repo and open the folder in VS Code.
3. In the blue status bar, click the `env:` selector and choose your board
   (e.g. `cyd32_st7789_res` for the 3.2" ST7789 board).
4. Click **Build** (✓), then **Upload** (→) with the board plugged in.
5. **Serial Monitor** (plug icon) shows boot logs at 115200 baud.

## Brightness

**☰ → Settings → Brightness** sets the backlight. It never goes fully
dark, and it's remembered.

## Touch calibration

On first boot (or when the **BOOT** button is held while powering on), the
screen shows corner arrows — tap each tip precisely. The calibration is saved
to flash and reused. To redo it: **☰ → Settings → Recalibrate**.

## Colors look wrong?

Panels vary between production runs. Open **☰ → Settings**: use
**Invert Colors** if colors look like a photo negative, and **Swap Red/Blue**
if blue shows as red. The fix is saved on the board.
For a dark screen on purpose, use **Theme: Dark** instead.

## License

[MIT](LICENSE). Third-party components and what may be borrowed from where:
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
