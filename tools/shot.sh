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
    # Check for an Xvfb on this display, not any Xvfb. The old `pgrep -x Xvfb`
    # was true if one ran on another display, so a second display was never
    # started and every capture failed at GLFW. pgrep -f only reads, so unlike
    # pkill -f (AGENTS.md) it cannot kill this shell.
    if ! pgrep -f "Xvfb ${DISPLAY} " >/dev/null; then
        # A dead Xvfb leaves its socket behind, so waiting for the socket would
        # return at once and the capture would fail at "GLFW: Failed to detect
        # any supported platform" (the regression harness segfaults instead).
        # Nothing owns this display right now, so clear the socket first and the
        # wait below means what it says.
        rm -f "/tmp/.X11-unix/X${DISPLAY#:}"
        Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 &
        XVFB_PID=$!
        for _ in $(seq 1 40); do
            [ -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && kill -0 "$XVFB_PID" 2>/dev/null && break
            sleep 0.25
        done
    fi
fi
cd "$SHOTS"
rm -f "$OUT"
LOG="${OUT%.png}.log"
if ! env "$@" BACKROOMS_SHOT="$OUT" "${BACKROOMS_BIN:-$ROOT/backrooms}" >"$LOG" 2>&1; then
    cat "$LOG"; exit 1
fi
# A failed shader compile does not crash (raylib falls back to its default
# shader and the frame goes black), so this grep is the only check for it. Keep
# ERROR: case-sensitive (raylib's TraceLog prefix): every headless run prints a
# lowercase "error: XDG_RUNTIME_DIR is invalid or not set", and matching that
# stopped the sweep after level 0.
if grep -E 'SHADER: .*([Ff]ailed|[Ee]rror)|^ERROR:' "$LOG"; then exit 1; fi
grep -E 'fps=|BENCH ' "$LOG" || true
[[ -s "$OUT" ]] || { cat "$LOG"; echo '!! no screenshot written'; exit 1; }
echo "wrote $PWD/$OUT"
