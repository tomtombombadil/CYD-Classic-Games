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
        # name: two words from the name lists (src/net/names.cpp)
        self.name, self.mac = name, mac
        self.dir = files or tempfile.mkdtemp(prefix="duo_%s_" % name.replace(" ", "_"))
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


def icons(board):
    return board.cmd("icons")[0]


def play_page(b):
    b.cmd("wplay")
    run(100)


def request(asker, other, title, answer="Play"):
    """asker finds `other`, asks for `title`; other answers. True if asked."""
    play_page(asker)
    asker.press("Find Players")
    if not wait_for(asker, other.name):
        check(False, "%s sees %s" % (asker.name, other.name))
        print("    %s" % asker.cmd("wpstate"))
        print("    %s %s" % (other.cmd("wpstate"), other.cmd("radio")))
        print("    %s: %s" % (other.name, other.dump()))
        print("    %s: %s" % (asker.name, asker.dump()))
        return False
    for _ in range(3):                                  # (the list may be rebuilding as it's tapped)
        asker.press(other.name)
        if wait_for(asker, "Tap a game to ask", 3000):
            break
    asker.press(title)
    ok = wait_for(other, "would like to play %s" % title)
    check(ok, "%s is asked to play %s" % (other.name, title))
    if not ok:
        print("    %s" % asker.cmd("wpstate"))
        print("    %s" % other.cmd("wpstate"))
        print("    %s: %s" % (asker.name, asker.dump()))
        print("    %s: %s" % (other.name, other.dump()))
        return False
    if answer:
        other.press(answer)
    return True


def offer(asker, other, title):
    """A session of `title` between the two boards (the asked board moves first)."""
    if not request(asker, other, title):
        return False
    ok = wait_for(other, "Your turn", 6000) or wait_for(other, "Respond in", 1)
    check(ok, "%s: the game starts" % title)
    run(500)
    return ok


def wait_may(b, ms=6000):
    t = 0
    while t < ms and not b.may():
        run(100)
        t += 100
    return b.may()


def counted(b, gid, what):
    return sum(("%s Wireless,-,%s" % (gid, what)) in s for s in b.stats())


def leave_session(A, B):
    """End whatever session is on (Goodbye or a forfeit) and get both back to the start."""
    for b in (A, B):
        if "Goodbye" in b.dump():
            b.press("Goodbye")
            run(1500)
            break
    for b in (A, B):
        d = b.dump()
        if "Forfeit" in d or "Your turn" in d or "turn" in d:
            pass
    for b in (A, B):
        b.cmd("home")
    run(5000)


def scenario_offer_and_play(A, B):
    print("Request, Connecting, play, rematch")
    for b in (A, B):
        play_page(b)
        b.cmd("twop 1")
        b.cmd("timer 0")
    run(500)
    check(all("Dozing" in b.cmd("radio")[0] for b in (A, B)), "idle boards in 2P doze (battery)")
    A.press("Find Players")
    expect(A, "Searching...")
    expect(A, B.name)
    expect(A, "available")
    shot(A, "players")
    A.press(B.name)
    expect(A, "Tap a game to ask %s" % B.name)
    A.press("Tic-Tac-Toe")
    expect(A, "Requesting...")
    expect(B, "%s would like to play Tic-Tac-Toe." % A.name)
    shot(B, "offer")
    B.press("Play")
    expect(B, "Your turn (X)")                          # the asked player moves first
    check(all("Listening" in b.cmd("radio")[0] for b in (A, B)), "in a game both radios stay awake")
    expect(A, "Their turn")
    check("2p=1" in icons(A) and "2p=1" in icons(B), "both 2P icons filled")
    shot(A, "start")
    tictactoe_moves(B, A, [0, 3, 1, 4, 2])             # X wins on the top row
    expect(B, "You win!")
    expect(A, "You lost")
    expect(A, "Again")
    expect(A, "New Game")
    expect(A, "Goodbye")
    shot(A, "over")
    check(counted(B, "tictactoe", "Won") == 1, "B recorded a win")
    check(counted(A, "tictactoe", "Lost") == 1, "A recorded a loss")
    B.cmd("menu")
    check("Forfeit Game" not in B.dump(), "no Forfeit once the game is over")
    B.cmd("back")
    B.press("Again")
    expect(A, "%s wants to play again" % B.name)
    A.press("Again")
    expect(A, "Your turn (X)")                          # the other player starts now
    expect(B, "Their turn")


