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
| `src/core/` | as source | compiled into the plugin module unchanged |
| `tests/golden`, `tools/contract_lib.cpp` | as the acceptance test | the same checks, run by an Unreal automation test |
| `src/sim/` | as source | compiled into the same module |
| `tests/traces`, `src/sim/trace.h` | as the sim's acceptance test | recorded input replayed through the sim, every frame's state compared |
| layout (`chunkLayout`, src/core/layout.h) | as data | drives instanced meshes, light fittings and doors; the per-level choice of decorations is the `DECOR` table in layout.cpp |
| mesher (`src/world_mesh.cpp`) | as reference only | the greybox (`src/port`) replaces it for M2; art replaces both |
| shader, occupancy texture, `lightAtCPU`, texture painters | no | Unreal's lighting and materials replace them |
| raylib audio, input, window | no | MetaSounds / Sound Cues, Enhanced Input, the engine |
| sounds, revolver GLB, CC0 textures | as assets | import; keep provenance beside each |

## Module setup

The project is in `unreal/` (its README has the layout and how to open it).

- One plugin, `Backrooms`, with one Runtime module, also `Backrooms`, holding
  core, the sim, `src/port` and the Unreal classes. Two modules (core, then sim)
  do not link in an editor build: each module is a shared library with hidden
  symbols, and core has no export annotations to add.
- The module compiles the repository's files, not copies: each
  `Private/Shared/*.cpp` is one `#include` of a file in `src/core`, `src/sim`,
  `src/port` or `tools/contract_lib.cpp`. A new source file there needs a
  wrapper; `tools/unreal-check.sh` fails until it has one.
- No PCH (`PCHUsageMode.NoPCHs`): a shared PCH is force-included into every
  file, core's included. No unity build, exceptions or RTTI.
- Floating point: core's generation compares noise against thresholds, and a
  compiler that fuses multiply-adds can flip a comparison and build a different
  maze. Core and sim carry this in source (`src/core/fp_strict.h`, included
  first by every core, sim and port .cpp; docs/migration.md, "Floating point").
  Do not remove those pragmas, and do not enable fast math. The module sets
  `FPSemantics = FPSemanticsMode.Precise` as well. The sim's vector functions
  (`src/sim/sim_math.h`) reproduce raymath bit for bit only without contraction.
- `World` is not thread-safe: `data()` generates on read and `StoreyScope`
  changes `World::qs`. Generate on one thread, or give a worker thread its own
  `World` with the same seed, level and visit.
- Unreal defines `check()` as a macro, so no shared identifier may be named
  `check` (the contract library's comparison is `contract::compare`).

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
| `Sim::audio` (`AudioEvent`s) | sim | sounds to play this tick, in order; the port plays and clears it |
| `SolidTracer` | platform, supplied by the port | the one query the sim asks the renderer: the first solid triangle along a `Ray3`, returning distance and a `Vec3` normal (bullets). In Unreal, a line trace against the chunk meshes |

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
`deathBy`, `deathTitle`). Balloon and confetti colours are indices into a
palette of `PARTY_COLOURS` entries; the port owns the colours.

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
| M4 sim | the sim ticks in Unreal from Enhanced Input; replaying a recorded input trace from the raylib build reaches the same positions and events (`tests/traces`, `Backrooms.Sim.Replay`) |
| M5 look | lighting, materials and post per level; performance budget met on the Mac |
| M6 actors and sound | hunter, dogs, sound events, HUD |

## Status (September 2026)

Nothing in `unreal/` has been compiled by Unreal: it was written where no
engine was available. `tools/unreal-check.sh` covers what can be checked
without one.

| milestone | state |
|---|---|
| M0 project | `unreal/BackroomsGame.uproject` and the plugin exist. Open: the licence decision, the target Mac's chip, memory and macOS, and the Unreal version |
| M1 core | ready to run: the automation tests `Backrooms.Core.Contract` and `Backrooms.Sim.Replay` compare with `tests/golden` and `tests/traces`. Without Unreal, the same shared files built the module's way (clang, C++20, `-Werror`, FMA enabled, no `-ffp-contract=off`) pass both |
| M2 greybox | written, not run: `UBackroomsWorldSubsystem` streams `ABackroomsChunkActor`s built from `src/port/greybox.h` round a DefaultPawn free camera, storeys stacked. The greybox itself is checked against raylib frames (below). Frame time on the Mac not measured |
| M3 layout | props (as their collision boxes), openings and light panels are in the greybox; fixtures are not. `tests/golden/layout.txt` holds every layout field |
| M4 sim | the trace recorder and replay exist (below); the sim does not tick in Unreal yet |
| M5, M6 | not started |

