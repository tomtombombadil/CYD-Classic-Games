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
    "s_dark_0_picker": "small_picker_dark",
    "s_light_1_select": "small_sudoku_light",
    "s_dark_3_digit_first_notes": "small_sudoku_notes_dark",
    "s_light_4_hint": "small_sudoku_hint",
    "s_light_5_menu": "small_sudoku_menu",
    # 320x480 boards (3.5" and 4.0")
    "l_light_0_picker": "large_picker_light",
    "l_light_1_select": "large_sudoku_light",
    "l_light_5_menu": "large_sudoku_menu",
    "l_light_6_stats": "large_sudoku_stats",
    "l_light_8_solved": "large_sudoku_solved",
    "l_dark_7_settings": "large_settings_dark",
}

src = pathlib.Path(sys.argv[1])
out = ROOT / "docs" / "screenshots"
out.mkdir(parents=True, exist_ok=True)
for name, dest in SHOTS.items():
    im = Image.open(src / f"{name}.ppm").convert("RGB")
    im = im.resize((im.width * 2, im.height * 2), Image.NEAREST)
    im.save(out / f"{dest}.png", optimize=True)
    print("wrote", out / f"{dest}.png")