def scenario_link_loss(A, B):
    print("Link loss: wait, then carry on")
    wait_may(A)
    A.cmd("move 4")
    run(500)
    blocked.add(A)
    run(4000)
    expect(B, "Out of range")
    expect(B, "out of range")
    check(not B.may(), "B can't move while A is out of range")
    blocked.discard(A)
    run(2000)
    expect(B, "Your turn (O)")


def scenario_leave_asks(A, B):
    print("Leaving asks first, then forfeits")
    wait_may(B)
    B.cmd("back")
    expect(B, "Leaving will forfeit this game.")
    shot(B, "leave")
    B.press("Keep Playing")
    run(200)
    check(B.may(), "Keep Playing: still in the game")
    B.cmd("back")
    B.press("Leave Game")
    run(1500)
    expect(B, "You forfeited Tic-Tac-Toe")
    expect(B, "Play Mode")                               # back on the Play page
    expect(A, "You win!")
    expect(A, "%s left and forfeited the game. You win!" % B.name)
    A.press("OK")
    check(counted(A, "tictactoe", "Won") == 1, "A has a win")
    check(counted(B, "tictactoe", "Lost") == 1, "B has a loss")
    A.press("Done")
    run(5000)
    expect(A, "Find Players")


def scenario_menu_forfeit(A, B):
    print("Forfeit Game in the menu")
    offer(A, B, "FourConnect")
    wait_may(B)
    B.cmd("anymove")
    run(500)
    A.cmd("menu")
    expect(A, "Forfeit Game")
    shot(A, "game_menu")
    A.press("Forfeit Game")
    run(1500)
    expect(A, "You forfeited FourConnect")
    expect(B, "left and forfeited the game. You win!")
    B.press("OK")
    check(counted(B, "fourconnect", "Won") == 1, "B got the win")
    B.press("Done")
    run(5000)


def scenario_answers(A, B):
    print("No Thanks, Other Game, no answer, cancelled")
    request(A, B, "Chess", "No Thanks")
    expect(A, "%s said 'no thanks'." % B.name)
    shot(A, "no_thanks")
    run(5000)
    # Other Game: B picks one of A's games and asks back
    A.press("Checkers")
    expect(B, "would like to play Checkers")
    B.press("Other Game")
    expect(B, "Pick the game you'd rather play")
    expect(A, "would rather play a different game")
    B.press("Mancala")
    expect(A, "%s would like to play Mancala." % B.name)
    A.press("No Thanks")
    expect(B, "%s said 'no thanks'." % A.name)
    B.cmd("home")
    A.cmd("wplay")
    run(5000)
    # Nobody answers
    request(A, B, "Reversi", None)
    run(31000)
    expect(A, "There was no answer from %s." % B.name)
    expect(B, "stopped waiting for an answer")
    run(5000)
    # Cancelled
    A.press("Find Players")
    expect(A, B.name)
    A.press(B.name)
    wait_for(A, "Tap a game to ask")
    A.press("Reversi")
    expect(B, "would like to play Reversi")
    A.press("Stop Asking")
    expect(B, "%s cancelled the request." % A.name)
    B.press("OK")
    A.cmd("home")
    B.cmd("home")
    run(5000)


