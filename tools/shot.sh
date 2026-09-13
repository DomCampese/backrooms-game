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
    # Check for an Xvfb on THIS display, not for any Xvfb at all. The old test
    # was `pgrep -x Xvfb`, which is true as soon as one exists anywhere — so
    # asking for a second display (say two worktrees verifying side by side)
    # silently skipped the launch and every capture died at GLFW with nothing
    # but ALSA warnings in the log to go on. pgrep -f is read-only, so unlike
    # the pkill -f trap in CLAUDE.md it cannot match and kill this shell.
    if ! pgrep -f "Xvfb ${DISPLAY} " >/dev/null; then
        Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 &
        for _ in $(seq 1 40); do
            [ -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && break
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
# A failed shader compile does not crash — raylib falls back to its default
# shader, the frame goes black and the frame rate goes UP — so this grep is the
# only thing standing between a broken build and a "faster" result. It must stay
# case-sensitive on ERROR:, which is raylib's own TraceLog prefix: the audio and
# GLFW stacks print a lowercase "error: XDG_RUNTIME_DIR is invalid or not set"
# on every headless run in this sandbox, and a case-insensitive match on that
# aborted the whole sweep after level 0 with nothing that looked like a cause.
if grep -E 'SHADER: .*([Ff]ailed|[Ee]rror)|^ERROR:' "$LOG"; then exit 1; fi
grep -E 'fps=|BENCH ' "$LOG" || true
[[ -s "$OUT" ]] || { cat "$LOG"; echo '!! no screenshot written'; exit 1; }
echo "wrote $PWD/$OUT"
