# Unreal Engine 5 handoff

For the engineer (or agent) who moves this game from raylib to Unreal Engine 5.
Read docs/migration.md first: it defines the layers this plan relies on and
lists what is still coupled.

The owner decides whether the browser build survives. The requirement that
does not move: the game runs smoothly on the owner's Mac.

## Before any code

1. **Licence.** The game is GPL-3.0-only (LICENSE, CREDITS.md). Code linked into
   an Unreal Engine binary is distributed under Epic's EULA, which is not
   compatible with the GPL. The owner holds the copyright in the original code
   and can license it differently for the Unreal version; third-party material
   keeps its own terms (CREDITS.md lists each: CC0 textures and revolver, CC BY-SA
   water recordings, CC BY-SA wiki lore). Get the owner's decision in writing
   before porting code. This is a licensing question, not a technical one, and
   it blocks M1.
2. **Target machine.** Record the Mac's chip, memory, macOS and the Unreal
   version you will use. Check that version's release notes for what its Metal
   renderer supports on that chip (Lumen, Nanite, virtual shadow maps, hardware
   ray tracing). Do not assume a feature is available on Mac because it is
   available on Windows.
3. **Parity build.** Keep the raylib build compiling until the port reaches M5.
   It is the reference for every parity check below.

## What carries over

| part | carries over | how |
|---|---|---|
| `src/core/` | as source | an Unreal plugin module, unchanged |
| `tests/golden`, `tools/contract.cpp` | as the acceptance test | run the same checks from an Unreal automation test |
| `src/sim/` | as source | a second module, `BackroomsSim`, that depends on core |
| layout (`chunkLayout`, src/core/layout.h) | as data | drives instanced meshes, light fittings and doors; the per-level choice of decorations is the `DECOR` table in layout.cpp |
| mesher (`src/world_mesh.cpp`) | as reference only | its vertex arrays can feed a greybox (M2); art replaces it |
| shader, occupancy texture, `lightAtCPU`, texture painters | no | Unreal's lighting and materials replace them |
| raylib audio, input, window | no | MetaSounds / Sound Cues, Enhanced Input, the engine |
| sounds, revolver GLB, CC0 textures | as assets | import; keep provenance beside each |

## Module setup

- One plugin, `Backrooms`, with a Runtime module `BackroomsCore` containing
  `src/core` verbatim. Build.cs: C++17 or later, `bEnableExceptions = false`,
  `bUseRTTI = false` (core already builds this way under tools/core-check.sh).
- Floating point: core's generation compares noise against thresholds, and a
  compiler that fuses multiply-adds can flip a comparison and build a different
  maze. Core carries this in source (docs/migration.md, "Contract tests"); do
  not remove those pragmas, and do not enable fast-math for the module.
- Unity builds merge .cpp files. Core has file-static helpers; if two collide,
  exclude the module from unity builds rather than renaming things.
- `World` is not thread-safe: `data()` generates on read and `StoreyScope`
  changes `World::qs`. Generate on one thread, or give a worker thread its own
  `World` with the same seed, level and visit.

## Entry points

The whole surface an Unreal port calls. docs/migration.md has the contracts in
full; this is the list to wire up.

| call | layer | what it gives |
|---|---|---|
| `World::seed`, `level`, `visit`, `wallH`, `storeyH` from `LEVEL_RULES` | core | set before the first `data()` |
| `World::data(cx, cz)` | core | a generated chunk (`ChunkData`), made on first read |
| `chunkLayout(world, cx, cz)` | core | props, fixtures, light fittings and openings of a chunk on storey `qs`, in metres |
| `wallNVal`, `wallWVal`, `floorY`, `ceilY`, `vflagAt`, `stairY` | core | the floorplan, overlays included |
| `collideCircle`, `groundAt`, `lineOfSight`, `canStep`, `pathStep`, `findOpenSpot` | core | physics and AI queries, storey-local |
| `World::staleChunks` | core | chunks whose geometry must be rebuilt; drain every tick |
| `Sim::step(InputFrame, dt, now)` | sim | one tick of play; `dt` clamped to 0.05 s, `now` a monotonic clock in seconds |
| `Sim::menuDrift`, `menuBegin`, `setPaused` | sim | the title screen and pause |
| `Sim::events` (`AudioEvent`) | sim | sounds to play this tick, in order |
| `SolidTracer` | platform, supplied by the port | the one query the sim asks the renderer: first solid triangle along a ray (bullets). In Unreal, a line trace against the chunk meshes |

What the port supplies to the sim each tick:

