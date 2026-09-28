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
| frames unchanged | `tools/proof-shots.sh DIR` before and after, then `--diff` | at the noise floor of one binary captured twice |
| behaviour unchanged | the regression harness (AGENTS.md) | exit 0 |

The mapdump set:

```bash
for lv in 0 1 2 3 4; do for v in 0 1; do
  ./mapdump --level $lv --seed 1337 --visit $v --cells 65 --plan -16 -16 32 32
done; done
./mapdump --level 0 --seed 1337 --visit 1 --list-stairs --no-plan
```

## Status

Filled in as each seam lands.

### Seam 1: core (September 2026)

`src/core/` compiles alone under `tools/core-check.sh` (C++17,
`-fno-exceptions -fno-rtti`, only `src/` on the include path).

| file | holds |
|---|---|
| `vec.h` | `Vec2`, `Vec3`, `TAU`, `clampf` |
| `hash.{h,cpp}` | `hash64`, `Rng`, `ih`, `lat`, `vnoise2`, `fbm2` |
| `level_rules.h` | `LevelRules` (wall height, light pitch, storey pitch, name, which tubes work), `LEVEL_RULES`, `EXIT_NEXT` |
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
  ...)`). What it used to decide from hashes (fixtures, fittings, props, doors)
  is now core's `ChunkLayout`: see seam 2 below for what is left.
- Game rules that read the world live in `src/game.cpp` (seam 2): hide spots,
  coins and crates hash cells there, and `tools/mapdump.cpp` mirrors
  `hideSpotAt` and `coinAt`.
- A prop's height is written in three places: `addProp` (mesher),
  `gatherCellAABBs` (core) and `Game::bottleShelfY`.
- Lighting constants are mirrored by hand between core, the mesher, the
  shader and `lightAtCPU`: the panel half-size (0.62), the light plane
  (core's `LIGHT_DROP`; render.cpp and the shader uniforms still write
  `wallH - 0.12`), the occupancy bits, the tube hash (`tubeHash` in core, `lhash`
  in GLSL).
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

### Seam 2: layout (partial, September 2026)

`chunkLayout(world, cx, cz)` (`src/core/layout.{h,cpp}`) describes what a chunk
of storey `qs` holds besides its floorplan, in world metres in that storey's
frame. `bakeChunk` builds each item and decides nothing about where it goes.
`./mapdump --layout CX CZ` prints it.

| list | holds |
|---|---|
| `props` | kind, cell, centre (after `vendFootprint`), floor height, quarter turns and yaw, the against-wall flag, the variation hashes the mesher builds from |
| `fixtures` | outlets (whole and broken), switches, grilles, exit signs, diffusers, sprinklers, conduit runs, scrawl (phrase, size, tilt, tint), lift doors, spalls on walls and columns, pipe runs, valve standpipes; per chunk, the Manila Room and Level 4's streamers |
| `fittings` | every light fitting on the level's grid in the chunk: position, the ceiling it hangs from, why one is not there (under an opening, in the Manila Room), Level 1's turn, and the static tube state (dead, output, faulty) from the same hash as the shader and `lightAtCPU` |
| `openings` | doorways (with joined neighbours), locked doors, exits and cursed exits (with their glyph and room for an open leaf), windows, rails (cap heights, which side is a void): the opening, wall base and top, header, sill |

The per-level choice of decorations is one table (`DECOR` in layout.cpp).
Flicker, blackouts and the hunter's dead-light pool vary with time and stay in
the renderer. Which tubes work (`dead`, `vary`, `faulty`) moved to
`LevelRules`, and `tubeHash`/`storeyHashOffset` to core.

Still decided in the mesher, and why:

| what | why it stays |
|---|---|
| walls, floors, ceilings and soffits, stairs, pools, the Poolrooms' arches, baked AO, skirting | the follow-up seam: geometry of the floorplan itself |
| Level 1's dock safety edging | follows floor drops, which is floor geometry |
| the Red Rooms tint | a colour field round cursed exits in the 3 x 3 chunks, read from core's `cursedExit`; it is how the walls look, not something standing there |
| the Manila Room's floorboards, paper, notes and chandelier | built from one `Rng` seeded by the layout's seed; the notes share the stream with the boards, so moving them means moving the boards |
| each prop's shape and variation (stacked cartons, the dead vending machine, cup count) | derived from the layout's hashes; how a prop looks |
| fitting style per level (troffer, batten, tray), exit glow colours, which exits get a glyph (Level 1), Level 2's emissive window panes | looks |
| the stairwell landing lamp's body | stairs |

Proofs, against the tree before this seam (a2755e9):

| proof | result |
|---|---|
| mapdump set (this file) | byte-identical to the baseline, all 11 runs |
| mesh bake | 1900 chunk bakes, old mesher against new, every slot's vertex, uv, normal, colour and index arrays byte-identical (43.5 M vertices a side). 100 chunks (cx, cz in -5..4) for each of: every level at visits 0 and 1, Level 0 storeys -1 and +1, exits everywhere on every level, the pinned Manila Room. Every fixture, opening and fitting-gap kind occurs. Moving the decal standoff by 0.1 mm fails it in 3037 slots |
| `tools/proof-shots.sh` | all 11 world frames 0 pixels differing (>16) against the baseline; menu.png 18% (its camera runs on the wall clock). Below the threshold the frames wobble with frame rate: the baseline binary recaptured differs from its own earlier lv1 by 624k pixels (max 13 levels), and from the new binary captured beside it by 95k (max 4) |
| regression harness | exit 0, 59 captures |
| `tools/core-check.sh` | passes, layout.h and layout.cpp included |

The mesh bake proof compiles the saved old `world_mesh.cpp` with
`-DbakeChunk=bakeChunkOld -DChunkMeshCache=ChunkMeshCacheOld` beside the
current one, opens a hidden raylib window (meshes upload to GL), bakes each
chunk with both and `memcmp`s the arrays. It lived in scratch space.
