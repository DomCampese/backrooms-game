#!/bin/bash
# What can be checked of the Unreal project (unreal/) without Unreal:
#   - every .cpp of core, sim and port, and the contract library, has a wrapper
#     in the plugin's Private/Shared that compiles it into the module;
#   - each wrapper compiles alone the way the module compiles it: no PCH, no
#     exceptions, no RTTI, C++20, warnings as errors;
#   - the wrappers linked together, with clang and without -ffp-contract=off
#     (the pragmas in src/core/fp_strict.h must hold contraction off alone, as
#     they must under Unreal's flags), pass the contract and replay every trace;
#   - the .uproject and .uplugin parse.
# Unreal's own classes (Public/, Private/*.cpp, Private/Tests) need the engine
# to compile and are not checked here.
#
#   tools/unreal-check.sh
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
CXX=${CXX:-clang++}
SHARED=unreal/Plugins/Backrooms/Source/Backrooms/Private/Shared
FLAGS=(-std=c++20 -fno-exceptions -fno-rtti -Wall -Wextra -Werror -Wshadow -Wundef
       -Wno-unused-parameter -Wno-missing-field-initializers -Isrc -Itools)
# FMA available, so contraction is possible and only the pragmas prevent it. On
# arm64 it always is.
[[ $(uname -m) == x86_64 ]] && FLAGS+=(-mfma)
fail=0

for src in src/core/*.cpp src/sim/*.cpp src/port/*.cpp tools/contract_lib.cpp; do
    inc=${src#src/}; inc=${inc#tools/}
    if ! grep -lqx "#include \"$inc\"" "$SHARED"/*.cpp; then
        echo "FAIL $src: no wrapper in $SHARED includes \"$inc\""; fail=1
    fi
done

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
for w in "$SHARED"/*.cpp; do
    if "$CXX" "${FLAGS[@]}" -O2 -c "$w" -o "$OUT/$(basename "$w" .cpp).o" 2>"$OUT/err"; then
        echo "ok   $w"
    else
        echo "FAIL $w"; head -20 "$OUT/err"; fail=1
    fi
done

if [ $fail = 0 ]; then
    cat > "$OUT/main.cpp" <<'CPP'
#include "contract_lib.h"
#include "sim/trace.h"
#include <cstdio>
int main(int argc, char **argv) {
    std::string report;
    int bad = contract::compare("tests/golden", contract::produce(), report);
    fputs(report.c_str(), stdout);
    for (int i = 1; i < argc; i++) {
        ReplayResult r = replayTrace(argv[i]);
        printf("%s %s: %s\n", r.ok ? "ok  " : "FAIL", argv[i], r.report.c_str());
        bad += !r.ok;
    }
    return bad ? 1 : 0;
}
CPP
    "$CXX" "${FLAGS[@]}" -O2 "$OUT/main.cpp" "$OUT"/*.o -o "$OUT/module"
    shopt -s nullglob
    traces=(tests/traces/*.trace)
    shopt -u nullglob
    "$OUT/module" "${traces[@]}" || fail=1
fi

for j in unreal/*.uproject unreal/Plugins/*/*.uplugin; do
    if python3 -c 'import json,sys; json.load(open(sys.argv[1]))' "$j"; then echo "ok   $j"
    else echo "FAIL $j: not JSON"; fail=1; fi
done

[ $fail = 0 ] && echo "unreal-check: passed" || { echo "unreal-check: FAILED"; exit 1; }
