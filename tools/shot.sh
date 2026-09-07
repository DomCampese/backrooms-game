#!/bin/bash
# One headless screenshot. usage: tools/shot.sh <name.png> [BACKROOMS_X=y ...]
#
# TakeScreenshot writes relative to the process working directory and rejects
# path separators outright, so this cds into the output directory and passes a
# bare filename. Output dir is $SHOTS_DIR, default ./shots.
#
# Note on BACKROOMS_SHOTFRAME: blackouts are scheduled off wall-clock time
# (GetTime() + 30 + rand*60), and the software rasteriser runs about 3 fps, so
# the default 600 — and even 150 — can capture a pitch-black frame that looks
# exactly like a broken shader. 80 is inside the guaranteed-lit window.
set -uo pipefail
OUT=${1:?usage: shot.sh <name.png> [ENV=val ...]}; shift
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHOTS=${SHOTS_DIR:-$ROOT/shots}
mkdir -p "$SHOTS"
export DISPLAY=${DISPLAY:-:99}
pgrep -x Xvfb >/dev/null || { Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 & sleep 2; }
cd "$SHOTS" || exit 1
# Shader errors must pass through this filter. A failed compile does not crash:
# raylib silently falls back to its default shader, the frame goes black, and
# the frame rate goes UP. Never grep this output for something else only.
env "$@" BACKROOMS_SHOT="$OUT" "$ROOT/backrooms" 2>/dev/null \
  | grep -iE "SHADER: .*(failed|error)|ERROR:|fps="
[ -f "$SHOTS/$OUT" ] && echo "wrote $SHOTS/$OUT" || { echo "!! no screenshot written"; exit 1; }
