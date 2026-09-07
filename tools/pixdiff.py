#!/usr/bin/env python3
"""Look at screenshots numerically, because "it looks better" has been wrong here.

  pixdiff.py crop  IN OUT X Y W H [SCALE]   zoom in on a detail
  pixdiff.py diff  A B                      mean luma + where two frames differ

`diff` reports which eighth of the screen the differences land in, which is how
you tell "I changed the HUD" from "I changed the world pass".
"""
import sys
from PIL import Image


def crop(a):
    src, dst = a[0], a[1]
    x, y, w, h = map(int, a[2:6])
    s = float(a[6]) if len(a) > 6 else 1.0
    im = Image.open(src).convert("RGB").crop((x, y, x + w, y + h))
    if s != 1.0:
        im = im.resize((int(w * s), int(h * s)), Image.NEAREST)
    im.save(dst)
    print(dst, im.size)


def diff(a):
    A = Image.open(a[0]).convert("RGB")
    B = Image.open(a[1]).convert("RGB")
    if A.size != B.size:
        sys.exit(f"size mismatch {A.size} vs {B.size}")
    pa, pb = list(A.getdata()), list(B.getdata())
    n = len(pa)
    print(f"mean luma: {sum(map(sum, pa))/(3*n):.2f} -> {sum(map(sum, pb))/(3*n):.2f}")
    W, H = A.size
    rows, cols, tot = [0] * 8, [0] * 8, 0
    for i, (p, q) in enumerate(zip(pa, pb)):
        if max(abs(u - v) for u, v in zip(p, q)) > 16:
            tot += 1
            x, y = i % W, i // W
            cols[min(7, x * 8 // W)] += 1
            rows[min(7, y * 8 // H)] += 1
    print(f"pixels differing >16: {tot}/{n} ({100*tot/n:.3f}%)")
    print("  by row band (top->bottom):", rows)
    print("  by col band (left->right):", cols)


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in ("crop", "diff"):
        sys.exit(__doc__)
    (crop if sys.argv[1] == "crop" else diff)(sys.argv[2:])
