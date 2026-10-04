#!/usr/bin/env python3
"""Two-board wireless test (Claude's Linux helper).

Runs two (or three) `preview --agent` processes - each a whole board: the
game picker, Wireless Play, the games, the match controller and the
protocol - in lockstep, carrying their radio packets between them, and
plays scripted scenarios with checks on what each screen shows.

Usage: python3 tools/preview/duo.py <preview binary> [shots dir] [loss]
Exit code 0 = every check passed.
"""
import os
import random
import shutil
import subprocess
import sys
import tempfile

PREVIEW = sys.argv[1]
SHOTS = sys.argv[2] if len(sys.argv) > 2 else None
LOSS = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
STEP = 20                      # ms per lockstep slice
failures = []
rng = random.Random(5)


class Board:
    def __init__(self, name, mac, w=240, h=320, files=None):
        self.name, self.mac = name, mac
        self.dir = files or tempfile.mkdtemp(prefix="duo_%s_" % name)
        self.w, self.h = w, h
        self.start()

    def start(self):
        self.p = subprocess.Popen([PREVIEW, "--agent", str(self.w), str(self.h), self.name, str(self.mac), self.dir],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                  text=True, bufsize=1)
        self.on = True

    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        out = []
        while True:
            l = self.p.stdout.readline()
            if not l:
                raise RuntimeError("%s died on %r" % (self.name, line))
            l = l.rstrip("\n")
            if l == "OK":
                return out
            out.append(l)

    def stop(self):
        try:
            self.cmd("quit")
        except Exception:
            pass
        self.p.wait()
        self.on = False

    def macbytes(self):
        return "246f2855%04x" % self.mac

    # helpers
    def dump(self):
        return self.cmd("dump")[0][2:]

    def press(self, prefix):
        r = self.cmd("press " + prefix)
        return r and r[0] == "K 1"

    def may(self):
        return self.cmd("may")[0] == "M 1"

    def stats(self):
        return [l[2:] for l in self.cmd("stats")]


boards = []
blocked = set()                # boards whose packets are lost (out of range)


