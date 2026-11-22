# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Vaibhav Agarwal

import chess, chess.engine, time, sys

NEKO = "/home/neko/chess_by_neko/runs/depth6_run/neko-chess"
STOCKFISH = "/usr/games/stockfish"
GAMES_PER_LEVEL = 10
TIME_LIMIT = 1.0
START_ELO = 2150
STEP = 50
MIN_ELO = 500

# Colors
RESET = "\033[0m"
BOLD = "\033[1m"
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
MAGENTA = "\033[95m"

CAT_WIN = r"""
      /\_/\  
     ( ^.^ ) NEKO WINS!!
      > ^ 
"""

CAT_CHEER = r"""
      /\_/\  
     ( -.- )  keep going neko, you got this...
      > ^ 
"""

def slow_print(text, color=RESET, delay=0.0):
    print(f"{color}{text}{RESET}")
    time.sleep(delay)

neko = chess.engine.SimpleEngine.popen_uci(NEKO)
sf = chess.engine.SimpleEngine.popen_uci(STOCKFISH)

elo = START_ELO
while elo >= MIN_ELO:
    sf.configure({"UCI_LimitStrength": True, "UCI_Elo": elo})
    neko_pts = 0.0
    sf_pts = 0.0

    slow_print(f"\n{'='*50}", CYAN)
    slow_print(f"  Testing NekoChess vs Stockfish @ {elo} Elo", BOLD + CYAN)
    slow_print(f"{'='*50}", CYAN)

    for g in range(GAMES_PER_LEVEL):
        board = chess.Board()
        neko_is_white = (g % 2 == 0)
        while not board.is_game_over():
            engine = neko if (board.turn == chess.WHITE) == neko_is_white else sf
            result = engine.play(board, chess.engine.Limit(time=TIME_LIMIT))
            board.push(result.move)

        outcome = board.outcome()
        if outcome.winner is None:
            neko_pts += 0.5
            sf_pts += 0.5
            slow_print(f"  Game {g+1}: DRAW  (Neko was {'White' if neko_is_white else 'Black'})", YELLOW)
        elif outcome.winner == neko_is_white:
            neko_pts += 1
            slow_print(f"  Game {g+1}: NEKO WINS 🐱  (Neko was {'White' if neko_is_white else 'Black'})", GREEN)
        else:
            sf_pts += 1
            slow_print(f"  Game {g+1}: Stockfish wins  (Neko was {'White' if neko_is_white else 'Black'})", RED)

    slow_print(f"\n  Score @ {elo} Elo: {BOLD}NekoChess {neko_pts}{RESET}{CYAN} - {BOLD}Stockfish {sf_pts}", CYAN)

    if neko_pts > sf_pts:
        print(f"{GREEN}{BOLD}{CAT_WIN}{RESET}")
        slow_print(f">>> NekoChess outscored Stockfish at {elo} Elo! <<<", BOLD + GREEN)
        slow_print(f">>> Estimated NekoChess strength: around {elo} Elo <<<", BOLD + GREEN)
        break
    else:
        print(f"{MAGENTA}{CAT_CHEER}{RESET}")
        slow_print(f"  Not quite — dropping to {elo - STEP} Elo and trying again...", MAGENTA, 0.5)

    elo -= STEP
else:
    slow_print(f"\nReached minimum test floor ({MIN_ELO} Elo) without an overall win.", RED)

neko.quit()
sf.quit()
