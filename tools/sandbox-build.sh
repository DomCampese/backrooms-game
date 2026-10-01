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
# -ffp-contract=off: no fused multiply-adds, so core's results do not depend on
# the compiler (src/core/fp_strict.h). Every build here compiles core.
CORE_FLAGS=(-std=c++17 -O2 -Wall -Wno-missing-field-initializers -ffp-contract=off)
FLAGS=("${CORE_FLAGS[@]}" -Irlshim)
LINK=("$SO" "-Wl,-rpath,$(dirname "$SO")" "-lpython$PYV" -lm -ldl -lpthread)
# src/ is the raylib platform; src/core and src/sim are the engine-independent
# layers (docs/migration.md). sim may not exist yet.
shopt -s nullglob
CORE=(src/core/*.cpp)
GAME=(src/*.cpp "${CORE[@]}" src/sim/*.cpp src/port/*.cpp)
shopt -u nullglob

# Delete the target before compiling, so a failed build cannot leave a working
# binary behind. c++ only replaces its output on success, so without this the
# previous ./backrooms survives the failure and the next tools/shot.sh captures
# THE OLD BUILD — a green-looking screenshot of code that is not the code you
# just wrote. That has cost this project two separate debugging sessions (see
# AGENTS.md, "A build failure looks exactly like a passing build if you only
# read the last line"), because the compiler error scrolls past and the capture
# afterwards works perfectly.
#
# Belt and braces with `set -e`: this way the failure is visible as a missing
# binary even when someone pipes the build's output through a filter and only
# reads the tail.
build_mapdump() {
    rm -f mapdump
    c++ "${CORE_FLAGS[@]}" tools/mapdump.cpp "${CORE[@]}" -o mapdump
    echo "built ./mapdump (core only)"
}
# contract: core's golden answers (docs/migration.md, "Contract tests").
build_contract() {
    rm -f contract
    c++ "${CORE_FLAGS[@]}" tools/contract.cpp tools/contract_lib.cpp "${CORE[@]}" -o contract
    echo "built ./contract (core only)"
}
# replay: a recorded trace through the sim alone (src/sim/trace.h).
build_replay() {
    rm -f replay
    c++ "${CORE_FLAGS[@]}" tools/replay.cpp src/sim/*.cpp "${CORE[@]}" -o replay
    echo "built ./replay (core and sim only)"
}
# greybox-view: the port's greybox (src/port) round a spot, drawn with raylib. Port
# code calls the sim (scene.cpp), so the sim links in too.
build_greybox_view() {
    rm -f greybox-view
    c++ "${FLAGS[@]}" tools/greybox-view.cpp src/port/*.cpp src/sim/*.cpp "${CORE[@]}" -o greybox-view "${LINK[@]}"
    echo "built ./greybox-view (python$PYV)"
}
# texdump: every texture generator, run without a window, written to PNG with
# its mean colour — see the top of tools/texdump.cpp.
build_texdump() {
    rm -f texdump
    python3 tools/embed-materials.py
    c++ "${FLAGS[@]}" tools/texdump.cpp src/textures.cpp src/surfaces.cpp src/util.cpp \
        "${CORE[@]}" -o texdump "${LINK[@]}"
    echo "built ./texdump (python$PYV)"
}
build_game() {
    rm -f backrooms
    python3 tools/embed-materials.py
    c++ "${FLAGS[@]}" "${GAME[@]}" -o backrooms "${LINK[@]}"
    echo "built ./backrooms (python$PYV)"
}

case "${1:-game}" in
    game)    build_game ;;
    mapdump) build_mapdump ;;
    texdump) build_texdump ;;
    contract) build_contract ;;
    replay)  build_replay ;;
    greybox-view) build_greybox_view ;;
    all)     build_game; build_mapdump; build_contract; build_replay; build_greybox_view; build_texdump ;;
    *)       echo "usage: $(basename "$0") [game|mapdump|contract|replay|greybox-view|texdump|all]"; exit 2 ;;
esac