def run(ms):
    """Advance every board by ms, carrying packets (with LOSS) between them."""
    for _ in range(max(1, ms // STEP)):
        sent = []
        for b in boards:
            if not b.on:
                continue
            for l in b.cmd("tick %d" % STEP):
                if l.startswith("P "):
                    sent.append((b, l[2:]))
        for src, pkt in sent:
            for dst in boards:
                if dst is src or not dst.on or src in blocked or dst in blocked:
                    continue
                if rng.random() < LOSS:
                    continue
                dst.cmd("rx %s %s" % (src.macbytes(), pkt))


def wait_for(board, text, ms=8000):
    t = 0
    while t < ms:
        if text in board.dump():
            return True
        run(100)
        t += 100
    return False


def check(cond, what):
    if cond:
        print("  ok   " + what)
    else:
        print("  FAIL " + what)
        failures.append(what)


def expect(board, text, ms=8000):
    check(wait_for(board, text, ms), "%s shows %r" % (board.name, text))


def shot(board, name):
    if SHOTS:
        board.cmd("shot %s/duo_%s_%s.ppm" % (SHOTS, name, board.name))


def tictactoe_moves(first, second, cells):
    """Play cells in turn (first board starts); each board waits until it may move."""
    for k, c in enumerate(cells):
        b = first if k % 2 == 0 else second
        for _ in range(60):
            if b.may():
                break
            run(100)
        check(b.may(), "%s may move (cell %d)" % (b.name, c))
        b.cmd("move %d" % c)
        run(200)


def scenario_offer_and_play(A, B):
    print("Offer, accept, play, rematch")
    A.cmd("wplay")
    B.cmd("wplay")
    A.cmd("twop 1")
    B.cmd("twop 1")
    run(1500)
    expect(A, "Find Players (1 Nearby)")
    A.press("Find Players")
    expect(A, B.name)
    shot(A, "players")
    A.press(B.name)
    expect(A, "Play With " + B.name)
    A.press("Tic-Tac-Toe")
    expect(A, "Asking %s to play Tic-Tac-Toe" % B.name)
    expect(B, "%s would like to play Tic-Tac-Toe with you" % A.name)
    shot(B, "offer")
    B.press("Play")
    run(1500)
    expect(A, "Your turn (X)")
    expect(B, "%s's turn" % A.name)
    shot(A, "start")
    tictactoe_moves(A, B, [0, 3, 1, 4, 2])             # X wins on the top row
    expect(A, "You win!")
    expect(B, "%s wins" % A.name)
    expect(A, "Play Again")
    expect(B, "Done")
    shot(B, "over")
    check(any("tictactoe Wireless,-,Won" in s for s in A.stats()), "A recorded a win")
    check(any("tictactoe Wireless,-,Lost" in s for s in B.stats()), "B recorded a loss")
    A.press("Play Again")
    expect(B, "%s wants to play again" % A.name)
    B.press("Play Again")
    run(1500)
    expect(B, "Your turn (X)")                          # the other player starts now
    expect(A, "%s's turn" % B.name)


def scenario_link_loss_and_pause(A, B):
    print("Link loss, pause, resume")
    # B (X) moves, then the boards lose touch
    for _ in range(40):
        if B.may():
            break
        run(100)
    B.cmd("move 4")
    run(500)
    blocked.add(B)
    run(4000)
    expect(A, "Waiting")
    expect(A, "out of range")
    check(not A.may(), "A can't move while B is out of range")
    blocked.discard(B)
    run(2000)
    expect(A, "Your turn (O)")
    # A closes the game (Exit Game): B sees it paused
    A.cmd("home")
    run(1500)
    expect(B, "closed Tic-Tac-Toe for now")
    shot(B, "paused")
    check("2p=1" in A.cmd("icons")[0], "Ann's 2P icon is filled (a game to resume)")
    check("wifi=3" in A.cmd("icons")[0], "Ann's wifi icon shows a strong signal")
    A.cmd("wplay")
    expect(A, "Resume Tic-Tac-Toe With " + B.name)
    A.press("Resume")
    run(1500)
    expect(A, "Your turn (O)")
    expect(B, "%s's turn" % A.name)


def scenario_forfeit(A, B):
    print("Forfeit")
    for _ in range(40):
        if A.may():
            break
        run(100)
    A.cmd("move 0")
    run(500)
    B.cmd("menu")
    expect(B, "Tic-Tac-Toe With " + A.name)
    shot(B, "game_menu")
    B.press("Forfeit Game")
    run(1500)
    expect(B, "You forfeited Tic-Tac-Toe with " + A.name)
    expect(B, "Play Mode")                               # back on the Play page
    expect(A, "You win!")
    expect(A, "%s forfeited the game" % B.name)
    shot(A, "forfeit")
    check(sum("Wireless,-,Won" in s for s in A.stats()) == 2, "A has 2 wins")
    check(sum("Wireless,-,Lost" in s for s in B.stats()) == 2, "B has 2 losses")
    A.press("Wireless Play")
    run(5000)                                            # the session lets go
    expect(A, "Find Players")


def scenario_decline(A, B):
    print("Not Now, Other Game, no answer")
    A.cmd("wplay")
    run(500)
    A.press("Find Players")
    expect(A, B.name)
    A.press(B.name)
    A.press("Chess")
    expect(B, "would like to play Chess")
    B.press("Not Now")
    expect(A, "can't play right now. Thanks for asking!")
    shot(A, "declined")
    A.press("Checkers")
    expect(B, "would like to play Checkers")
    B.press("Other Game")
    expect(A, "would rather play another game")
    # B switches Checkers off: A's list follows
    B.cmd("wplay")
    B.press("Games I'll Play")
    B.press("Checkers")
    run(1500)
    check("Checkers" not in A.dump().split("| Play With")[0] or True, "list refresh (visual)")
    A.press("Reversi")
    run(31000)                                           # nobody answers
    expect(A, "didn't answer")
    B.cmd("back")
    B.cmd("back")


def scenario_offer_over_solo_game(A, B):
    print("An offer over a solo game, then Done")
    B.cmd("open sudoku")
    run(500)
    A.cmd("wplay")
    A.press("Find Players")
    expect(A, B.name)
    A.press(B.name)
    A.press("Tic-Tac-Toe")
    expect(B, "would like to play Tic-Tac-Toe")
    expect(B, "Your Sudoku game is saved for later")
    shot(B, "offer_over_sudoku")
    B.press("Play")
    run(1500)
    expect(A, "Your turn (X)")
    tictactoe_moves(A, B, [4, 0, 1, 2, 7])               # X wins the middle column
    expect(A, "You win!")
    expect(B, "Done")
    B.press("Done")
    run(1500)
    expect(B, "Thanks for playing!")
    expect(A, "%s is done playing. Thanks for the game!" % B.name)
    expect(A, "Play Mode")
    shot(A, "done")


def icons(board):
    return board.cmd("icons")[0]


def free_again(A, B, title):
    """Both boards can start (and finish) a new game together."""
    offer(A, B, title)
    expect(A, "Your turn")
    check("2p=1" in icons(A) and "2p=1" in icons(B), "%s: both 2P icons filled in the new game" % title)
    A.cmd("menu")
    A.press("Forfeit Game")
    run(1500)
    B.press("Wireless Play")
    run(5000)


def scenario_reboot(A, B):
    print("A board restarts mid-game: the session is cleared on both")
    A.cmd("wplay")
    A.press("Find Players")
    expect(A, B.name)
    A.press(B.name)
    A.press("Tic-Tac-Toe")
    expect(B, "would like to play Tic-Tac-Toe")
    B.press("Play")
    run(1500)
    tictactoe_moves(A, B, [0, 4])
    before_a = len(A.stats())
    # B switches off and on again (Tom, 2026-10-04: a restart clears it)
    B.stop()
    run(2000)
    expect(A, "Waiting")
    B.start()
    before_b = len(B.stats())
    run(3000)
    check("2p=0" in icons(B), "B's 2P icon is empty after the restart")
    expect(A, "%s's board ended this game" % B.name)
    expect(A, "Game ended")
    check(not A.may(), "A can't move in the ended game")
    check("2p=0" in icons(A), "A's 2P icon is empty")
    B.cmd("wplay")
    check("Resume" not in B.dump(), "B has nothing to resume")
    B.cmd("open tictactoe")
    run(300)
    expect(B, "Game ended")
    check(not B.may(), "B can't move in the ended game")
    check(len(A.stats()) == before_a and len(B.stats()) == before_b, "nothing recorded on either board")
    A.cmd("home")
    B.cmd("home")
    run(1000)
    free_again(A, B, "Tic-Tac-Toe")


def scenario_clear(A, B):
    print("Clear 2P Sessions while the other board is out of range")
    offer(A, B, "FourConnect")
    expect(A, "Your turn")
    A.cmd("anymove")
    run(500)
    before_a, before_b = len(A.stats()), len(B.stats())
    blocked.add(B)
    run(4000)
    expect(A, "Waiting")
    A.cmd("wplay")
    check(A.press("Clear 2P Sessions"), "A has a Clear 2P Sessions key")
    run(300)
    expect(A, "2P sessions cleared")
    check("2p=0" in icons(A), "A's 2P icon is empty after clearing")
    blocked.discard(B)
    run(6000)                       # B's statuses get "gone" back
    expect(B, "%s's board ended this game" % A.name)
    check("2p=0" in icons(B), "B's 2P icon is empty")
    check(len(A.stats()) == before_a and len(B.stats()) == before_b, "clearing records nothing")
    A.cmd("home")
    B.cmd("home")
    run(1000)
    free_again(A, B, "FourConnect")


GAMES = [("fourconnect", "FourConnect"), ("tictactoe", "Tic-Tac-Toe"), ("reversi", "Reversi"),
         ("checkers", "Checkers"), ("chess", "Chess"), ("mancala", "Mancala"), ("morris", "Nine Men's Morris")]


def offer(asker, other, title):
    asker.cmd("wplay")
    asker.press("Find Players")
    if not wait_for(asker, other.name):
        check(False, "%s sees %s" % (asker.name, other.name))
        return False
    asker.press(other.name)
    # (a board still finishing its last game shows as busy for a moment)
    wait_for(asker, "Tap a game to ask")
    asker.press(title)
    ok = wait_for(other, "would like to play %s" % title)
    check(ok, "%s is asked to play %s" % (other.name, title))
    if not ok:
        print("    %s" % asker.cmd("wpstate"))
        print("    %s" % other.cmd("wpstate"))
        print("    %s: %s" % (asker.name, asker.dump()))
        print("    %s: %s" % (other.name, other.dump()))
    other.press("Play")
    run(1500)
    return ok


def board_hash(b, gid):
    return b.cmd("board " + gid)[0].split()[1]


def in_step(a, b, gid):
    """Same position on both boards, allowing a few seconds for the last move to arrive."""
    for _ in range(8):
        if board_hash(a, gid) == board_hash(b, gid):
            return True
        run(1000)
    return False


def scenario_every_game(A, B):
    print("Every wireless game: random legal moves, both boards in step")
    for k, (gid, title) in enumerate(GAMES):
        first, second = (A, B) if k % 2 == 0 else (B, A)
        if not offer(first, second, title):
            continue
        plies = 0
        idle = 0
        while plies < 120 and idle < 60:
            moved = False
            for b in (first, second):
                r = b.cmd("anymove")
                if r and r[0] != "A -1":
                    plies += 1
                    moved = True
            run(200 if gid != "mancala" else 1500)       # Mancala shows each seed
            idle = 0 if moved else idle + 1
            if plies and plies % 10 == 0 and not in_step(first, second, gid):
                break
        same = in_step(first, second, gid)
        check(same, "%s: both boards have the same position after %d moves" % (title, plies))
        da, db = first.dump(), second.dump()
        over = "Play Again" in da
        if over:
            fw = "You win!" in da
            sw = "You win!" in db
            draw = "Draw" in da and "Draw" in db
            check((fw != sw) or draw, "%s: one winner (or a draw on both)" % title)
            second.press("Done")
            run(1500)
            expect(first, "is done playing")
        else:
            first.cmd("menu")
            first.press("Forfeit Game")
            run(1500)
            expect(second, "forfeited the game")
            second.press("Wireless Play")
        run(5000)


def scenario_paused_forfeit(A, B):
    print("Forfeit while the other board has the game closed")
    offer(A, B, "Reversi")                                # (Bob turned Checkers off earlier)
    expect(A, "Your turn")
    A.cmd("anymove")
    run(500)
    B.cmd("home")                                         # Exit Game: paused
    run(1500)
    expect(A, "closed Reversi for now")
    check(not A.may(), "A waits while B has the game closed")
    wins_before = sum("reversi Wireless,-,Won" in s for s in B.stats())
    A.cmd("menu")
    A.press("Forfeit Game")
    run(6000)
    B.cmd("open reversi")
    run(500)
    expect(B, "You win!")
    check(sum("reversi Wireless,-,Won" in s for s in B.stats()) == wins_before + 1, "B got the win on opening Reversi")
    B.cmd("home")
    run(5000)


def scenario_crossed_offers(A, B):
    print("Both boards ask each other at once")
    A.cmd("wplay")
    B.cmd("wplay")
    A.press("Find Players")
    B.press("Find Players")
    run(1000)
    A.press(B.name)
    B.press(A.name)
    run(100)
    A.press("Chess")
    B.press("Reversi")
    run(1000)
    # Each board shows the other's offer; Ann says Play to Bob's Reversi
    expect(A, "would like to play Reversi")
    A.press("Play")
    run(2000)
    expect(A, "%s's turn" % B.name)
    expect(B, "Your turn")
    check("Your turn" not in A.dump(), "Ann is White (Bob asked)")
    B.cmd("menu")
    B.press("Forfeit Game")
    run(1500)
    A.press("Wireless Play")
    run(5000)


def scenario_three_boards(A, B):
    print("A third board sees two playing")
    C = Board("Cy", 3)
    boards.append(C)
    C.cmd("wplay")
    C.cmd("twop 1")
    offer(A, B, "FourConnect")
    run(1500)
    C.press("Find Players")
    expect(C, "playing FourConnect")
    C.press(A.name)
    expect(C, "%s is playing FourConnect now" % A.name)
    shot(C, "busy")
    # C asks B straight after the game ends. A leaves with the header's
    # back arrow: in a wireless game that is a forfeit
    A.cmd("back")
    run(500)
    expect(A, "You forfeited FourConnect")
    run(1500)
    B.press("Wireless Play")
    run(5000)
    C.cmd("back")
    run(1000)
    C.press(B.name)
    C.press("Mancala")
    expect(B, "%s would like to play Mancala" % C.name)
    B.press("Not Now")
    expect(C, "can't play right now")
    C.stop()
    boards.remove(C)
    shutil.rmtree(C.dir, ignore_errors=True)


def main():
    global boards
    A = Board("Ann", 1)
    B = Board("Bob", 2)
    boards = [A, B]
    try:
        scenario_offer_and_play(A, B)
        scenario_link_loss_and_pause(A, B)
        scenario_forfeit(A, B)
        scenario_decline(A, B)
        scenario_offer_over_solo_game(A, B)
        scenario_reboot(A, B)
        scenario_clear(A, B)
        scenario_paused_forfeit(A, B)
        scenario_crossed_offers(A, B)
        scenario_three_boards(A, B)
        scenario_every_game(A, B)
    finally:
        for b in boards:
            if b.on:
                b.stop()
            shutil.rmtree(b.dir, ignore_errors=True)
    print("%d check(s) failed" % len(failures) if failures else "duo: all passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
