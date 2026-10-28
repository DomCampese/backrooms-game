#!/bin/bash
# The camcorder's tape look in both languages, drawn on one GPU and compared:
# TAPE_FS (web/src/shaders.cpp) against BackroomsTape.ush, and against
# M_BackroomsTape's Custom node (TAPE_CODE in backrooms_world.py) round it.
# DXC compiles each to SPIR-V (and to DXIL, to prove it compiles for D3D too)
# and spirv-cross turns that back into GLSL. Run after changing any of them.
#
# Needs DXC (DXC=path, default dxc on PATH; the Linux release tarball from
# github.com/microsoft/DirectXShaderCompiler works with its lib/ on
# LD_LIBRARY_PATH), spirv-cross (apt install spirv-cross), the sandbox's
# raylib (tools/sandbox-setup.sh) and a display (Xvfb, as tools/shot.sh starts).
set -euo pipefail
cd "$(dirname "$0")/.."
DXC=${DXC:-dxc}
OUT=${TMPDIR:-/tmp}/tape-check
mkdir -p "$OUT"

# The Custom node's code, pasted into its stand-in material.
python3 - "$OUT/glue.hlsl" "$PWD" <<'PY'
import re, sys
out, repo = sys.argv[1], sys.argv[2]
world = open(repo + "/unreal/Plugins/Backrooms/Content/Python/backrooms_world.py").read()
code = re.search(r'TAPE_CODE = """(.*?)"""', world, re.S).group(1)
glue = open(repo + "/tools/tape-check-glue.hlsl").read()
glue = glue.replace("// TAPE_CODE", code).replace('"../unreal/', '"' + repo + "/unreal/")
open(out, "w").write(glue)
PY
for shader in tools/tape-check.hlsl "$OUT/glue.hlsl"; do
    name=$(basename "$shader" .hlsl)
    "$DXC" -T ps_6_0 -E main -Fo "$OUT/$name.dxil" "$shader"
    "$DXC" -T ps_6_0 -E main -spirv -Fo "$OUT/$name.spv" "$shader"
    spirv-cross "$OUT/$name.spv" --version 330 --no-es --glsl-emit-ubo-as-plain-uniforms --output "$OUT/$name.glsl"
done

eval "$(sed -n '/^SO=/,/^LINK=/p' tools/sandbox-build.sh)"
c++ "${FLAGS[@]}" -Iweb/src -o "$OUT/tape-check" tools/tape-check.cpp web/src/shaders.cpp "${LINK[@]}"

if [ -z "${DISPLAY:-}" ]; then
    export DISPLAY=:99
    pgrep -x Xvfb >/dev/null || { Xvfb :99 -screen 0 1600x1000x24 >/dev/null 2>&1 & sleep 1; }
fi
"$OUT/tape-check" "$OUT/tape-check.glsl" "$OUT/glue.glsl" docs/screenshot.png
