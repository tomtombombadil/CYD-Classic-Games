"""Turn preview renders into the README screenshots (Claude's Linux helper).

Usage: python3 tools/preview/readme_shots.py <dir with preview .ppm files>
Writes docs/screenshots/*.png at 2x (nearest neighbour, so pixels stay crisp).
"""
import pathlib
import sys

from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parents[2]
SHOTS = {
    # 240x320 boards (2.8" and 3.2")
    "s_light_0_picker": "small_picker_light",
    "s_light_12_strategy": "small_strategy",
    "s_light_1_select": "small_sudoku_light",
    "s_light_5_menu": "small_sudoku_menu",
    "s_light_26_chess": "small_chess",
    "s_light_28_chess_peek": "small_chess_peek",
    "s_light_24_checkers": "small_checkers",
    "s_light_23_reversi": "small_reversi",
    "s_light_17_fourconnect": "small_fourconnect",
    "s_light_19_tictactoe": "small_tictactoe",
    "s_light_29_cyddle": "small_cyddle",
    "s_light_30_yahtcyd": "small_yahtcyd",
    "s_light_52_rpgdice_every": "small_rpgdice",
    "s_light_52_rpgdice_preset": "small_rpgdice_preset",
    "s_light_13_sliding": "small_sliding",
    "s_light_32_minesweeper": "small_minesweeper",
    "s_light_36_mastercyd": "small_mastercyd",
    "s_light_35_twenty48": "small_2048",
    "s_light_37_pegs": "small_pegs",
    "s_light_38_memory": "small_memory",
    "s_light_39_nonogram": "small_nonogram",
    "s_light_45_solitaire": "small_solitaire",
    "s_dark_45_solitaire_win2": "small_solitaire_win",
    "s_light_48_spider": "small_spider",
    "s_light_47_pyramid": "small_pyramid",
    "s_light_46_golf": "small_golf",
    "s_light_49_freecell": "small_freecell",
    "s_light_51_blackjack_done": "small_blackjack",
    "s_light_54_vpoker_held": "small_vpoker",
    "s_light_56_holdem_flop": "small_holdem",
    "s_light_40_help_twenty48_2": "small_help",
    "s_light_15_lightswitch": "small_lightswitch",
    "s_dark_0_settings": "small_settings_dark",
    "s_custom_0_editor": "small_theme_editor",
    # 320x480 boards (3.5" and 4.0")
    "l_light_0_picker": "large_picker_light",
    "l_dark_26_chess": "large_chess_dark",
    "l_dark_29_cyddle_solved": "large_cyddle_dark",
    "l_dark_30_yahtcyd": "large_yahtcyd_dark",
    "l_light_18_twoplayer_menu": "large_twoplayer_menu",
    "l_dark_23_reversi": "large_reversi_dark",
    "l_dark_32_minesweeper_lost": "large_minesweeper_dark",
}

src = pathlib.Path(sys.argv[1])
out = ROOT / "docs" / "screenshots"
out.mkdir(parents=True, exist_ok=True)
for name, dest in SHOTS.items():
    im = Image.open(src / f"{name}.ppm").convert("RGB")
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(out / f"{dest}.png", optimize=True)
    print("wrote", out / f"{dest}.png")
