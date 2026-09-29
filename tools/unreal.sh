#!/bin/bash
# Build, open, play and test the Unreal project (unreal/) from the terminal.
#
#   tools/unreal.sh build     compile the editor modules (after every pull)
#   tools/unreal.sh open      build, then open the project in the editor
#   tools/unreal.sh play      build, then run the game in its own window
#   tools/unreal.sh test      build, then run the Backrooms automation tests
#   tools/unreal.sh import    import assets/models into the project again
#   tools/unreal.sh log       the "Backrooms:" lines from the last run's log
#
# `make unreal`, `make unreal-open`, `make unreal-play` and `make unreal-test`
# call the same. UE_ROOT is the engine (default: the Launcher's 5.8 install).
# play takes LEVEL and SEED, as the raylib build's BACKROOMS_LEVEL and
# BACKROOMS_SEED: `LEVEL=1 SEED=42 tools/unreal.sh play`.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
PROJECT="$ROOT/unreal/BackroomsGame.uproject"
UE_ROOT=${UE_ROOT:-"/Users/Shared/Epic Games/UE_5.8"}
EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
EDITOR_CMD="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor-Cmd"
LOG="$ROOT/unreal/Saved/Logs/BackroomsGame.log"

die() { echo "unreal.sh: $*" >&2; exit 1; }

[[ -d "$UE_ROOT/Engine" ]] || die "no engine at $UE_ROOT; set UE_ROOT to the UE_5.8 folder"

build() {
    # The editor holds the plugin's binaries open, and a build under it either
    # fails to write them or is replaced by the running copy.
    if pgrep -x UnrealEditor >/dev/null; then
        die "quit the Unreal editor first; it locks the plugin while it is open"
    fi
    # Files the repository does not have, under folders Unreal compiles: a class
    # made from the editor's New C++ Class menu stopped a build here once
    # (TestHyd.h, "Expected a GENERATED_BODY()"), and read as the port's fault.
    local stray
    stray=$(cd "$ROOT" && git ls-files --others --exclude-standard -- unreal/Source unreal/Plugins/*/Source)
    if [[ -n "$stray" ]]; then
        echo "unreal.sh: these are not in the repository, and Unreal will compile them:" >&2
        echo "$stray" | sed 's/^/    /' >&2
        echo "unreal.sh: delete them if you did not mean to add them" >&2
    fi
    "$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" BackroomsGameEditor Mac Development \
        -Project="$PROJECT" -WaitMutex
}

case "${1:-build}" in
build)
    build
    ;;
open)
    build
    open -a "$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app" --args "$PROJECT"
    ;;
play)
    build
    # The game cannot import; the editor does it when it opens the project.
    if ! ls "$ROOT/unreal/Content/Backrooms/Revolver/"*.uasset >/dev/null 2>&1; then
        echo "unreal.sh: the revolver is not imported yet; run 'make unreal-open' once and let the editor import it" >&2
    fi
    MAP="/Engine/Maps/Entry?level=${LEVEL:-0}?seed=${SEED:-1337}"
    "$EDITOR" "$PROJECT" "$MAP" -game -windowed -ResX=1440 -ResY=850 -log || true
    echo "--- Backrooms lines from $LOG:"
    grep "Backrooms:" "$LOG" || echo "(none)"
    ;;
test)
    build
    "$EDITOR_CMD" "$PROJECT" -ExecCmds="Automation RunTests Backrooms; Quit" \
        -unattended -nullrhi -nosound -log || true
    echo "--- results:"
    grep -E "Test Completed|Result=\{" "$LOG" || echo "(no results in $LOG)"
    ;;
import)
    "$EDITOR_CMD" "$PROJECT" -run=pythonscript \
        -script="import backrooms_import; backrooms_import.import_revolver()" -unattended -log
    grep "Backrooms:" "$LOG" || echo "(no Backrooms lines in $LOG)"
    ;;
log)
    grep "Backrooms:" "$LOG" || echo "(no Backrooms lines in $LOG)"
    ;;
*)
    sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
