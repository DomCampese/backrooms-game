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
c++ -std=c++17 -O2 -Wall -Wno-missing-field-initializers -Irlshim \
    src/*.cpp -o backrooms "$SO" "-Wl,-rpath,$(dirname "$SO")" \
    "-lpython$PYV" -lm -ldl -lpthread
echo "built ./backrooms (python$PYV)"
