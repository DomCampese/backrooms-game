#!/bin/bash
# Record tests/traces/walk-l0-l2.trace: the game driven through a fixed script
# of keys and mouse moves under Xvfb (xdotool), with BACKROOMS_RECORD on. The
# script starts a run, walks, sprints, looks round, calls the hunter up and
# shoots at it, reloads, throws a flare, drinks, pauses, and takes the F3 key to
# the next level twice (Level 0 to 2).
#
#   tools/record-trace.sh [OUT] [BINARY]
#   ./replay OUT
#
# Keys are held across frames: at a software rasteriser's 2-3 fps a tap shorter
# than a frame lands between two polls and raylib never sees it. The trace
# depends on the frame rate (dt and the clock are recorded), so each recording
# differs; a replay of any one of them must match it exactly.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/tests/traces/walk-l0-l2.trace}")
BIN=$(realpath "${2:-$ROOT/backrooms}")
command -v xdotool >/dev/null || { echo "record-trace: needs xdotool"; exit 2; }
export DISPLAY=${DISPLAY:-:97}
if ! pgrep -f "Xvfb ${DISPLAY} " >/dev/null; then
    rm -f "/tmp/.X11-unix/X${DISPLAY#:}"
    Xvfb "$DISPLAY" -screen 0 1440x850x24 >/dev/null 2>&1 &
    for _ in $(seq 1 40); do [ -e "/tmp/.X11-unix/X${DISPLAY#:}" ] && break; sleep 0.25; done
fi
# The run keeps records; keep them out of the real home directory.
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
# exec, so $! is the game: killing a subshell leaves the game running and recording.
(cd "$WORK" && exec env HOME="$WORK" BACKROOMS_SEED=1337 BACKROOMS_RECORD="$OUT" "$BIN" >"$WORK/game.log" 2>&1) &
GAME=$!
for _ in $(seq 1 60); do
    WID=$(xdotool search --name "BACKROOMS" 2>/dev/null | head -1 || true)
    [ -n "$WID" ] && break
    sleep 0.5
done
[ -n "$WID" ] || { echo "record-trace: no game window"; cat "$WORK/game.log"; exit 1; }
sleep 4   # the first frames build Level 0's surfaces

key() { xdotool keydown --window "$WID" "$1"; sleep 0.9; xdotool keyup --window "$WID" "$1"; sleep 0.9; }
hold() { xdotool keydown --window "$WID" "$1"; sleep "$2"; xdotool keyup --window "$WID" "$1"; sleep 0.8; }
click() { xdotool mousedown 1; sleep 0.9; xdotool mouseup 1; sleep 1.2; }
look() { xdotool mousemove_relative -- "$1" "$2"; sleep 1; }

key Return                          # begin a run
xdotool mousemove 720 425; click    # capture the mouse
hold w 5
look 120 0
look -60 10
xdotool keydown --window "$WID" shift; hold w 3; xdotool keyup --window "$WID" shift
key space
key f                               # torch
key m                               # chalk
key F3                              # the debug keys
key e                               # the hunter, about 12 m ahead
sleep 3
click; click                        # two shots
key r
sleep 2
key c                               # a chase
hold s 3
key 2; key q                        # a flare, thrown
key 3                               # a drink
key p; sleep 2; key p               # pause and resume
key n                               # Level 1
hold w 4
key 4; key 1
look 0 -80
click
key n                               # Level 2
hold w 3
kill "$GAME"; wait "$GAME" 2>/dev/null || true
echo "recorded $(grep -c '^digest' "$OUT") frames to $OUT"
