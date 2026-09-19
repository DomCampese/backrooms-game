#!/bin/bash
# Build the game against rlshim/ and the wheel's raylib .so. Run
# tools/sandbox-setup.sh once first. You will run this one dozens of times.
set -euo pipefail
cd "$(dirname "$0")/.."
SO=$(ls "$PWD"/.rlwheel/raylib/_raylib_cffi.cpython-*-linux-gnu.so 2>/dev/null | head -1)
[ -n "$SO" ] || { echo "no raylib .so — run tools/sandbox-setup.sh first"; exit 1; }
# Absolute path plus an rpath, never a relative one: the loader resolves a
# relative DT_NEEDED against the *current* directory, so a relatively-linked
# binary dies with "cannot open shared object file" as soon as anything runs it
# from elsewhere — which tools/shot.sh does on every single screenshot.
# the wheel's .so is a CPython extension, so link the matching libpython
PYV=$(basename "$SO" | sed -n 's/.*cpython-\([0-9]\)\([0-9]*\)-.*/\1.\2/p')
FLAGS=(-std=c++17 -O2 -Wall -Wno-missing-field-initializers -Irlshim)
LINK=("$SO" "-Wl,-rpath,$(dirname "$SO")" "-lpython$PYV" -lm -ldl -lpthread)

# Delete the target before compiling, so a failed build cannot leave a working
# binary behind. c++ only replaces its output on success, so without this the
# previous ./backrooms survives the failure and the next tools/shot.sh captures
# THE OLD BUILD — a green-looking screenshot of code that is not the code you
# just wrote. That has cost this project two separate debugging sessions (see
# CLAUDE.md, "A build failure looks exactly like a passing build if you only
# read the last line"), because the compiler error scrolls past and the capture
# afterwards works perfectly.
#
# Belt and braces with `set -e`: this way the failure is visible as a missing
# binary even when someone pipes the build's output through a filter and only
# reads the tail.
build_mapdump() {
    rm -f mapdump
    c++ "${FLAGS[@]}" tools/mapdump.cpp src/world.cpp src/util.cpp src/levels.cpp \
        src/textures.cpp -o mapdump "${LINK[@]}"
    echo "built ./mapdump (python$PYV)"
}
build_game() {
    rm -f backrooms
    python3 tools/embed-materials.py
    c++ "${FLAGS[@]}" src/*.cpp -o backrooms "${LINK[@]}"
    echo "built ./backrooms (python$PYV)"
}

case "${1:-game}" in
    game)    build_game ;;
    mapdump) build_mapdump ;;
    all)     build_game; build_mapdump ;;
    *)       echo "usage: $(basename "$0") [game|mapdump|all]"; exit 2 ;;
esac
