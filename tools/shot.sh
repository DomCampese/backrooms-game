#!/bin/bash
# Native macOS or Linux/Xvfb capture. Keep a full log and fail on shader errors.
set -euo pipefail
OUT=${1:?usage: shot.sh <name.png> [ENV=val ...]}; shift
[[ "$OUT" == "$(basename "$OUT")" ]] || { echo 'Use a bare screenshot filename'; exit 1; }
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SHOTS=${SHOTS_DIR:-$ROOT/shots}
mkdir -p "$SHOTS"
if [[ $(uname -s) == Linux ]]; then
    export DISPLAY=${DISPLAY:-:99}
    if ! pgrep -x Xvfb >/dev/null; then
        Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 &
        sleep 2
    fi
fi
cd "$SHOTS"
rm -f "$OUT"
LOG="${OUT%.png}.log"
if ! env "$@" BACKROOMS_SHOT="$OUT" "${BACKROOMS_BIN:-$ROOT/backrooms}" >"$LOG" 2>&1; then
    cat "$LOG"; exit 1
fi
if grep -iE 'SHADER: .*(failed|error)|ERROR:' "$LOG"; then exit 1; fi
grep -E 'fps=|BENCH ' "$LOG" || true
[[ -s "$OUT" ]] || { cat "$LOG"; echo '!! no screenshot written'; exit 1; }
echo "wrote $PWD/$OUT"
