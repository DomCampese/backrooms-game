#!/bin/bash
# Capture a fixed set of frames for before/after comparison of a change that
# should not alter what is drawn. Rendering time is pinned (BACKROOMS_TIME) so
# light flicker and wall-clock animation do not differ between runs.
#
#   tools/proof-shots.sh OUTDIR [BINARY]
#   tools/proof-shots.sh --diff DIR_A DIR_B
#
# Use a DISPLAY of your own when another capture may be running (DISPLAY=:101).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)

if [ "${1:-}" = "--diff" ]; then
    A=${2:?}; B=${3:?}; worst=0
    for png in "$A"/*.png; do
        name=$(basename "$png")
        out=$("$ROOT/tools/pixdiff.py" diff "$png" "$B/$name" 2>/dev/null | grep 'pixels differing')
        printf '%-22s %s\n' "$name" "$out"
    done
    exit 0
fi

OUT=${1:?usage: proof-shots.sh OUTDIR [BINARY]}
BIN=${2:-$ROOT/backrooms}
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
export SHOTS_DIR=$OUT BACKROOMS_BIN=$BIN
common=(BACKROOMS_SEED=1337 BACKROOMS_SHOTFRAME=60 BACKROOMS_CLEAN=1 BACKROOMS_TIME=4)

shot() {   # name, then extra environment
    local name=$1; shift
    "$ROOT/tools/shot.sh" "$name.png" "${common[@]}" "$@" | tail -1
}
for lv in 0 1 2 3 4; do shot "lv$lv" BACKROOMS_LEVEL=$lv BACKROOMS_POS=15,15,0.8; done
shot vending   BACKROOMS_LEVEL=1 BACKROOMS_POS=31.3,3,3.1416,-0.05
shot pool      BACKROOMS_LEVEL=2 BACKROOMS_POS=95,79,1.2
shot beam      BACKROOMS_LEVEL=1 BACKROOMS_FLASH=1
shot manila    BACKROOMS_LEVEL=0 BACKROOMS_MANILA=1 BACKROOMS_POS=44,16,0
shot menu      BACKROOMS_MENU=1
shot stairwell BACKROOMS_LEVEL=0 BACKROOMS_POS=-119.0,-139.2,1.57,0.3
shot atrium    BACKROOMS_LEVEL=0 BACKROOMS_POS=-24.0,-142.2,-1.57
