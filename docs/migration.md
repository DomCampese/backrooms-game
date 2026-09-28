# Engine seams

The game runs on raylib today. These seams separate what the game *is* (the
building, the rules) from how it is drawn and heard, so that a port to another
engine keeps the first and replaces the second. The Unreal Engine 5 port plan is
in docs/unreal-handoff.md.

## Layers

| layer | directory | may depend on | contains |
|---|---|---|---|
| core | `src/core/` | the C++17 standard library only | math types, hashes and noise, per-level rules, chunk generation, storeys and features, collision, line of sight, pathfinding, the light-occlusion grid, the layout description |
| sim | `src/sim/` | core | game state and rules: player, hunter, dogs, health, pickups, exits, blackouts, storey changes. Takes an input frame and a clock, emits events |
| platform | `src/` | everything | the raylib backend: window, input, mesher, textures, shaders, rendering, audio, the revolver model |

Rules:

- core and sim do not include `raylib.h`, `rlgl.h` or any GL, audio or window
  header. `tools/core-check.sh` compiles them alone and fails if one does.
- core and sim never read the wall clock or a global random source. Time comes
  in as an argument; randomness comes from seeded `Rng`.
- Nothing in core or sim knows about textures, atlases, UVs or meshes. Where the
  platform needs to know what to build, core describes it (`ChunkLayout`) and
  the platform decides how it looks.
- core and sim avoid exceptions and RTTI, so they compile under Unreal's default
  module settings.

## Proving a seam change

Seam work is meant to change nothing the player can see or do.

| proof | command | pass |
|---|---|---|
| generator unchanged | `mapdump` runs below, `diff` against the previous build | identical text |
| core answers unchanged | `./contract --check` (Contract tests, below) | exit 0 |
| frames unchanged | `tools/proof-shots.sh DIR` before and after, then `--diff` | at the noise floor of one binary captured twice |
| behaviour unchanged | the regression harness (AGENTS.md) | exit 0 |

The mapdump set:

```bash
for lv in 0 1 2 3 4; do for v in 0 1; do
  ./mapdump --level $lv --seed 1337 --visit $v --cells 65 --plan -16 -16 32 32
done; done
./mapdump --level 0 --seed 1337 --visit 1 --list-stairs --no-plan
```

## Contract tests

`tools/contract.cpp` links src/core alone and compares its answers with the
text files in `tests/golden`. A port running the same core must reproduce every
line. It runs in under 0.1 s.

```bash
tools/sandbox-build.sh contract    # or: make contract-check
./contract --check                 # compare with tests/golden; exit 1 on any difference
./contract --write                 # regenerate tests/golden
```

| file | holds |
|---|---|
| `chunks.txt` | 121 chunks, one line each: a hash of the whole chunk and one per `ChunkData` field. Levels 0-4, visits 0 and 1, seed 1337, the 3x3 chunks round the origin and chunk (40,-37); on Level 0 also both storeys of every vertical feature among them and of the first feature of each kind near the origin |
| `chunks_full.txt` | 18 of those chunks field by field, one row per z: the origin chunk of every level and visit, and the Level 0 visit 1 feature chunks |
| `queries.txt` | `gatherCellAABBs`, `collideCircle`, `groundAt` (through a hole into the storey below, on flights, on a vending machine), `stairY` up each flight kind, `lineOfSight`, `canStep`, `pathStep`, `findOpenSpot`, `ceilY`, `buildOccupancy` over a 48-cell window, `vendFootprint` for each turn and flag. Probes are doors, a locked door, rails, a pillar, holes and vending machines found in the generated world, and seeded samples, each printed with the coordinates of the call |
| `mutations.txt` | `shiftEdge` and `unlockEdge` on chosen edges: the walls read back through `wallNVal`/`wallWVal`, the `staleChunks` entries appended, and a repeat that appends nothing |

A failure names the file, line, chunk and field: `FAIL chunks.txt:4 [chunk L0
v0 s0 (0,-1)] fields elev`, or the field and row of a full dump. Floats are
printed with `%.9g`, which round-trips a float, so the comparison is exact.

Regenerate the goldens only with a deliberate change to what the generator or a
query produces, and say so in the commit message with what moved. A refactor,
a build change or a port must pass them unchanged. Not covered: the order of
`staleChunks` from `unloadFar`/`unloadAll`, which follows `unordered_map`
iteration and differs between standard libraries.

## Floating point

Core's results must not depend on the compiler. Two mechanisms, both needed:

| mechanism | where | covers |
|---|---|---|
| `-ffp-contract=off` | Makefile, `tools/sandbox-build.sh`, `tools/web-build.sh`, `tools/core-check.sh` | gcc, clang, em++ |
| `src/core/fp_strict.h`, included first by every core .cpp | the source | clang (`#pragma clang fp contract(off)`), MSVC (`float_control(precise, on)` then `fp_contract(off)`), others (`STDC FP_CONTRACT OFF`); GCC ignores pragmas and needs the flag |

Rule: no fused multiply-adds in core. A fused multiply-add rounds once where
the source rounds twice, and the generator thresholds noise (`fbm2(...) >
0.60f`), so contraction can flip a comparison and generate a different maze.
clang contracts within an expression by default (the Mac arm64 build); gcc
contracts across expressions in C++ whenever FMA instructions are enabled.
Measured on x86-64 with `-mfma`: world.cpp compiles to 28 FMA instructions
under gcc and 47 under clang, 0 with the flag, and 0 under clang with the header
alone. Without either, clang changed one contract answer (`stairY`'s ramp,
2.78999996 to 2.7900002); no chunk moved at seed 1337.

