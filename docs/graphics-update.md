# Graphics update validation

Built with native Homebrew raylib and C++17 on macOS. The initial five-level and
menu sweep passed shader checks. The native regression harness passed sprint
recovery, crouch/stationary gating, fresh-run stamina reset, and battery retention,
and produced nine visual captures. Revolver and wall-clearance images were inspected.
A final sweep and benchmark run was started after the furniture/detail changes;
its complete results should be checked in shots/ before merge.

An earlier interleaved Level 0 benchmark measured best-of-three mean frame time
8.106 ms baseline versus 7.870 ms updated. This predates the final furniture pass
and is not a final performance guarantee. The baseline was latest main with only
matching capture/timing instrumentation added. Logs are under shots/bench.

Known limits: transparent sorting is per chunk, not per triangle. Material relief
on non-ceramic surfaces is derived from albedo rather than authored height data.
Only one flare illuminates the room at a time. The live native UI attempt lost the
app before input could be sent; a full run-around playtest remains outstanding.
Further realism work should prioritize object bevels, richer roughness/material
variation, and navigation landmarks, then assess them while moving in-game.

The stack uses a programming library rather than an editor-driven engine. Its
particularly unusual constraint is synthesizing all meshes, textures and sound
in code. This provides control and compact distribution, but makes visual tooling
and asset refinement part of this repository's own engineering workload.
