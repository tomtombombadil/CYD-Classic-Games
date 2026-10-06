# Generates src/games/<id>/<id>_help.cpp (How To Play pages) from the texts
# below. Edit the text here, run `python3 tools/make_help.py`, commit both.
import pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent / "src" / "games"
B = "\\xE2\\x80\\xA2 "   # bullet as C escape

def c(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"').replace("•", "@@B@@")
    lines = s.split("\n")
    out = []
    for i, l in enumerate(lines):
        l = l.replace("@@B@@", B).replace("@@OK@@", '" "\\xEF\\x80\\x8C" "').replace("@@BS@@", '" "\\xEF\\x95\\x9A" "')
        out.append('"' + l + ('\\n' if i < len(lines) - 1 else '') + '"')
    return "\n        ".join(out)

def write(gid, pages, two_player=False):
    body = []
    for title, text in pages:
        if text is None:
            body.append('    {"%s", kHelpTwoPlayer},' % title)
        elif text == "@WIRELESS":
            body.append('    {"%s", kHelpWireless},' % title)
        else:
            body.append('    {"%s",\n        %s},' % (title, c(text)))
    src = f'''// How To Play pages for this game (see src/games/help.h). Plain data.
#include "games/help.h"
{'#include "games/common/help_common.h"' + chr(10) if two_player else ''}
namespace {{

using games::HelpPage;
{'using games::kHelpTwoPlayer;' + chr(10) if two_player else ''}{'using games::kHelpWireless;' + chr(10) if any(t == "@WIRELESS" for _, t in pages) else ''}
const HelpPage kPages[] = {{
{chr(10).join(body)}
}};

}} // namespace

CYD_HELP({gid}, kPages)
'''
    (ROOT / gid / f"{gid}_help.cpp").write_text(src)

TP = ("New Games", None)
WL = ("Wireless", "@WIRELESS")      # the two-player games that play CYD to CYD

write("sudoku", [
 ("The Idea", "Fill the 9x9 grid so every row, every column and every 3x3 box holds the digits 1 to 9 once each.\nEvery puzzle has exactly one answer, and none ever needs guessing: the level says which solving techniques it needs."),
 ("Entering Digits", "The third key in the tool row switches the input mode.\n• Digit 1st: tap a digit, then the cells it goes in. Tap a cell holding that digit to clear it.\n• Cell 1st: tap a cell, then a digit. The same digit again clears it.\nClashing digits turn red."),
 ("Notes, Undo, Hint", "• Notes: digits add or remove pencil marks instead. A placed digit clears itself from the notes it sees.\n• Undo steps back one action.\n• Hint: the first tap points at a cell, the second fills it in (green). Hints count in your stats."),
 ("Levels And Stats", "Easy: singles only. Medium: adds locked candidates. Hard: pairs, triples, X-Wing. Expert: Swordfish, XY- and XYZ-Wing.\nThe clock stops after 2 minutes without a touch. Stats keep every solve, time and hints used."),
])

write("lightswitch", [
 ("The Idea", "Turn every light off.\nTapping a light flips it and its four neighbours (above, below, left, right): on goes off, off goes on.\nEvery board can be solved. Tapping the same light twice undoes it, so the order of your taps doesn't matter."),
 ("Par And Hints", "Par in the top bar is the fewest taps that solve this board. Try to match it.\nHint: the first tap outlines a light that is part of a shortest solution; the second tap presses it.\nEasy, Medium and Hard start from more scrambled boards."),
])

write("sliding", [
 ("The Idea", "Put the tiles in order, 1 at the top left, reading across like a book, with the gap last.\n3x3 is quick, 4x4 is the classic 15-Puzzle, 5x5 is a long haul."),
 ("Moving Tiles", "Tap any tile in the gap's row or column: it and every tile between it and the gap slide over by one.\nTiles already in their home spot are tinted, so you can see what's done.\nStats keep your best time and fewest moves for each size."),
])

write("minesweeper", [
 ("The Idea", "Open every cell that isn't a mine.\nA number tells how many of the 8 cells around it hide a mine. Use the numbers to work out where the mines are.\nThe first tap always opens an area, and every board can be cleared by logic alone: no guessing needed."),
 ("Dig And Flag", "• Dig (key under the board): a tap opens a cell.\n• Flag: a tap marks a cell you know is a mine; tap again to remove it.\n• Long-press a hidden cell to flag it in either mode.\n• Tap a number whose mines are all flagged to open the cells around it."),
 ("Winning", "Open every safe cell to win. Opening a mine ends the game and shows the rest; a crossed-out flag was wrong.\nThe top bar counts mines minus flags.\nEasy 8x10 with 10 mines, Medium 9x11 with 15, Hard 10x11 with 20."),
])

write("fourconnect", [
 ("The Idea", "Two players take turns dropping discs into the 7 columns. A disc falls to the lowest free spot.\nThe first to get four of their discs in a line wins: across, up and down, or diagonal. A full board with no line is a draw."),
 ("Playing", "Tap anywhere in a column to drop your disc there.\nA dot marks the last disc played. The winning four get a ring.\nPlay Again appears when the game ends."),
 TP,
 WL,
], True)

write("tictactoe", [
 ("The Idea", "X and O take turns marking the 3x3 grid; X goes first.\nThree of your marks in a row, column or diagonal wins. A full grid with no line is a draw.\nTap a square to mark it."),
 ("The Computer", "Easy looks one move ahead, Medium two. Hard plays perfectly: you can't beat it, but you can always hold it to a draw.\nPlay Again appears when the game ends."),
 TP,
 WL,
], True)

write("reversi", [
 ("The Idea", "Black and White take turns placing discs; Black goes first.\nA disc must outflank: it traps a straight line of the other side's discs between itself and one of yours. Every trapped disc flips to your color, in every direction at once."),
 ("Playing", "Your legal moves show as dots. Tap one to play it.\nIf you have no legal move your turn passes. The game ends when neither side can move; the most discs wins.\nThe top bar counts both sides' discs."),
 ("The Computer", "Easy looks 2 moves ahead, Medium 4, Hard 6. Hard also plays the last 10 empty squares perfectly."),
 TP,
 WL,
], True)

write("checkers", [
 ("The Idea", "Pieces move diagonally on the dark squares; Black goes first.\nA man moves one step forward. Jump an enemy piece by hopping over it to the empty square beyond; it's removed.\nTake all the other side's pieces, or leave them no move, to win."),
 ("Jumps And Kings", "Jumping is compulsory: if you can jump, you must. After a jump, the same piece keeps jumping while it can.\nA man reaching the far row is crowned king and moves both ways. Crowning ends the move.\n40 moves each with no jump and no man moving is a draw."),
 ("Playing", "Tap one of your pieces (its landing squares show as dots), then tap where it goes. When a jump is possible, only pieces that can jump respond.\nA double jump is tapped one landing at a time.\nTap one of the other side's pieces to see where it can go; the next tap clears that."),
 TP,
 WL,
], True)

write("chess", [
 ("Playing", "Tap one of your pieces: dots show where it can go. Then tap the square.\nA pawn reaching the far side asks what it becomes (Queen, Rook, Bishop, Knight).\nTap one of the other side's pieces to see its moves; the next tap clears that."),
 ("The Rules", "White moves first. Castling, en passant and promotion all work. A king in check has its square tinted red; you must get it out of check.\nCheckmate wins. Stalemate, threefold repetition, 50 moves with no capture or pawn move, and too little material to mate are draws."),
 ("The Computer", "Easy looks 2 moves ahead. Medium looks 3, thinking up to 2 seconds. Hard looks up to 6, thinking up to 6 seconds a move.\nThe screen stays usable while it thinks. Against the computer you and it take turns playing White."),
 TP,
 WL,
], True)

write("cyddle", [
 ("The Idea", "Find the hidden five-letter word.\nType a guess on the keyboard and tap @@OK@@ (the check key). Each letter then turns:\n• green: right letter, right spot\n• gold: in the word, but elsewhere\n• grey: not in the word"),
 ("Guessing", "Guesses must be real words. The keyboard colors each letter by what you've learned so far. The @@BS@@ key removes the last letter.\nWhen the game is over, @@OK@@ starts a new word."),
 ("Levels", "Easy: 7 guesses. Normal: 6 guesses.\nHard: 6 guesses, and every green letter must stay in place and every gold letter must be used in later guesses.\nStats keep wins, losses and guess counts for each level."),
])

write("yahtcyd", [
 ("The Idea", "Score as many points as you can with five dice in 13 turns.\nEach turn: tap Roll, then tap dice to hold them (they turn gold) and roll the rest again, up to three rolls. Then tap an empty box on the score card to score that roll. Every box is used once; used boxes turn blue."),
 ("Upper Boxes", "Ones to Sixes score the total of the matching dice: three 4s in Fours = 12.\nIf the upper boxes add up to 63 or more (three of each number), you earn a 35 point bonus.\nAfter each roll, empty boxes show what they would score."),
 ("Lower Boxes", "• 3 / 4 of a Kind: total of all dice\n• Full House (3 + 2 alike): 25\n• Run of 4 (four in a row): 30\n• Run of 5: 40\n• Yaht-CYD (five alike): 50\n• Chance: total of all dice\nA box that doesn't fit scores 0."),
 ("Extra Yaht-CYDs", "Each extra Yaht-CYD after a scored 50 earns 100 more. It must go in its number's upper box if that's empty; else in an open lower box, where Full House and the runs score in full; only with those all used, in an upper box for 0.\nStats keep every game's score, your best and average."),
])

write("twenty48", [
 ("The Idea", "Slide the tiles to merge them. Two equal tiles that meet join into one worth their sum: 2 + 2 = 4, 4 + 4 = 8, and on up.\nAfter every slide a new 2 (sometimes a 4) appears. Make a 2048 tile to win, then keep going for a higher score."),
 ("Sliding", "Tap toward the side you want the tiles to go: above the board slides up, below it down, left or right of it sideways.\nThe board's two diagonals split the whole screen into these four zones, so a tap anywhere works. A tap that moves nothing does nothing."),
 ("Score And End", "Each merge adds the new tile's value to your score. The newest tile has a dark ring.\nThe game ends when no slide can move a tile. Stats keep games played to the end, and any game that reached 2048."),
])

write("mastercyd", [
 ("The Idea", "The CYD hides a code of colored pegs. Crack it in 10 guesses.\nAfter each guess, small marks next to it tell you:\n• a filled dot for each peg that is the right color in the right place\n• a ring for each right color in the wrong place\nThe marks don't say which pegs they mean."),
 ("Guessing", "Tap a color at the bottom to put it in the next empty slot. Tap a slot in your row to take its peg out.\nWhen the row is full, tap Check.\nThe code shows at the top when the game ends."),
 ("Levels", "Easy: 4 pegs, no color used twice.\nNormal: 4 pegs, colors may repeat.\nHard: 5 pegs, colors may repeat.\nThere are always 6 colors. Stats keep wins, losses, guesses and best times for each level."),
])

write("pegs", [
 ("The Idea", "Jump pegs over each other until only one is left.\nA peg jumps over a neighbouring peg into the empty hole straight beyond it, and the peg it jumped over is taken off. On the Triangle, jumps go in six directions; on the other boards only across and up or down."),
 ("Playing", "Tap a peg: the holes it can jump to show as dots. Then tap one of them.\nUndo takes back jumps, as many as you like, even after you get stuck. Restart This Game (in the menu) puts every peg back."),
 ("Boards", "Triangle: 15 holes, a quick warm-up.\nEnglish: the classic 33-hole cross, centre empty.\nEuropean: 37 holes, the hardest; it starts with the hole two above the centre, because the centre start can't be solved.\nStats keep wins, losses and best times."),
])

write("memory", [
 ("The Idea", "Every picture is on exactly two tiles, face down. Find all the pairs in as few turns as you can.\nTap a tile to turn it over, then tap a second one. A matching pair stays face up with a green edge."),
 ("Misses", "Two different pictures stay up with a red edge so you can remember them. Your next tap turns them back over and turns the tile you tapped, so there is no waiting.\nStats keep your best time and fewest turns for 4x4, 4x5 and 5x6."),
])

write("nonogram", [
 ("The Idea", "Paint a hidden picture. The numbers beside each row and above each column are its clue: the lengths of the runs of filled cells in that line, in order. \"3 1\" means a run of 3, then at least one gap, then a run of 1.\nFill the cells so every clue matches."),
 ("Fill And Mark", "Fill (key under the grid): a tap fills a cell; tap again to clear it.\nMark: a tap puts an X where you know nothing goes, to help you think.\nA clue turns grey when its line matches. The row and column you last tapped are tinted."),
 ("Solving", "Every puzzle has one answer, and you can always find the next cell by looking at one row or column at a time; no guessing.\nStart with big runs: a run of 4 in 5 cells must cover the middle three.\nStats keep your best time for each size."),
])

write("solitaire", [
 ("The Idea", "Move every card onto the four foundations, one pile per suit, from Ace up to King.\nOn the seven columns, cards go down in alternating colors: a red 6 on a black 7. Only a King goes into an empty column. A face-down card left on top turns up by itself."),
 ("Playing", "Tap a face-up card to pick it up (with the cards on it), then tap where it goes. Tap the picked card again to send it to a foundation, or else to a column that takes it.\nTap the stock (the face-down pile, in the top corner on your stylus hand's side) to turn 1 or 3 cards; when it's empty, tap it to turn the waste back over."),
 ("Keys And Finish", "Undo takes back moves, Hint shows a good one (a green bar marks where it goes).\nOnce every card is face up and the stock is empty, the rest go up by themselves and the cards bounce off the table. Tap to stop the show."),
 ("Options", "Draw 1 or Draw 3 (default).\nStandard scoring: +5 to a column from the waste, +10 to a foundation, +5 for turning a card up, -15 back off a foundation, a time bonus when you win.\nVegas: a deal costs $52, each foundation card pays $5, the total carries on. Or no score."),
])

write("golf", [
 ("The Idea", "Clear all seven columns onto the waste pile (the face-up card below them).\nTap a column to play its top card onto the waste: it must be one rank higher or lower than the waste card, any suit. A 6 goes on a 5 or a 7.\nAce and King don't wrap round, and nothing goes on a King."),
 ("Playing", "Stuck? Tap the stock (the face-down pile) to turn its next card onto the waste.\nUndo takes back moves, even after you run out. Hint shows a column that can play, or the stock.\nThe fewer cards left at the end, the better. Clear them all to win."),
])

write("pyramid", [
 ("The Idea", "Take the pyramid apart. Remove two cards that add up to 13: Ace = 1, Jack = 11, Queen = 12. A King is 13 by itself and goes as soon as you tap it.\nOnly uncovered cards can go: a card is uncovered once both cards resting on it from below are gone."),
 ("Playing", "Tap a card (amber edge), then its partner. The top card of the waste (next to the stock) can pair too.\nTap the stock to turn its next card onto the waste. You can go through the stock three times.\nUndo takes back moves; Hint shows a pair, a King or the stock."),
])

write("spider", [
 ("The Idea", "Two decks in ten columns. Build runs from King down to Ace in one suit: a full run comes off by itself. Take off all eight runs to win.\nA card goes on any card one rank higher, any suit. But only cards in one suit going down by one move together, so same-suit builds are worth more."),
 ("Playing", "Tap a card in a column's top run: it and the cards on it are picked. Then tap the column they go to. Tap the picked card again to send it to the best column.\nAn empty column takes any card or run.\nTap the stock (a top corner) to deal one card onto every column; every column must have a card first."),
 ("Levels And Score", "1 Suit (all spades) is the gentle start; 2 Suits and 4 Suits get much harder.\nScore: 500 to start, 1 off for every move or deal, 100 for every run taken off.\nUndo takes back moves; Hint shows a good move or the stock."),
])

write("freecell", [
 ("The Idea", "Every card is face up from the start, and nearly every deal can be won.\nMove all the cards onto the foundations (Spades, Hearts, Clubs, Diamonds), each from Ace to King.\nOn the columns, cards go down in alternating colors. An empty column takes any card."),
 ("Free Cells", "The four free cells (top row, on your stylus hand's side) each hold one card while you dig out the one you need.\nA run of cards moves together if there's room: one card per empty free cell plus one, doubled for each empty column.\nCards nothing else needs go up to the foundations by themselves."),
 ("Playing", "Tap a card (it turns gold), then tap where it goes: a column, the free-cell row, or the foundation row.\nTap the picked card again to send it to its foundation, or else to a column or a free cell.\nUndo takes back moves; Hint shows one."),
])

write("blackjack", [
 ("The Idea", "You against the dealer. Get closer to 21 than the dealer without going over.\nNumber cards count their number, J Q K count 10, an Ace 1 or 11.\nAn Ace and a 10-card as your first two cards is a blackjack: it pays 3 to 2."),
 ("A Round", "Bet with +5, +10, +25 (Clear starts over), then tap Deal.\nHit takes a card, Stand keeps your total. Double doubles your bet for exactly one more card. Split makes two hands from a pair.\nThe dealer turns the hidden card over and must draw to 17."),
 ("Chips", "You start with 500. A win pays your bet, a tie (push) gives it back.\nRun out and New Chips gives you another 500. New Game (in the menu) starts over at 500.\nStats keep hands won, lost and pushed, blackjacks and your most chips."),
])

write("rpgdice", [
 ("The Idea", "A dice roller for tabletop role-playing games. Every die: Coin, d4, d6, d8, d10, d12, d20 and d100 (two d10s: tens and ones).\nThe last roll is drawn as dice, each showing its number, with the total. A natural 20 glows gold, a natural 1 red."),
 ("Rolling", "Tap dice keys to build a roll: d6 three times and d8 once makes 3d6 + 1d8. -1 and +1 set a modifier.\nRoll rolls it; Roll again repeats it. After a roll, the next die you tap starts a new one. Clear empties it."),
 ("Presets", "A preset rolls up to four lines at once, each a label (Hit, Damage...), dice and a modifier: a fighter's two attacks are Hit d20+7, Damage d8+5, Hit d20+4, Damage d6+5.\nPresets: tap one to roll it. Edit Presets: change a name, lines, dice and modifiers. + New makes one."),
 ("History", "History lists every roll, newest first: the dice, each one's number and the total. < > turn the pages.\nClear History empties it. Rolls, presets and history are saved on the board."),
])

write("farkle", [
 ("The Idea", "A push-your-luck dice game for two: you against the computer, or pass-and-play. First to 10,000 wins; the other player then gets one last turn to beat it.\nRoll six dice, set aside the ones that score, then roll the rest again or bank the points."),
 ("A Turn", "Tap scoring dice to set them aside (they turn gold); the Bank key shows what you'd bank.\nRoll throws the dice left. Each roll must score something, or it's a Farkle: the turn's points are lost.\nSet all six aside and you roll all six again (hot dice)."),
 ("Scoring", "• A 1 = 100, a 5 = 50\n• Three of a kind = 100 x the number (three 1s = 1000)\n• Four of a kind 1000, five 2000, six 3000\n• 1-2-3-4-5-6 = 1500, three pairs = 1500\n• Four of a kind and a pair = 1500, two triples = 2500\nDice count only in the roll they came up in."),
 TP,
], two_player=True)

write("mancala", [
 ("The Idea", "Kalah, the classic Mancala: six pits a side with four seeds each, and a store for each player.\nSeeds go round: down the left column into the bottom store, up the right one into the top store. Most seeds in your store at the end wins."),
 ("A Move", "Tap one of your pits: its seeds are sown one at a time into the next pits - down your side, into your store, up the other side - skipping the other player's store.\nLast seed in your store: you go again.\nLast seed in an empty pit of yours: it and the seeds opposite go to your store."),
 ("The End", "When either side's pits are all empty, the game ends: the other player puts the seeds left on their side into their store.\nThe pit last sown from is lit; your pits light up when it's your turn.\nYour pits are on your stylus hand's side (Settings: Right Hand or Left Hand)."),
 TP,
 WL,
], two_player=True)

write("morris", [
 ("The Idea", "Each side has nine men; White starts. Three of your men in a line - a mill - lets you take one of the other side's men.\nTake them down to two men, or leave them no move, and you win."),
 ("Placing, Then Moving", "First, take turns placing a man on any empty point (tap it).\nWith all men placed, a turn moves one man along a line to the next empty point: tap your man (dots show where it can go), then the point.\nA side down to three men may fly: move to any empty point."),
 ("Mills", "Make a mill and the men you may take get a red ring: tap one. Men in a mill are safe while the other side has men outside mills.\nTap your new man again to take that move back.\nThe last move is tinted; a red ring on an empty point shows where a man was taken. 50 moves each with nothing taken is a draw."),
 TP,
 WL,
], two_player=True)

write("sank", [
 ("The Idea", "Each side hides a fleet in a 10 x 10 sea: Carrier 5 squares, Battleship 4, Cruiser 3, Submarine 3, Destroyer 2. Ships lie across or down; they may touch but not overlap.\nTake turns firing one shot into the other sea. Sink every ship to win."),
 ("Your Fleet", "Biggest ship first: tap where one end goes (it turns gold), then a lit square the way it points (near an edge it slides in). Undo takes one back; Ready when all five are in.\nOptions (menu): Ship Placement Random - Shuffle until you like it.\nPass-and-play: the board says who to pass it to, then waits for Ready."),
 ("Firing", "Your turn shows Their Waters: tap a square. The shell falls with a whistle - a splash and MISS!, or an explosion and HIT!. Then the view turns to My Fleet while they aim and fire at you.\nWhite pegs are misses, red bursts hits; a sunk ship shows. The keys switch the view any time."),
 ("The Computer", "Easy fires at random, and after a hit tries the squares around it.\nMedium fires on a spaced pattern and follows a line of hits.\nHard works out every way the ships left could lie and fires where most of them cross."),
 TP,
 WL,
], two_player=True)

write("wheel", [
 ("The Idea", "Solve the hidden phrase before the others. You play Max and Zoe (or a friend: Pass and Play). Three rounds, a new puzzle each; the category shows under the board.\nMost money banked after three rounds wins."),
 ("A Turn", "• Spin: the wheel stops on a money wedge - pick a consonant; each one in the puzzle pays that much and you go again. Not there: the next player's turn.\n• Vowel: buy a vowel for $250.\n• Solve: fill in the blanks."),
 ("The Wheel", "BUST: you lose this round's money and your turn.\nSKIP: you lose your turn.\nSolve the puzzle to bank your round money (at least $500). The others lose theirs. Letters already called are grey."),
 ("Solving", "Tap Solve, then tap letters: they fill the blanks in reading order (the gold tile is next). Delete takes one back; Cancel goes back to your turn.\nRight wins the round; wrong passes the turn.\nComputer: Easy solves late, Hard knows lots of puzzles and solves early."),
 TP,
], two_player=True)

write("ultimate", [
 ("The Idea", "Nine small tic-tac-toe boards make one big board. Win a small board with three in a row; win the game with three small boards in a row on the big board.\nX starts, anywhere."),
 ("Where You Play", "The square you pick sends the other player to the small board in the same place: play the top right square of a board, and they must play in the top right board.\nIf that board is already won or full, they may play in any open board.\nThe board(s) you may play in are lit."),
 ("The Board", "A won board shows a big X or O; a full board with no winner turns grey and counts for nobody. All boards done with no three in a row is a draw.\nThe last mark has a gold square.\nComputer: Easy looks 2 moves ahead, Medium 4, Hard thinks about a second."),
 TP,
 WL,
], two_player=True)

write("gomoku", [
 ("The Idea", "A 15 x 15 board. Black places a stone first, then the players take turns placing one stone on any empty point (where the lines cross).\nFive or more of your stones in a row - across, down or diagonal - wins."),
 ("Playing", "Tap a point to place your stone there. The last stone has a gold ring; the winning five gets a line through it.\nWatch out: four in a row with both ends open can't be stopped, and three with both ends open soon becomes four. Block those early!"),
 ("The Computer", "Easy plays its own lines and blocks only a five.\nMedium and Hard look 4 and 6 moves ahead among the most promising points.\nA full board with no five is a draw."),
 TP,
 WL,
], two_player=True)

write("vpoker", [
 ("The Idea", "The classic Jacks or Better machine. Bet 1 to 5 credits and get five cards. Tap the cards you want to keep: they say HELD. Then Draw replaces the rest, once.\nYour final hand pays by the table at the top, times your bet. The hand you hold lights up."),
 ("Paying Hands", "• Jacks or Better: a pair of Jacks, Queens, Kings or Aces\n• Two Pair, Three of a Kind\n• Straight: five in a row (A-2-3-4-5 and 10-J-Q-K-A count)\n• Flush: five of one suit\n• Full House, Four of a Kind, Straight Flush\n• Royal Flush: 10 to Ace of one suit, 4000 at a 5 credit bet"),
 ("Keys", "Bet One adds a credit to the bet (1 to 5, then back to 1). Bet Max bets 5 and deals at once.\nHint holds the cards the standard Jacks or Better strategy keeps: about the best play there is.\nYou start with 500 credits; run out and New Credits gives you 500 more."),
])

write("holdem", [
 ("The Idea", "No-limit Texas Hold'em: you against three computer players, Ada, Max and Zoe. Everyone starts with 1000 chips; blinds are 5 and 10.\nYou get two cards. Five shared cards come face up in the middle: three (the flop), one (the turn), one (the river). Your best five of those seven cards wins the pot."),
 ("Betting", "A betting round comes before the flop and after each new card. On your turn:\n• Fold: give up the hand\n• Check or Call: match the bet\n• Bet / Raise: the smallest raise; Pot: the size of the pot\n• All In: all your chips\nAll-ins make side pots: you win only what you matched."),
 ("Hands", "From best: Straight Flush, Four of a Kind, Full House, Flush, Straight, Three of a Kind, Two Pair, Pair, High Card.\nYour best hand so far shows next to your cards. At a showdown everyone still in shows their cards."),
 ("The Table", "The player to act has a gold edge; D is the dealer button, which moves each hand.\nA computer player who runs out buys back in. Run out yourself and New Chips gives you 1000 more.\nLevels: the computer weighs its chances more carefully on Medium and Hard, and plays its position."),
])
