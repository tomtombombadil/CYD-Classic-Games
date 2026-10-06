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
| Pipe Race (Pipe Mania / Pipe Dream) | *Built 2026-10-06*: id `piperace`; 8x8, queue of 5, swap -50, goal 10 + 2 a level, faster each level, rocks from level 3, Water Now / Fast Flow (double points) |


**Strategy (vs computer, pass-and-play, CYD vs CYD)**
| Game | Notes |
|---|---|
| Chess | Own MIT engine; levels by depth/time |
| Checkers | Strong AI is cheap |
| Reversi (Othello) | Strong AI is cheap |
| FourConnect (Connect Four) | Can play perfectly; easy levels hold back |
| Mancala (Kalah) | Two rows of six pits (*built 2026-10-03*: portrait, pits in two columns, stores in the middle) |
| Tic-Tac-Toe / Ultimate Tic-Tac-Toe | Ultimate is the interesting one (*built 2026-10-05*: id `ultimate`, wireless key 9) |
| Nine Men's Morris | Big targets (*built 2026-10-03*) |
| Gomoku | 15x15, 16 px cells on 240-wide boards (*built 2026-10-05*: freestyle five, wireless key 10) |
| Strategy Go! (Stratego) | *Next (Tom, 2026-10-06)*: 10x10, hidden pieces - wireless like You Sank My CYD!, pass-and-play with a cover screen |
| Acquisitions (Acquire) | *Next (Tom, 2026-10-06)*: 12x9 hotel tiles, chains, stock, mergers; computer players |
| Sorry! / Trouble style race game | *Planned (Tom, 2026-10-06)*: 2-4 players, pass-and-play + computer; own name |
| Escape from Atlantis style | *Planned (Tom, 2026-10-06)*: sinking hex island, sharks and whales; own name |
| You Sank My CYD! (Battleship) | Two grids, flip between them (*built 2026-10-05*: id `sank`; classic rules - ships may touch; manual placement by default, Random in Options; wireless key 8, version 2) |

**Word**
| Game | Notes |
|---|---|
| CYD-dle (Wordle-style) | Public-domain word list; on-screen keyboard |
| Trivia | Open Trivia DB, CC BY-SA 4.0 - OK'd by Tom 2026-10-06 (data file stays CC BY-SA, code MIT). One shared question bank for planned trivia shows (Millionaire, Hollywood Squares, multiple-choice Jeopardy, a Trivial Pursuit style board) |
| ~~Hangman~~ | Dropped (Tom, 2026-10-03): needs a keyboard, not much fun vs a computer |
| Wheel of CYD (Wheel of Fortune) | phrase guess with spinning reward/fail aspect (*built 2026-10-05*: id `wheel`; you vs Max and Zoe or 2-player pass-and-play; 502 original puzzles) |

