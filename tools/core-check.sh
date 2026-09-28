#!/bin/bash
# Compile the engine-independent layers alone, the way an Unreal module would:
# C++17, no exceptions, no RTTI, and nothing on the include path but src/.
# Fails if a file does not compile that way, or if it includes anything outside
# its allowed layers (docs/migration.md): core may include core; sim and port
# may include core and themselves. A raylib, GL or platform header anywhere in the chain fails too,
# even where one happens to be installed system-wide.
#
#   tools/core-check.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
CXX=${CXX:-c++}
# -ffp-contract=off as every real build of core (src/core/fp_strict.h).
FLAGS=(-std=c++17 -fno-exceptions -fno-rtti -ffp-contract=off -Wall -Wno-missing-field-initializers -fsyntax-only -Isrc)
FORBIDDEN='(^|/)(raylib|rlgl|raymath|rcamera|glfw3?|gl|glad|miniaudio)\.h$|/GL/|/GLES[0-9]*/'
fail=0

# check LAYER DIR ALLOWED_DIR...
check() {
    local layer=$1 dir=$2; shift 2
    local allowed=("$@") f inc ok a
    [ -d "$dir" ] || { echo "skip $layer: no $dir"; return; }
    shopt -s nullglob
    local files=("$dir"/*.h "$dir"/*.cpp)
    shopt -u nullglob
    for f in "${files[@]}"; do
        local lang=c++; [[ $f == *.h ]] && lang=c++-header
        local out
        if ! out=$("$CXX" "${FLAGS[@]}" -x "$lang" -H "$f" 2>&1); then
            echo "FAIL $f: does not compile alone"
            printf '%s\n' "$out" | grep -v '^\.' | head -20
            fail=1; continue
        fi
        # -H lists every header opened, one per line, prefixed by dots
        local bad=0
        while read -r inc; do
            inc=$(realpath -m "$inc")
            if [[ $inc =~ $FORBIDDEN ]]; then
                echo "FAIL $f: reaches $inc"; bad=1; continue
            fi
            [[ $inc == "$ROOT"/* ]] || continue          # the standard library
            ok=0
            for a in "${allowed[@]}"; do [[ $inc == "$ROOT/$a"/* ]] && ok=1; done
            [ $ok = 1 ] || { echo "FAIL $f: includes ${inc#"$ROOT"/}, outside ${allowed[*]}"; bad=1; }
        done < <(printf '%s\n' "$out" | sed -n 's/^\.\+ //p')
        if [ $bad = 1 ]; then fail=1; else echo "ok   $f"; fi
    done
}

check core src/core src/core
check sim src/sim src/core src/sim
check port src/port src/core src/port

[ $fail = 0 ] && echo "core-check: passed" || { echo "core-check: FAILED"; exit 1; }
