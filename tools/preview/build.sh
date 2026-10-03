#!/bin/sh
# Builds the PC preview (Linux/CI only). Usage: tools/preview/build.sh <lvgl dir> <out dir>
# Makes two programs: `preview` (the real game list) and `preview_paging`
# (a long fake list, to check the picker's pages).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
LVGL=$1
OUT=$2
mkdir -p "$OUT/obj"
for f in $(find "$LVGL/src" -name '*.c'); do
  o="$OUT/obj/lv_$(echo "$f" | md5sum | cut -c1-12).o"
  [ "$o" -nt "$f" ] || gcc -c -O1 -w -DCYD_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -I"$ROOT/include" -I"$LVGL" "$f" -o "$o" &
  [ $(jobs -p | wc -l) -ge 8 ] && wait
done
wait
# Everything under src/ui and src/games except device-only files (the ones
# that include Arduino.h; preview_stubs.cpp stands in for them).
SRC="$(find "$ROOT/src/ui" "$ROOT/src/games" -name '*.cpp' ! -name registry.cpp | xargs grep -L '<Arduino.h>' | sort | tr '\n' ' ') $ROOT/tools/preview/preview_stubs.cpp"
# C files under src/games (generated fonts)
for f in $(find "$ROOT/src/games" -name '*.c'); do
  gcc -c -O1 -w -DLV_CONF_INCLUDE_SIMPLE -I"$ROOT/include" -I"$LVGL" "$f" -o "$OUT/obj/game_$(basename "$f" .c).o"
done
FLAGS="-std=c++17 -O1 -Wall -DCYD_PREVIEW -DLV_CONF_INCLUDE_SIMPLE -I$ROOT/include -I$LVGL -I$ROOT/src"
g++ $FLAGS "$ROOT/tools/preview/preview.cpp" "$ROOT/tools/preview/card_mockups.cpp" $SRC "$ROOT/src/games/registry.cpp" "$OUT"/obj/*.o -lm -o "$OUT/preview"
g++ $FLAGS -DCYD_PAGING_TEST "$ROOT/tools/preview/preview.cpp" "$ROOT/tools/preview/card_mockups.cpp" $SRC "$ROOT/tools/preview/paging_ops.cpp" \
  -DCYD_GAMES_DEF="\"$ROOT/tools/preview/paging_test.def\"" "$ROOT/src/games/registry.cpp" "$OUT"/obj/*.o -lm -o "$OUT/preview_paging"