| input | from |
|---|---|
| `InputFrame` (movement, look delta in px, held and pressed buttons, `playing`) | Enhanced Input actions; `look` scaled so a pixel means what a raylib mouse pixel meant (0.0030 rad) until retuned |
| `dt`, `now` | the engine's delta and a game-time clock. Blackouts, spawns and whispers are scheduled on `now` |
| `AudioEvent` playback | a sound component: per-clip pitch, volume and pan persist between plays and the rules rely on it (docs/migration.md, seam 3) |

What the port reads from the sim to draw: the player (`px, py, pz, eyeY, yaw,
pitch, fwd, fov`, lean and roll), `ent`, `dogs`, `litFlares`, `deck`,
`bullets`, `bulletImpacts`, `coinsWorld`, `confetti`, `chalk`, the pickup and
balloon functions, and the HUD state (`deckNote`, `sanityLine`, `tapeLine`,
`deathBy`, `deathTitle`).

## Mapping

| this game | Unreal |
|---|---|
| `World` owned by `Game` | a `UWorldSubsystem` (or `UGameInstanceSubsystem` if a level change must keep it) owning `World` and the sim |
| chunk streaming (`streamChunks`, `unloadFar`, `staleChunks`) | the subsystem keeps a chunk actor per (storey, cx, cz), created and destroyed from the same rules; drain `World::staleChunks` every tick before touching a chunk actor |
| walls, floors, ceilings, stairs, pools | M2: a dynamic or procedural mesh per chunk built from the mesher's arrays; later a modular kit placed from core data |
| props, fixtures (`ChunkLayout`) | instanced static meshes per kind, one component per chunk |
| light fittings (`ChunkLayout`) | emissive panel meshes everywhere, plus a pool of real lights assigned to the fittings nearest the player each frame (the shader already sums only the 3x3 fittings round a point). Dead, dim and faulty states come from the layout; flicker and blackouts come from the sim's clock |
| storeys (`storeyH`, `changeStorey` rebasing) | core and the sim stay storey-local. Place storey `s` at `z = s * storeyH` in Unreal space and convert at the boundary; do not port the rebase into Unreal actors |
| player movement and collision | the sim stays authoritative (it uses core's `collideCircle`/`groundAt`), and the pawn follows it, until parity is proven; Unreal collision is for the camera and effects |
| hunter, dogs (billboard sheets) | skeletal meshes driven by sim state; the sim's gait phase counts footfalls and can drive animation and footstep notifies together |
| `InputFrame` | filled from Enhanced Input actions |
| sound events | a component that plays each event with attenuation at its position |
| tone curve, migraine, blackout post | post-process volume and materials |
| coordinates | core: metres, y up, +z south; Unreal: centimetres, z up, left-handed. Convert in one function and test it with the contract probes |

## Milestones

Each ends with a check that can fail.

| milestone | done when |
|---|---|
| M0 project | Unreal project opens and runs on the target Mac; licence decision recorded |
| M1 core | `BackroomsCore` compiles in Unreal; an automation test runs every case in `tests/golden` and passes |
| M2 greybox | chunks stream round a free camera on Level 0 and Level 1, walls and floors from core, storeys stacked; frame time measured on the Mac |
| M3 layout | props, fixtures, doors and panel positions placed from `ChunkLayout`; a screenshot at the proof-shot positions (tools/proof-shots.sh) matches the raylib frame's layout |
| M4 sim | the sim ticks in Unreal from Enhanced Input; walking the same scripted input as the raylib build reaches the same positions (sim trace comparison) |
| M5 look | lighting, materials and post per level; performance budget met on the Mac |
| M6 actors and sound | hunter, dogs, sound events, HUD |

## Parity rules

- Same seed, level, visit and storey give the same chunk contents and the same
  query answers (`tests/golden`). A port that fails the goldens is building a
  different game.
- The sim's update order is fixed (docs/migration.md, seam 3). Keep it.
- Anything with a position must be converted by the one conversion function.

## Pitfalls that still apply

- Walls are read through `wallNVal`/`wallWVal`, which include the overlays for
  shifted walls and unlocked doors. Reading `ChunkData::wallN` directly shows a
  wall that collision no longer has.
- The mesher reads neighbouring chunks (for ceilings and wall bases), so baking
  a chunk can generate its neighbours. Generation itself never reads a
  neighbour.
- `mapdump` and the goldens default to visit 0; the game's first arrival on a
  level after the menu is visit 1 on Level 0. Use `--visit 1` when comparing
  with a capture.
- AGENTS.md "Things that will bite you" is written for the raylib build; most
  of it dies with the renderer, but the generator and storey entries still hold.
