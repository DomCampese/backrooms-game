#!/bin/bash
# Reconstruct a raylib build environment where the system has no raylib.
#
# There is no raylib package in the Claude Code web sandbox, but the Python
# wheel ships a complete raylib shared object plus cffi-preprocessed headers,
# and C++ links against both perfectly well. This turns the prose recipe in
# CLAUDE.md into one command. Idempotent; safe to re-run.
#
# Writes:  rlshim/{raylib,raymath,rlgl}.h   and   .rlwheel/  (both gitignored)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
WHEEL=$ROOT/.rlwheel

if [ ! -d "$WHEEL/raylib" ]; then
    echo "==> fetching the raylib wheel"
    rm -rf "$WHEEL"; mkdir -p "$WHEEL"
    pip3 download raylib -d "$WHEEL" --no-deps -q
    (cd "$WHEEL" && unzip -o -q raylib-*.whl)
fi

echo "==> building rlshim/ from the wheel's cffi headers"
mkdir -p "$ROOT/rlshim"
for h in raylib raymath rlgl; do
    src=$WHEEL/raylib/$h.h.modified
    [ -f "$src" ] || { echo "missing $src"; exit 1; }
    {
        echo '#pragma once'
        # cffi strips #defines, so put back the ones the game uses
        cat <<'DEFS'
#ifndef PI
#define PI 3.14159265358979323846f
#endif
#ifndef DEG2RAD
#define DEG2RAD (PI/180.0f)
#endif
#ifndef RAD2DEG
#define RAD2DEG (180.0f/PI)
#endif
#ifndef MATERIAL_MAP_DIFFUSE
#define MATERIAL_MAP_DIFFUSE MATERIAL_MAP_ALBEDO
#endif
#ifndef MATERIAL_MAP_SPECULAR
#define MATERIAL_MAP_SPECULAR MATERIAL_MAP_METALNESS
#endif
#ifndef SHADER_LOC_MAP_DIFFUSE
#define SHADER_LOC_MAP_DIFFUSE SHADER_LOC_MAP_ALBEDO
#endif
DEFS
        echo 'extern "C" {'
        cat "$src"
        # raylib.h.modified's last line is a declaration with a trailing "//"
        # comment and NO trailing newline, so appending "}" on the same line
        # buries it inside that comment. The extern "C" then never closes and
        # the build dies hundreds of lines away in <initializer_list> with
        # "template with C linkage". A bare echo first guarantees the newline.
        echo ''
        echo '}'
    } > "$ROOT/rlshim/$h.h"
done

# raymath declares its functions inline; they will not resolve against the .so
sed -i 's/^inline //; s/^static inline /static /' "$ROOT/rlshim/raymath.h"

# the named Color constants are macros too, and need Color to exist first
cat >> "$ROOT/rlshim/raylib.h" <<'COLORS'
#define CLITERAL(type) (type)
#define LIGHTGRAY  CLITERAL(Color){ 200, 200, 200, 255 }
#define GRAY       CLITERAL(Color){ 130, 130, 130, 255 }
#define DARKGRAY   CLITERAL(Color){ 80, 80, 80, 255 }
#define YELLOW     CLITERAL(Color){ 253, 249, 0, 255 }
#define GOLD       CLITERAL(Color){ 255, 203, 0, 255 }
#define ORANGE     CLITERAL(Color){ 255, 161, 0, 255 }
#define PINK       CLITERAL(Color){ 255, 109, 194, 255 }
#define RED        CLITERAL(Color){ 230, 41, 55, 255 }
#define MAROON     CLITERAL(Color){ 190, 33, 55, 255 }
#define GREEN      CLITERAL(Color){ 0, 228, 48, 255 }
#define LIME       CLITERAL(Color){ 0, 158, 47, 255 }
#define DARKGREEN  CLITERAL(Color){ 0, 117, 44, 255 }
#define SKYBLUE    CLITERAL(Color){ 102, 191, 255, 255 }
#define BLUE       CLITERAL(Color){ 0, 121, 241, 255 }
#define DARKBLUE   CLITERAL(Color){ 0, 82, 172, 255 }
#define PURPLE     CLITERAL(Color){ 200, 122, 255, 255 }
#define VIOLET     CLITERAL(Color){ 135, 60, 190, 255 }
#define DARKPURPLE CLITERAL(Color){ 112, 31, 126, 255 }
#define BEIGE      CLITERAL(Color){ 211, 176, 131, 255 }
#define BROWN      CLITERAL(Color){ 127, 106, 79, 255 }
#define DARKBROWN  CLITERAL(Color){ 76, 63, 47, 255 }
#define WHITE      CLITERAL(Color){ 255, 255, 255, 255 }
#define BLACK      CLITERAL(Color){ 0, 0, 0, 255 }
#define BLANK      CLITERAL(Color){ 0, 0, 0, 0 }
#define MAGENTA    CLITERAL(Color){ 255, 0, 255, 255 }
#define RAYWHITE   CLITERAL(Color){ 245, 245, 245, 255 }
COLORS

echo "==> done. now run: tools/sandbox-build.sh"