Not covered by the pragmas: clang's `-ffast-math` and `-ffp-contract=fast`
override them (7 answers change, including `stairY` returning a number where it
returns NaN off a flight), and `/fp:fast` on MSVC reassociates as well. The MSVC
pragmas are untested: there is no MSVC here. An
Unreal module compiling core must not use fast math; check the toolchain's
defaults and run the contract tests under it.

libm: generation uses no transcendental function. `sqrtf` (collision, sight)
is correctly rounded by IEEE 754 on every platform, and `floorf`, `ceilf` and
`fabsf` are exact. The one dependency is `vendFootprint` (world.cpp,
`float yaw = (rotByte & 3) * PROP_TURN, ca = cosf(yaw), sa = sinf(yaw);`),
which feeds the vending machine's collision box and the mesher. `PROP_TURN` is
1.5708, not pi/2, so the results are tiny non-zero values (glibc: cos 1.5708 =
-0x1.e5ddeap-19) whose last bit another libm may round differently. That moves
a corner by about 1e-13 m before rounding, enough to change a stored coordinate
by one ulp. The contract prints these boxes, so a port on another
libm would fail there first. Replacing the two calls with a four-entry table of
glibc's values (hex float literals) is cheap and preserves every result on this
build; it has not been done.

## Status

Filled in as each seam lands.

### Seam 1: core (September 2026)

`src/core/` compiles alone under `tools/core-check.sh` (C++17,
`-fno-exceptions -fno-rtti`, only `src/` on the include path).

| file | holds |
|---|---|
| `vec.h` | `Vec2`, `Vec3`, `TAU`, `clampf` |
| `hash.{h,cpp}` | `hash64`, `Rng`, `ih`, `lat`, `vnoise2`, `fbm2` |
| `fp_strict.h` | no floating-point contraction for the file that includes it first (Floating point, above) |
| `level_rules.h` | `LevelRules` (wall height, light pitch, storey pitch, name), `LEVEL_RULES`, `EXIT_NEXT` |
| `world.{h,cpp}` | `ChunkData`, generation, storeys and vertical features, the wall/prop/floor/ceiling accessors and overlays, collision (`gatherCellAABBs`, `collideCircle`, `groundAt`), `lineOfSight`, `canStep`, `pathStep`, `findOpenSpot`, `buildOccupancy`, the Manila Room, vending placement (`vendFootprint`, `liftHash`) |

The platform side of the seam:

| file | holds |
|---|---|
| `src/world_mesh.{h,cpp}` | `bakeChunk` (the mesher), `ChunkMesh` slots, `ChunkMeshCache` |
| `src/mesh_builder.{h,cpp}` | `MB`, `addPropBox`, `addSolidBox`, `PLAIN_UV` |
| `src/object_meshes.{h,cpp}` | can, deck, reels, lamp, flare, crate and lid |
| `src/vec_rl.h` | `toRl` / `fromRl` |
| `src/levels.{h,cpp}` | `LevelCfg : LevelRules` (the look), `lightAtCPU` and the other CPU lighting mirrors |
| `src/util.{h,cpp}` | `cl8`, `SAMPLE_RATE`, `PARTY` |

How meshes follow core's chunks: `ChunkData` holds no meshes. Core appends a
`ChunkRef {storey, cx, cz}` to `World::staleChunks` wherever geometry goes
stale (`unloadFar`, `unloadAll`, `rebuildChunk`, and through it `shiftEdge`
and `unlockEdge`). `ChunkMeshCache` keys baked chunks by absolute storey and
chunk and drains that list at the start of every call, so a mesh lives exactly
as long as its chunk data and `setStorey` needs no event.

Still coupled, and why:

- The mesher reads the world through `World`'s accessors (`bakeChunk(World &,
  ...)`), and it still decides decoration itself from hashes: which walls carry
  fittings, scrawl, conduit, lift doors, spalls and pipes, the light-fitting
  grid, the Red Rooms tint. A second renderer would have to repeat those rules.
  The planned `ChunkLayout` (core describes what to build) is not done.
- Game rules that read the world live in `src/game.cpp` (seam 2): hide spots,
  coins and crates hash cells there, and `tools/mapdump.cpp` mirrors
  `hideSpotAt` and `coinAt`.
- A prop's height is written in three places: `addProp` (mesher),
  `gatherCellAABBs` (core) and `Game::bottleShelfY`.
- Lighting constants are mirrored by hand between core, the mesher, the
  shader and `lightAtCPU`: the panel half-size (0.62), the light plane
  (`wallH - 0.12`), the occupancy bits.
- The render loop reads `ChunkData` directly for the stairwell landing lights
  (`nfeat`, `feats`).

Proofs, against a build of main before the seam (374a84e; two captures of
that binary give the noise floor):

| proof | result |
|---|---|
| mapdump set (this file) | byte-identical, all 11 runs |
| `tools/proof-shots.sh` | all 11 world frames 0 pixels differing (>16), the noise floor; menu.png 3613 against a floor of 736-3054 across three baseline pairs (its camera drifts on the wall clock) |
| regression harness | exit 0, 59 captures |
| `tools/core-check.sh` | passes; fails as expected on a core header that includes `src/util.h` or any `src/` header |
| code that only moved or was renamed | identical assembly: `ih`, `lat`, `vnoise2`, `fbm2` in their new file; `world_mesh.cpp`, `mesh_builder.cpp`, `object_meshes.cpp` before and after naming shared constants |
| comment rewrite | every touched file token-identical with comments removed (`c++ -fpreprocessed -E`) |
