#!/usr/bin/env python3
"""Finish the prepared revolver GLB: give the grip panels a walnut stock.

The CC0 source is a stainless revolver whose grip panels are the same flat grey
as its steel, so in the hand the whole gun read as one grey object. A stainless
revolver with checkered walnut stocks is the classic combination; this recolours
only the grip texels and leaves the steel, the normals and the roughness alone.

Which texels are the grip: the source's own metallic map says non-metal (blue
channel low) *and* its normal map says the texel is part of a UV island rather
than unused background (the background is (128,128,128), not a normal). The
screw medallion in the middle of the panel is metal, so it stays steel.

The stored albedo keeps its shading: each texel's grey is used as a multiplier
on the wood, so the stippled panel, its border and the baked occlusion survive.

Idempotent: the GLB is marked (asset.extras.gripFinish) and a second run does
nothing. tools/import-revolver.py runs this after writing a fresh GLB, so a
reimport from the original archive reproduces the finished asset.

Needs only Pillow. Usage: tools/revolver-finish.py [path/to/revolver.glb]
"""
import io
import json
import math
import struct
import sys
from pathlib import Path

from PIL import Image

MARK = "walnut-v1"


def read_glb(data):
    magic, version, _ = struct.unpack("<III", data[:12])
    assert magic == 0x46546C67 and version == 2, "not a glTF 2 binary"
    jlen, jtype = struct.unpack("<II", data[12:20])
    assert jtype == 0x4E4F534A
    doc = json.loads(data[20:20 + jlen])
    off = 20 + jlen
    blen, btype = struct.unpack("<II", data[off:off + 8])
    assert btype == 0x004E4942
    return doc, bytearray(data[off + 8:off + 8 + blen])


def write_glb(doc, views):
    """Lay every bufferView out again in order, 4-byte aligned."""
    binary = bytearray()
    for i, chunk in enumerate(views):
        while len(binary) % 4:
            binary.append(0)
        doc["bufferViews"][i]["byteOffset"] = len(binary)
        doc["bufferViews"][i]["byteLength"] = len(chunk)
        binary.extend(chunk)
    binary += b"\0" * ((-len(binary)) % 4)
    doc["buffers"] = [{"byteLength": len(binary)}]
    enc = json.dumps(doc, separators=(",", ":")).encode()
    enc += b" " * ((-len(enc)) % 4)
    return (struct.pack("<III", 0x46546C67, 2, 28 + len(enc) + len(binary))
            + struct.pack("<II", len(enc), 0x4E4F534A) + enc
            + struct.pack("<II", len(binary), 0x004E4942) + bytes(binary))


def lattice(ix, iy, seed):
    h = (ix * 374761393 + iy * 668265263 + seed * 2246822519) & 0xFFFFFFFF
    h = ((h ^ (h >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0


def vnoise(x, y, seed):
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
    a, b = lattice(ix, iy, seed), lattice(ix + 1, iy, seed)
    c, d = lattice(ix, iy + 1, seed), lattice(ix + 1, iy + 1, seed)
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy


def walnut(x, y):
    """Walnut at a texel: fine straight grain along the panel (texture y), its
    figure wandering across it, darker latewood lines and open pores."""
    warp = vnoise(x * 0.02, y * 0.005, 11) * 5.0 + vnoise(x * 0.09, y * 0.025, 12) * 1.2
    ring = 0.5 + 0.5 * math.sin(x * 0.85 + warp * 2.2)
    late = ring ** 4.0
    band = vnoise(x * 0.05, y * 0.01, 15)          # wider, softer figure
    tone = 0.84 + 0.30 * vnoise(x * 0.012, y * 0.004, 13)
    pore = 0.84 if lattice(x, y // 3, 14) < 0.05 else 1.0
    light, dark = (90, 58, 38), (40, 24, 16)
    t = 0.25 + 0.30 * late + 0.30 * band
    return [(l + (d - l) * t) * tone * pore for l, d in zip(light, dark)]


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "assets/models/revolver.glb"
    doc, binary = read_glb(path.read_bytes())
    if doc.get("asset", {}).get("extras", {}).get("gripFinish") == MARK:
        print(f"{path}: grip already finished ({MARK})")
        return
    views = [bytes(binary[v.get("byteOffset", 0):v.get("byteOffset", 0) + v["byteLength"]]) for v in doc["bufferViews"]]
    gun = next(m for m in doc["materials"] if m["name"] == "gun")

    def image_of(texture_ref):
        src = doc["textures"][texture_ref["index"]]["source"]
        return src, doc["images"][src]

    a_idx, a_img = image_of(gun["pbrMetallicRoughness"]["baseColorTexture"])
    _, mr_img = image_of(gun["pbrMetallicRoughness"]["metallicRoughnessTexture"])
    _, n_img = image_of(gun["normalTexture"])
    load = lambda img: Image.open(io.BytesIO(views[img["bufferView"]])).convert("RGB")
    albedo, mr, normal = load(a_img), load(mr_img), load(n_img)
    pa, pm, pn = albedo.load(), mr.load(), normal.load()
    w, h = albedo.size
    # The grip's typical stored grey: the texel's own grey over this is the
    # shading multiplier that carries the stippling and occlusion onto the wood.
    greys = [sum(pa[x, y]) / 3 for y in range(h) for x in range(w)
             if pm[x, y][2] < 128 and pn[x, y][2] > 180]
    greys.sort()
    ref = greys[len(greys) // 2]
    changed = 0
    for y in range(h):
        for x in range(w):
            if pm[x, y][2] >= 128 or pn[x, y][2] <= 180:
                continue
            k = min(max((sum(pa[x, y]) / 3) / ref, 0.35), 1.2)
            pa[x, y] = tuple(max(0, min(255, round(c * k))) for c in walnut(x, y))
            changed += 1
    out = io.BytesIO()
    albedo.save(out, format="JPEG", quality=92, optimize=True)
    views[a_img["bufferView"]] = out.getvalue()
    doc.setdefault("asset", {}).setdefault("extras", {})["gripFinish"] = MARK
    path.write_bytes(write_glb(doc, views))
    print(f"{path}: {changed} grip texels finished as walnut (reference grey {ref:.1f})")


if __name__ == "__main__":
    main()
