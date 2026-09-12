#!/usr/bin/env python3
"""Interleave builds, retain logs, report frame distributions and total runtime."""
import os
from pathlib import Path
import re
import subprocess
import sys
import time

if len(sys.argv) < 3:
    sys.exit("usage: tools/bench.sh <binA> <binB> [level=0] [runs=3]")
binaries = [str(Path(p).resolve()) for p in sys.argv[1:3]]
level = int(sys.argv[3]) if len(sys.argv) > 3 else 0
runs = int(sys.argv[4]) if len(sys.argv) > 4 else 3
if runs < 1 or not 0 <= level <= 4:
    sys.exit("runs must be positive and level must be 0..4")
out = Path(__file__).resolve().parent.parent / "shots" / "bench" / f"level-{level}"
out.mkdir(parents=True, exist_ok=True)
results = [[], []]
for run in range(runs):
    # Alternate which build goes first as well as interleaving the builds.
    for index in ([0, 1] if run % 2 == 0 else [1, 0]):
        name = f"{'AB'[index]}-{run+1}"
        env = dict(os.environ, BACKROOMS_SEED="1337", BACKROOMS_LEVEL=str(level),
                   BACKROOMS_POS=os.environ.get("BACKROOMS_POS", "15,15,0.8"),
                   BACKROOMS_BENCH="1", BACKROOMS_TIME="4", BACKROOMS_SHOT=name+".png",
                   BACKROOMS_SHOTFRAME=os.environ.get("BACKROOMS_SHOTFRAME", "240"))
        start = time.perf_counter()
        p = subprocess.run([binaries[index]], cwd=out, env=env, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True, timeout=180)
        wall = (time.perf_counter()-start)*1000
        (out / (name+".log")).write_text(p.stdout)
        if p.returncode or re.search(r"SHADER: .*?(failed|error)|ERROR:", p.stdout, re.I):
            sys.exit(p.stdout)
        if not (out / (name+".png")).is_file():
            sys.exit(f"{name}: screenshot missing; inspect {out}")
        m = re.search(r"BENCH frames=(\d+) mean_ms=([\d.]+) median_ms=([\d.]+) p95_ms=([\d.]+)", p.stdout)
        frame = tuple(map(float, m.groups()[1:])) if m else None
        results[index].append((wall, frame))
        print(f"{name}: total_ms={wall:.1f}" + (f" mean/median/p95_ms={frame}" if frame else " (legacy build)"), flush=True)
for index, label in enumerate("AB"):
    print(f"{label}: {binaries[index]}")
if all(r[1] is not None for group in results for r in group):
    best = [min(group, key=lambda r: r[1][0])[1] for group in results]
    print(f"level {level}, best-of-{runs}: A mean={best[0][0]:.3f}ms, B mean={best[1][0]:.3f}ms, B/A={best[1][0]/best[0][0]:.3f}")
else:
    best = [min(r[0] for r in group) for group in results]
    print(f"Legacy comparison: total runtime only (includes startup, screenshot, vsync). B/A={best[1]/best[0]:.3f}")
print(f"Logs: {out}")