**The greybox** (`src/port/greybox.{h,cpp}`, core and the standard library
only) builds a chunk as flat-coloured boxes and quads from core's accessors and
`chunkLayout`: floors, ceilings, steps and soffits, walls, doorways and their
headers, windows, locked doors, exits, rails (sloped caps on flights), stairs as
treads, pillars, props as the boxes collision gives them, water, and light
panels (dead ones dark). Every face is emitted in both windings, so neither
Unreal's front-face convention nor the handedness swap can hide one.
`tools/greybox-view.cpp` draws it with raylib from the camera a capture uses;
at the proof-shot positions (lv0 to lv4, vending, pool, stairwell) it lines up
with the game's frames wall for wall. A capture's spawn spot is picked by
`findOpenSpot` before any level is entered, in Level 0's maze at visit 0 on
one storey, and the viewer does the same.

**Traces** (`src/sim/trace.h`). `BACKROOMS_RECORD=path` writes every call the
raylib build makes on the sim: the start (`SimStart`), level entries, pauses,
each frame's `InputFrame`, `dt` and clock, each `SolidTracer` answer, and a
digest of the state after the frame. `./replay path` (core and sim only) runs
it through a fresh `Sim`, answering the tracer from the recording, and stops at
the first digest that differs, naming the frame and the fields.
`tools/record-trace.sh` drives the game with xdotool and wrote
`tests/traces/walk-l0-l2.trace` (152 frames: a run begun, walking, sprinting,
the hunter called up and shot at, a reload, a flare, a drink, a pause, Levels 0
to 2). Replayed with gcc and clang, `-O2` and `-march=native`: every digest
matches. Without `-ffp-contract=off` or the pragma, both compilers diverged at
frame 31; the sim now includes `fp_strict.h` like core.

What to expect on the Mac:

- **Contract**: should pass. If only fitting lines of `layout.txt` fail, it is
  `tubeHash`'s `sinf` (docs/migration.md, "Floating point").
- **Replay**: may not. The sim calls `sinf`, `cosf`, `atan2f`, `expf` and
  friends, and Apple's libm may round some last bits differently from glibc's.
  A replay failure names the first frame and field that moved. If it is libm,
  the fix is in the sim (a table or a portable implementation where the result
  feeds a decision), not in the trace.

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
- `mapdump` and the goldens default to visit 0. A headless capture of Level 0
  shows visit 1, and of Level 1 visit 0 (AGENTS.md). Match the visit before
  comparing positions with a capture.
- AGENTS.md "Things that will bite you" is written for the raylib build; most
  of it dies with the renderer, but the generator and storey entries still hold.

## Not done yet

What the seams leave for the port, or for the raylib build before it.

| item | where | why it matters |
|---|---|---|
| floorplan geometry as data | walls, floors, ceilings, soffits, stairs, pools, arches, skirting are still built directly by `bakeChunk` | the greybox (`src/port`) derives them from core's accessors for M2; art needs core to describe the floorplan the way `chunkLayout` describes fixtures |
| the sim in Unreal | an actor or subsystem that fills `InputFrame` from Enhanced Input, calls `simBegin`/`applyLevel`/`simPlace` and `Sim::step`, supplies a `SolidTracer` (a line trace) and plays `AudioEvent`s | M4 |
| libm in the sim | src/sim | replaying a Linux trace on a Mac may diverge (Status, above) |
| `tubeHash`'s `sinf` | src/core/layout.cpp | the one libm call left in core; may flip a fitting's dead or faulty state on another libm |
| lighting numbers mirrored by hand | panel half-size, light plane, tube hash in core, the mesher, the shader and `lightAtCPU` | a port reads them from core; the raylib build still has copies |
| HUD strings are `const char *` sim state | src/sim | a port maps them to its own text type |