**Game shows** (*planned, Tom 2026-10-06*; own names)
| Game | Notes |
|---|---|
| Deal or No Deal style | 26 cases, a banker offer formula; no content needed |
| Press Your Luck style | 18-square board with jumping lights; spins earned without trivia |
| Card Sharks style | Higher or lower on the shared cards |

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
- **Redesign decided 2026-10-04 (Tom, situation review); built in v0.21.0.**
  Replaces the matching parts above. Built as written, plus: the request
  popup going away says "Ann cancelled the request." (names are silly
  words: no her/his), or "... stopped waiting for an answer." after 30 s;
  a partner's forfeit / closed game pops a notice with OK over the board;
  game-over keys read Again / New Game / Goodbye (no room for the name);
  Move Timer Off waits 60 s before "No reply"; titles show the player's
  name alone ("Play With Bob" doesn't fit 240 px).
  - Finding: a board doesn't look for players all the time. 2P = the board
    listens and answers; it only sends when its player searches (a "who's
    there?" call that boards in 2P answer with their info) or is in a game.
    "Searching..." while it looks. The list: "Bob - available" ("free" if
    "available" doesn't fit), "Bob - busy", "Bob - no games", "Bob - needs
    update". Busy players can't be picked: no requests interrupt a 2P game.
  - Names carry the board ID internally (protocol, code, log); players only
    ever see the chosen name. No collisions.
  - Picking a player shows the games: playable (both players offer it) lit,
    the rest greyed out.
  - Requesting: "Requesting..." The answers are **Play**, **No Thanks**,
    **Other Game** (no "Not Now"). Other Game = Bob picks the game he'd
    rather play: a counter-request back to Ann (same three answers).
  - Answers as Ann sees them: "Bob said 'no thanks'." / "Bob can't play
    right now." (became busy after being picked; a second asker gets this
    too - the first asker has priority) / "That game is no longer
    available." / "There was no answer from Bob." / "Bob went out of
    range." Bob sees "Ann cancelled her request." when she stops.
  - Agreed: "Connecting..." until the game starts; a little trill on both
    boards when it does. The **requested player moves first** (in games
    where it matters).
  - A one-player game of the same game is saved and comes back silently
    once the 2P game is over: "Resuming your previous one player game."
    (never mention the computer).
  - Leaving a 2P game = forfeit: "Bob left and forfeit the game." Clear 2P
    Sessions while the partner is connected and the game is on = forfeit.
  - Game over: Play Again, choose a New Game, or "Goodbye Bob" (ends the
    session).
  - Draws: no draw offer; draws only by the game's rules.
  - **Move Timer** (a 2P setting; Tom's name): 30 s (default), 1 min, 2 min, 5 min, Off - one
    setting for every game. The shorter of the two players' settings applies
    (Off = no limit); a player is told only when it differs from theirs.
    Countdown in the header: "Waiting... 30s" for the waiting player,
    "Respond in 30s" for the player to move.
  - Two different failures (Tom, 2026-10-04):
    - **Player doesn't respond** (communication fine): at 0 the late player
      gets "You haven't responded in time. You will forfeit if you do not
      respond in 10 seconds." and 10 more seconds; then they forfeit. The
      player can close that pop-up and still has the 10 seconds to move.
    - **Communication fails** (range, power, interference - not anyone's
      choice): after a whole move time with nothing heard: "No reply from
      Bob. Do you want to close the game, or keep waiting?" Close = "Communications
      failed. Game not counted." Keep Waiting = one more move time; if still
      nothing, no loss and no forfeit: the session is saved and resumed if
      Bob comes back in range: "Bob is back in range. Continue Chess?"
      Continue / Close Game on both boards; it goes on only if both tap
      Continue, else it ends not counted. A saved session **survives a
      restart** (Tom: DIY boards, maybe run by children - an oops reboot
      mustn't kill the fun; replaces "a restart clears the session"), the
      2P icon is lit while it waits, and Clear 2P Sessions always removes it.
  - The one allowed "are you sure": leaving a 2P game (the back arrow, Exit
    Game) says "Leaving will forfeit this game." Keep Playing / Leave Game.
    The menu's Forfeit Game key acts at once.
  - Requests say only "Ann would like to play Chess." (no "you move first").
  - "Bob left and forfeited the game."
  - List versions: "Bob - needs update" when this board's version is higher
    (tapping it makes clear **Bob** must update); "Bob - later version"
    when this board's is lower (tapping it makes clear **this** board must
    update). Both pages show the web flasher's address.
  - Radio: 2P = listen silently; send only while the player searches (and
    in a game). Answer only calls from CYD Classic Games boards. The search
    call and its answer must stay readable by every later version, so an
    older board can still say "needs update".
- **Battery (Tom, 2026-10-04):** an idle 2P board dozes (the radio wakes
  120 ms every 2 s, the driver's ESP-NOW power saving - no stop/start,
  which is too slow); boards looking for it repeat every 100 ms, so it is
  found or asked within about 2 s ("Searching..." / "Requesting..." on
  screen). Fully awake during anything else (CLAUDE.md has the list).
- **HARD RULE (Tom, 2026-10-04): no free-form communication between
  players, ever** - no chat, messages or any player-filled field on the
  air. Only fixed codes: presence, versions, games offered, requests and
  their fixed answers, moves, game state. (CLAUDE.md has the full rule.)
- **Player names are picked, never typed** (Tom, 2026-10-04, from the hard
  rule): two words, one from each of two fixed lists in the firmware -
  fun, funny and silly, kid-safe ("Wobbly Llama", "Turbo Penguin"). The name
  page has **Random** and **Pick From List** (pick a word from each list).
  On the air a name is two numbers plus the board ID; no typed text ever
  leaves a board. The lists are **append-only** (a word's number never
  changes), so every version shows the same name.
- Compatibility (Tom, 2026-10-04): boards play together when their
  **link version** (finding, requests, game sync) and that **game's
  version** (its moves and rules) match - not their firmware version.
  Every wireless game has a fixed **game key** (never its place in the
  list) and every move travels as a **move key** that describes the move
  itself (e.g. Chess from-square, to-square, promotion), never its place in
  a generated move list - so lists can change without changing what's
  sent. A test in CI plays fixed games per wireless game and fails the
  build if the moves sent change while the game's version didn't. The
  first "who's there?" call and its answer keep a frozen layout forever.
  List: "needs update" / "later version" only when nothing can be played;
  otherwise the player is "available" and games that differ are greyed
  with "needs update" on the games page.
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
  Nine Men's Morris, You Sank My CYD! (kNetwork in games.def +
  `match::Game::legal`). Farkle
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
   Checkers, Chess, Mancala, Nine Men's Morris. *Built 2026-10-04.* You
   Sank My CYD! joined 2026-10-05.
6. **Word and dice games:** (CYD-dle, Yaht-CYD done in stage 3) Farkle (*built 2026-10-03*), RPG Dice roller (*built 2026-10-03*); Trivia if a
   suitable question bank is found.
7. Remaining candidates as Tom picks them.
8. **Tom's list (2026-10-06):** first Pipe Race, Acquisitions, Strategy
   Go!; then (tentative) Deal or No Deal, Sorry!/Trouble, Press Your
   Luck, Card Sharks and Escape from Atlantis style games (own names);
   then the trivia family on Open Trivia DB. Assessed as poor fits:
   Family Feud (needs real survey data), Feudal (24x24 board too small).
   Big projects for later: Catan, Carcassonne, a Gold Box style dungeon
   crawl (SRD 5.1, CC BY 4.0), a B-17 style solo mission game of our own.

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
- Names: Light Switch, MasterCYD, SokoCYD, KenCYD, You Sank My CYD!,
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