def scenario_one_player_comes_back(A, B):
    print("A one-player game is put aside and comes back")
    B.cmd("open tictactoe")
    run(300)
    B.cmd("menu")
    B.press("Easy")
    run(300)
    wait_may(B)
    B.cmd("move 4")                                       # a move vs the computer
    run(1500)
    hash_before = board_hash(B, "tictactoe")
    B.cmd("home")
    run(300)
    offer(A, B, "Tic-Tac-Toe")
    expect(B, "Your turn (X)")
    tictactoe_moves(B, A, [0, 3, 1, 4, 2])
    expect(A, "Goodbye")
    A.press("Goodbye")
    run(1500)
    expect(A, "Thanks for playing!")
    expect(B, "%s said goodbye. Thanks for the game!" % A.name)
    expect(B, "Play Mode")
    run(5000)
    B.cmd("open tictactoe")
    run(300)
    expect(B, "Resuming your previous")
    check(board_hash(B, "tictactoe") == hash_before, "B's one-player game is as it was")
    check(sum("tictactoe Computer" in s for s in B.stats()) == 0, "nothing recorded for the put-aside game")
    B.cmd("home")
    run(1000)


def scenario_busy_and_priority(A, B):
    print("A third board: the first asker has priority; busy boards")
    C = Board("Tiny Yeti", 3)
    boards.append(C)
    play_page(C)
    C.cmd("twop 1")
    C.cmd("timer 0")
    request(A, B, "Chess", None)                          # B's question is up
    play_page(C)
    C.press("Find Players")
    expect(C, B.name)
    C.press(B.name)
    wait_for(C, "Tap a game to ask")
    C.press("Reversi")
    expect(C, "%s can't play right now." % B.name)
    B.press("Play")
    expect(B, "Your turn")
    run(1500)
    C.cmd("back")
    run(1500)
    expect(C, "busy")
    shot(C, "busy")
    # Clear 2P Sessions with the partner there and the game going = a forfeit
    play_page(B)
    B.press("Clear 2P Sessions")
    run(1500)
    expect(A, "%s left and forfeited the game. You win!" % B.name)
    A.press("OK")
    A.press("Done")
    C.stop()
    boards.remove(C)
    shutil.rmtree(C.dir, ignore_errors=True)
    B.cmd("home")
    run(5000)


def scenario_restart(A, B):
    print("A board restarts mid-game: the session waits, then both carry on")
    offer(A, B, "Tic-Tac-Toe")
    tictactoe_moves(B, A, [0, 4])
    B.stop()
    run(2000)
    expect(A, "Out of range")
    B.start()
    run(500)
    check("2p=1" in icons(B), "B's 2P icon is lit after the restart (a session waits)")
    expect(A, "is back in range. Continue Tic-Tac-Toe?")
    expect(B, "is back in range. Continue Tic-Tac-Toe?")
    shot(B, "meet")
    B.press("Continue")
    expect(B, "Waiting for %s" % A.name)
    A.press("Continue")
    run(2000)
    expect(B, "Your turn (X)")
    check(in_step(A, B, "tictactoe"), "both boards have the same position")
    tictactoe_moves(B, A, [8, 2, 6, 7, 3])
    run(1500)
    for b in (A, B):
        if "Goodbye" in b.dump():
            b.press("Goodbye")
            break
    run(5000)


def scenario_comm_close(A, B):
    print("Lost touch: No reply, Close Game (not counted)")
    for b in (A, B):
        b.cmd("timer 30")
    offer(A, B, "FourConnect")
    before_a, before_b = len(A.stats()), len(B.stats())
    expect(B, "Respond in")
    expect(A, "Waiting...")
    blocked.add(B)
    run(31000)
    expect(A, "No reply from %s." % B.name)
    shot(A, "noreply")
    A.press("Close Game")
    expect(A, "Communications failed. Game not counted.")
    blocked.discard(B)
    run(4000)
    expect(B, "Communications failed. Game not counted.")
    B.press("OK")
    check(len(A.stats()) == before_a and len(B.stats()) == before_b, "nothing recorded on either board")
    B.cmd("home")
    run(5000)


