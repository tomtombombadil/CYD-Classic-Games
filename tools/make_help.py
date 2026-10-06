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

write("piperace", [
 ("The Idea", "Water is about to pour out of the tank. Lay pipe for it to run through - and stay ahead of it!\nEach level has a goal: the water must pass through that many pipes before it runs out of pipe. Reach the goal and the next level is faster and longer."),
 ("Laying Pipe", "The next five pieces show above the board; the one edged in gold comes first. Tap an empty square to lay it there.\nTap a pipe the water hasn't reached to swap it for the next piece (it costs 50 points).\nThe water can't go through rocks, off the board, or into a pipe that doesn't fit."),
 ("The Water", "The header counts down until the water starts; Water Now starts it at once.\nWhen the water runs out of pipe, the level ends. Fast Flow makes it rush on when you're done laying - every pipe it fills while fast scores double."),
 ("Score", "100 points a pipe filled, 200 while flowing fast. Water crossing a cross piece both ways: 400 more. A swapped pipe, or one laid but never reached when the level ends: minus 50.\nStats keep every finished game: score, level and pipes."),
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

write("strategygo", [
 ("The Idea", "Two armies of 40 hidden pieces. Capture the other side's Flag - or leave them with no piece that can move - to win.\nYou see your own ranks; theirs show only after a battle. A dot on one of theirs means it has moved, so it's no Bomb or Flag. Red moves first."),
 ("Setting Up", "Your army starts laid out for you in your four rows. Tap two of your pieces to swap them; Shuffle deals a new layout. Tap Ready when you like it.\nKeep the Flag safe, often on the back row behind Bombs."),
 ("Moving", "Tap one of your pieces, then a lit square: one square across or up and down, never into the lakes.\nScouts (2) go any distance in a straight line. Bombs and the Flag never move.\nA piece may not go back and forth between the same two squares more than five times in a row."),
 ("Battles", "Move onto an enemy piece to fight: both are shown, the higher rank wins, equal ranks both go.\n• The Spy (S) beats the Marshal (10) - only when the Spy strikes.\n• A Miner (3) clears a Bomb; anything else that hits a Bomb is lost.\nThe Pieces key lists what each side has lost."),
 ("The Pieces", "Marshal 10 (1), General 9 (1), Colonel 8 (2), Major 7 (3), Captain 6 (4), Lieutenant 5 (4), Sergeant 4 (4), Miner 3 (5), Scout 2 (8), Spy S (1), Bomb B (6), Flag F (1).\nThe computer sees only what you would: pieces that have fought, and which ones have moved."),
])

write("acquisitions", [
 ("The Idea", "Build hotel chains and buy their shares. The richest player when the game ends wins.\nYou play Ada, Max and Zoe. Each turn: lay one of your six tiles on the board, buy up to three shares, then draw a new tile. Everyone starts with $6,000."),
 ("Laying Tiles", "Your tiles are the keys at the bottom; their squares are edged in gold on the board.\nA tile next to a loose tile founds a hotel: pick one of the seven, and get a free share.\nA tile next to a hotel makes it bigger. A grey key's tile can't go down now."),
 ("Mergers", "A tile that joins two hotels merges them: the bigger one takes over the smaller.\nThe two biggest shareholders of the smaller hotel get bonuses (10 and 5 times its share price). Then each holder sells those shares, trades two for one share of the bigger hotel, or keeps them.\nA hotel of 11 or more tiles is safe: it can't be taken over."),
 ("Shares And Money", "After your tile, tap the hotel chips to buy shares (up to three); Undo gives one back. Done ends your turn.\nShare prices grow with a hotel's size. Sunrise and Oakwood are cheap, Royal and Crimson dear.\nTap a chip at other times to see the Stocks page: who holds what."),
 ("The End", "Once a hotel has 41 tiles, or every hotel is safe, the player to move may end the game (End Game).\nThen every hotel pays its bonuses, all shares are sold, and the most money wins.\nEasy, Medium and Hard set how well Ada, Max and Zoe play. Stats keep your place and money."),
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

write("escape", [
 ("The Idea", "The island is sinking! Each colour has 9 explorers worth 1 to 5 (only you see your values). Get them to the safe islands in the corners - by land, swimming or by boat.\nWhen the Volcano erupts the game ends: the most points saved wins."),
 ("A Turn", "1. Three moves: an explorer one hex (land, sea, boat or a safe island), or a boat one hex. A swimmer moves once a turn.\n2. Sink a tile: beaches first, then forests, then mountains.\n3. The creature die: move one Shark (2 hexes), Whale (3) or Sea Serpent (1) - or Skip."),
 ("Under The Tiles", "A sunk tile can hide a Shark (it eats the swimmers there), a Whale, a Boat (swimmers climb in), a Whirlpool (swimmers and boats in and around it are lost) - or the Volcano.\nSharks eat swimmers, whales tip boats over, sea serpents eat swimmers and boats."),
 ("Boats", "A boat has 3 seats. You may move an empty boat, or one where most of the explorers are yours.\nNo one moves into a creature's hex. At most 3 explorers stand on a tile or swim in a hex."),
 ("On The Screen", "Tap a hex with your piece: your boat first, then your explorers, best first - tap again for the next. Then tap a framed hex.\nRed frames: tiles you may sink. A white ring = swimming.\nComputer: Easy only runs; Medium also uses the creatures; Hard looks 2 moves ahead."),
])

write("sorrycyd", [
 ("The Idea", "Race your four pawns out of Start, once round the board and into Home. All four Home wins.\nYou are Red, at the bottom; Max, Zoe and Ada are Blue, Yellow and Green. Tap Draw Card (or the deck in the middle), then move as the card says."),
 ("The Cards", "1 or 2: start a pawn, or move. A 2 draws again.\n3, 5, 8, 12: move. 4: move 4 back.\n7: move 7, or split it over two pawns.\n10: move 10, or 1 back.\n11: move 11, or switch places with another pawn on the track.\nSorry!: a pawn from Start takes another pawn's square."),
 ("Bumps And Slides", "Land on another colour's pawn and it goes back to its Start. You can't land on your own.\nLand on the first square of another colour's slide and you slide to its end, sending back every pawn on it - yours too.\nOnly your colour goes into your Safety Zone; Home needs the exact count."),
 ("Your Move", "The pawns that can move have a gold ring: tap one, then a dot. A 7 split: tap the dot short of 7, then the other pawn goes the rest.\nSorry! or a switch: the pawns you can take the place of get rings.\nNo move fits: the turn is lost. A 4 back from near Start gets you close to Home!"),
 ("The Computer", "Easy moves its own pawns as far as it can.\nMedium also sends others back and keeps its pawns out of reach.\nHard also thinks about its next card.\nThe cards decide a lot - even Hard loses often. Stats keep your place and pawns Home."),
])

write("whowants", [
 ("The Idea", "Fifteen questions climb a money ladder from $100 to $1,000,000. Questions 1-5 are easy, 6-10 medium, 11-15 hard. Tap an answer: it turns gold, then green if it's right - or red.\n$1,000 and $32,000 are safe: a wrong answer drops you back to the last safe amount you passed."),
 ("Lifelines", "Each once a game:\n50:50 takes away two wrong answers.\nAudience: the studio audience votes - their percents show on the answers. They know easy questions best.\nPhone: a friend says which answer they think it is, and how sure they are. Friends can be wrong!"),
 ("Walking Away", "Not sure? Walk Away (top right) and keep the money you've won so far.\nThe menu's Money Ladder shows every prize and where you are.\nQuestions don't repeat until you've played them all."),
 ("The Questions", "The questions come from Open Trivia DB (opentdb.com), shared under CC BY-SA 4.0, screened for young players and changed to plain text. Stats keep what you won and how far you got."),
])

write("jeoparcyd", [
 ("The Idea", "You against Max and Zoe. The board has six categories (one per row) of five clues each, worth $200 to $1,000 - the bigger the value, the harder the question.\nTwo rounds (the second pays double), then a Final clue. Most money wins."),
 ("Buzzing In", "Whoever has control picks a value. Then everyone may answer: tapping an answer IS buzzing in, so be quick - Max and Zoe buzz in too.\nRight: you win the value and pick next. Wrong: you lose the value, and the others may still try. The bar shows the time left."),
 ("Daily Doubles", "One value in round 1 and two in round 2 hide a Daily Double: only the picker answers, for a wager - the least, half, all, or the clue's value. You may bet up to your money, or the round's top value if you have less."),
 ("Final", "Everyone with money bets (nothing, a quarter, half or all), then answers one hard question. Right adds the bet, wrong takes it off.\nComputer: Easy knows less and buzzes slower; Hard knows more and is quick. Questions: Open Trivia DB (CC BY-SA 4.0)."),
])

write("hollywood", [
 ("The Idea", "Tic-tac-toe with nine stars. On your turn tap a star: they're asked a question and give an answer - but stars sometimes bluff!\nAgree or Disagree. Judge right and the square is yours. Judge wrong and it goes to the other player."),
 ("Winning", "Three in a row wins - or any five squares.\nBut a winning square must be earned: if judging wrong would hand the other player the win, the square stays open instead.\nX goes first."),
 ("The Stars", "Stars give the right answer a bit more than half the time. When the question is hard, think twice before agreeing!\nComputer: Easy picks any star and knows less; Medium and Hard play tic-tac-toe well and know more. Questions: Open Trivia DB (CC BY-SA 4.0)."),
 TP,
], two_player=True)

write("trivialcyd", [
 ("The Idea", "Win a wedge in all six colours: Geography (blue), Entertainment (pink), History (yellow), Arts & Literature (brown), Science & Nature (green) and Sports & Leisure (orange).\nYou against Max and Zoe, or 2-4 people (menu: Pass and Play)."),
 ("A Turn", "Roll, then tap one of the two framed squares (either way round). Answer a question in that square's colour.\nRight: roll again. Wrong: the next player's turn.\nA die square = roll again, no question."),
 ("Wedges", "Every sixth square is a colour's headquarters (a wedge in a white circle). Answer right there to win that colour's wedge for your pie in the middle.\nWith all six, your next turn is one question in a random colour: get it right to win!"),
 ("The Computer", "Easy knows fewer answers and doesn't aim for wedges; Medium and Hard head for the wedges they need and know more.\nQuestions: Open Trivia DB (CC BY-SA 4.0). Stats keep your place and wedges."),
])

write("dealcyd", [
 ("The Idea", "26 cases hide 26 amounts, from 1 cent to $1,000,000. Pick one to be yours - it stays shut until the end.\nThen open the other cases a few at a time. Every amount you open is one your case can't hold: it turns grey on the boards at the sides."),
 ("The Banker", "After each round the Banker calls with an offer for your case. Deal takes the money and ends the game. No Deal plays on.\nRounds open 6, 5, 4, 3 and 2 cases, then one at a time. The offers grow as the game goes on - but so does the risk."),
 ("The End", "With one other case left, you may keep yours or swap it for that one, and you win what's inside.\nIf you took a deal, you see what your own case held. Stats keep what you won, the round you dealt in and what your case held."),
])

write("presscyd", [
 ("The Idea", "18 squares ring the board. Each keeps changing between money, money plus one more spin, and a Gremlin.\nTap Spin and a light jumps round the squares. Tap STOP! and you win whatever the lit square shows. The most money after two rounds wins."),
 ("Gremlins", "A Gremlin takes all the money you have. A fourth Gremlin puts you out of the game.\nThe board in round 2 pays more - and has more Gremlins."),
 ("Spins And Passing", "Everyone has 3 spins in round 1 and 4 in round 2; money with +1 Spin gives you one more. The player with the least money goes first.\nPass gives the spins you have left to the leader, who then has to take them - a way to keep your money safe. Spins passed to you can't be passed on."),
 ("Max And Zoe", "The computer players spin, stop after a moment and pass when it suits them.\nStats keep your place and money for every game."),
])

write("cardsharks", [
 ("The Idea", "Each player has a row of five cards; only the first is face up. On your turn, call the next card Higher or Lower than the one before it.\nTurn over all five of your cards first to win the round. Two rounds win the game. Aces are high."),
 ("Calls And Misses", "Right: the card stays and you call again - or Freeze to keep your place and pass the turn.\nWrong (the same rank is wrong too): every card since your last freeze goes and the turn passes. The card that beat you shows crossed out; a gold bar marks where you froze."),
 ("Change", "Once a turn, before your first call, Change swaps the card in play for a fresh one - handy on a 7, 8 or 9.\nThe line in the middle says what the card in play is. The player who didn't start the last round starts the next one."),
 ("The Computer", "Easy calls by the card alone and freezes after two right.\nMedium also changes middle cards and freezes before a risky call.\nHard counts the cards still unseen and takes chances when you are close to winning."),
 TP,
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
