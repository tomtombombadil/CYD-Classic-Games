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
    "s_light_11_puzzles": "small_puzzles",
    "s_light_1_select": "small_sudoku_light",
    "s_dark_3_digit_first_notes": "small_sudoku_notes_dark",
    "s_light_17_fourconnect": "small_fourconnect",
    "s_light_19_tictactoe": "small_tictactoe",
    "s_light_13_sliding": "small_sliding",
    "s_light_15_lightswitch": "small_lightswitch",
    "s_dark_0_settings": "small_settings_dark",
    "s_custom_0_editor": "small_theme_editor",
    "s_custom_0_palette": "small_theme_palette",
    "s_custom_1_sudoku": "small_sudoku_custom",
    # 320x480 boards (3.5" and 4.0")
    "l_light_0_picker": "large_picker_light",
    "l_light_12_strategy": "large_strategy",
    "l_light_1_select": "large_sudoku_light",
    "l_dark_17_fourconnect": "large_fourconnect_dark",
    "l_light_18_twoplayer_menu": "large_twoplayer_menu",
    "l_light_21_fourconnect_stats": "large_fourconnect_stats",
}

src = pathlib.Path(sys.argv[1])
out = ROOT / "docs" / "screenshots"
out.mkdir(parents=True, exist_ok=True)
for name, dest in SHOTS.items():
    im = Image.open(src / f"{name}.ppm").convert("RGB")
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(out / f"{dest}.png", optimize=True)
    print("wrote", out / f"{dest}.png")