def scenario_comm_wait(A, B):
    print("Lost touch: Keep Waiting, put away, met again")
    offer(A, B, "Reversi")
    wait_may(B)
    B.cmd("anymove")
    run(500)
    blocked.add(B)
    run(31000)
    expect(A, "No reply from")
    expect(B, "No reply from")
    A.press("Keep Waiting")
    B.press("Keep Waiting")
    run(31000)
    expect(A, "is saved. It goes on when you meet again.")
    check("2p=1" in icons(A) and "2p=1" in icons(B), "2P icons lit while the session waits")
    blocked.discard(B)
    run(3000)
    expect(A, "is back in range. Continue Reversi?")
    expect(B, "is back in range. Continue Reversi?")
    A.press("Continue")
    B.press("Continue")
    run(2000)
    check(in_step(A, B, "reversi"), "Reversi goes on with both in step")
    expect(A, "Respond in")
    # The move timer: A doesn't move
    run(31000)
    expect(A, "You haven't responded in time.")
    shot(A, "timer")
    A.press("OK")
    run(11000)
    expect(A, "You ran out of time and forfeited the game.")
    A.press("OK")
    expect(B, "%s ran out of time and forfeited the game. You win!" % A.name)
    B.press("OK")
    check(counted(B, "reversi", "Won") == 1, "B got the win")
    for b in (A, B):
        b.press("Done")
        b.cmd("timer 0")
    run(5000)


def scenario_forfeit_away(A, B):
    print("Forfeit while the other board is out of range: it hears later")
    offer(A, B, "Mancala")
    wait_may(B)
    B.cmd("anymove")
    run(2500)
    blocked.add(B)
    run(500)
    A.cmd("menu")
    A.press("Forfeit Game")
    run(8000)
    blocked.discard(B)
    run(3000)
    expect(B, "left and forfeited the game.")
    B.press("OK")
    check(counted(B, "mancala", "Won") == 1, "B got the win")
    B.press("Done")
    run(5000)


def scenario_crossed(A, B):
    print("Both boards ask each other at once")
    for b in (A, B):
        play_page(b)
        b.press("Find Players")
    run(1500)
    A.press(B.name)
    B.press(A.name)
    run(200)
    A.press("Chess")
    B.press("Reversi")
    run(2000)
    da, db = A.dump(), B.dump()
    one = ("would like to play Reversi" in da) != ("would like to play Chess" in db)
    check(one, "exactly one board shows the other's question")
    asked = A if "would like to play Reversi" in da else B
    asked.press("Play")
    run(2000)
    check("Your turn" in asked.dump(), "the asked player moves first")
    asked.cmd("menu")
    asked.press("Forfeit Game")
    run(1500)
    other = B if asked is A else A
    other.press("OK")
    other.press("Done")
    run(5000)


def scenario_new_game(A, B):
    print("Game over: New Game with the same player")
    offer(A, B, "Tic-Tac-Toe")
    tictactoe_moves(B, A, [0, 3, 1, 4, 2])
    expect(A, "New Game")
    A.press("New Game")
    expect(A, "Tap a game to ask %s" % B.name)
    A.press("Checkers")
    expect(B, "would like to play Checkers")
    B.press("Play")
    expect(B, "Your turn")
    B.cmd("menu")
    B.press("Forfeit Game")
    run(1500)
    A.press("OK")
    A.press("Done")
    run(5000)


