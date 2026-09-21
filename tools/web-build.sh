#!/bin/bash
# Build the WebAssembly release into web/dist. Needs an active emsdk in PATH
# (source emsdk_env.sh first); everything else it fetches or builds itself.
#
#   tools/web-build.sh            # build into web/dist
#   RAYLIB_SRC=/path tools/web-build.sh   # reuse a raylib checkout you already have
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
OUT="$ROOT/web/dist"

# Pinned so a release's bytes are reproducible, and so the bundled zlib notice
# in LICENSES/ describes the raylib that is actually linked into the .wasm.
# Bump this and LICENSES/raylib-*.txt together — the notice ships in the build.
RAYLIB_TAG=5.5
RAYLIB_SRC="${RAYLIB_SRC:-$ROOT/.raylib-web/src}"

command -v em++ >/dev/null || {
    echo "emcc not found. Install emsdk and 'source emsdk_env.sh' first." >&2; exit 1; }

# raylib for the browser, built as WebGL 2 rather than the Makefile's default
# WebGL 1. That is not a preference: the world shader uses dFdx and texelFetch,
# which are core in GLSL ES 3.00 and simply absent from ES 2.0, so an ES2 build
# fails to compile the shader — and a failed compile in raylib does not crash,
# it silently falls back to the default shader and renders a black frame faster
# than the real one (see AGENTS.md). There is no partial-credit version of this.
if [ ! -f "$RAYLIB_SRC/libraylib.a" ]; then
    echo "building raylib $RAYLIB_TAG for web..."
    rm -rf "$ROOT/.raylib-web"
    git clone --depth 1 --branch "$RAYLIB_TAG" https://github.com/raysan5/raylib.git "$ROOT/.raylib-web" >/dev/null 2>&1
    make -C "$ROOT/.raylib-web/src" PLATFORM=PLATFORM_WEB GRAPHICS=GRAPHICS_API_OPENGL_ES3 -j"$(nproc)" >/dev/null
    RAYLIB_SRC="$ROOT/.raylib-web/src"
fi

python3 tools/embed-materials.py

mkdir -p "$OUT"
rm -f "$OUT/index.html" "$OUT/index.js" "$OUT/index.wasm"

em++ -std=c++17 -O2 -DPLATFORM_WEB \
    -Wall -Wno-missing-field-initializers \
    -I"$RAYLIB_SRC" \
    src/*.cpp "$RAYLIB_SRC/libraylib.a" \
    -o "$OUT/index.html" \
    --shell-file web/shell.html \
    -sUSE_GLFW=3 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=268435456 \
    -sSTACK_SIZE=4194304 \
    -sFORCE_FILESYSTEM=1 -lidbfs.js \
    `# HEAPF32: miniaudio's ScriptProcessor callback reaches for Module.HEAPF32.buffer,` \
    `# and emscripten stopped hanging the heap views off Module by default. Without` \
    `# it every audio callback throws "Cannot read properties of undefined", ~40 times` \
    `# a second, and the game renders perfectly in total silence.` \
    -sEXPORTED_RUNTIME_METHODS=FS,ENV,addRunDependency,removeRunDependency,callMain,HEAPF32 \
    -sASSERTIONS=0

cp -f LICENSE CREDITS.md "$OUT/" 2>/dev/null || true
mkdir -p "$OUT/LICENSES" && cp -f LICENSES/*.txt "$OUT/LICENSES/" 2>/dev/null || true

# GitHub Pages serves this directory as-is; .nojekyll stops Pages' Jekyll pass
# from dropping files it considers special. Nothing here starts with an
# underscore today, but a future emscripten output could.
touch "$OUT/.nojekyll"

echo "built $OUT/index.{html,js,wasm}"
ls -la "$OUT"