def scenario_switching(A, B):
    """Games ended every way, then another game, many times: moves flow after each switch."""
    print("Switching games: forfeit, leave, New Game, Goodbye - moves flow every time")
    import random as _r
    R = _r.Random(11)
    games = [("tictactoe", "Tic-Tac-Toe"), ("reversi", "Reversi"), ("fourconnect", "FourConnect"),
             ("checkers", "Checkers"), ("chess", "Chess")]

    def flows(gid):
        for _ in range(3):
            mover = None
            for _ in range(60):
                if A.may():
                    mover = A
                    break
                if B.may():
                    mover = B
                    break
                run(100)
            if not mover:
                print("    stuck: %s / %s" % (A.cmd("wpstate"), B.cmd("wpstate")))
                return False
            mover.cmd("anymove")
            run(400)
            if "Again" in A.dump():
                return True
        return in_step(A, B, gid)

    def to_the_end(p):
        for _ in range(300):
            if "Goodbye" in p.dump():
                return True
            for b in (A, B):
                b.cmd("anymove")
            run(150)
        return False

    gid, title = "reversi", "Reversi"
    offer(A, B, title)
    for step, act in enumerate(["newgame", "forfeit", "goodbye", "leave", "newgame", "newgame"]):
        check(flows(gid), "switch %d: %s moves flow" % (step, title))
        p, q = (A, B) if step % 2 == 0 else (B, A)
        gid, nxt = R.choice(games)
        if act in ("newgame", "goodbye") and not to_the_end(p):
            act = "forfeit"
        if act == "newgame":
            p.press("New Game")
            wait_for(p, "Tap a game to ask", 4000)
            p.press(nxt)
            if wait_for(q, "would like to play %s" % nxt):
                q.press("Play")
            run(2500)
        else:
            if act == "forfeit":
                p.cmd("menu")
                p.press("Forfeit Game")
            elif act == "leave":
                p.cmd("back")
                p.press("Leave Game")
            else:
                p.press("Goodbye")
            run(1500)
            for b in (A, B):
                b.press("OK")
                b.press("Done")
                b.cmd("home")
            run(4000)
            offer(q, p, nxt)
        title = nxt
    check(flows(gid), "after the last switch: %s moves flow" % title)
    for b in (A, B):
        b.cmd("menu")
        if b.press("Forfeit Game"):
            break
    run(1500)
    for b in (A, B):
        b.press("OK")
        b.press("Done")
        b.cmd("home")
    run(5000)


def board_hash(b, gid):
    return b.cmd("board " + gid)[0].split()[1]


def in_step(a, b, gid):
    """Same position on both boards, allowing a few seconds for the last move to arrive."""
    for _ in range(8):
        if board_hash(a, gid) == board_hash(b, gid):
            return True
        run(1000)
    return False


GAMES = [("fourconnect", "FourConnect"), ("tictactoe", "Tic-Tac-Toe"), ("reversi", "Reversi"),
         ("checkers", "Checkers"), ("chess", "Chess"), ("mancala", "Mancala"), ("morris", "Nine Men's Morris")]


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
        if "Goodbye" in da:
            fw = "You win!" in da
            sw = "You win!" in db
            draw = "Draw" in da and "Draw" in db
            check((fw != sw) or draw, "%s: one winner (or a draw on both)" % title)
            second.press("Goodbye")
            run(1500)
            expect(first, "said goodbye")
        else:
            first.cmd("menu")
            first.press("Forfeit Game")
            run(1500)
            expect(second, "forfeited the game")
            second.press("OK")
            second.press("Done")
        run(5000)


def main():
    global boards
    A = Board("Jolly Llama", 1)
    B = Board("Zippy Otter", 2)
    boards = [A, B]
    try:
        scenario_offer_and_play(A, B)
        scenario_link_loss(A, B)
        scenario_leave_asks(A, B)
        scenario_menu_forfeit(A, B)
        scenario_answers(A, B)
        scenario_one_player_comes_back(A, B)
        scenario_busy_and_priority(A, B)
        scenario_restart(A, B)
        scenario_comm_close(A, B)
        scenario_comm_wait(A, B)
        scenario_forfeit_away(A, B)
        scenario_crossed(A, B)
        scenario_new_game(A, B)
        scenario_switching(A, B)
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
