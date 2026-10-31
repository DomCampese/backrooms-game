# Unreal Engine 5 handoff

For the engineer (or agent) who moves this game from raylib to Unreal Engine 5.
Read docs/migration.md first: it defines the layers this plan relies on and
lists what is still coupled.

The owner decides whether the browser build survives. The requirement that
does not move: the game runs smoothly on the owner's Mac.

## Before any code

1. **Licence.** Decided (September 28, 2026): the owner relicensed the game's
   code from GPL-3.0-only to MIT, which Epic's EULA allows beside the engine.
   Third-party material keeps its own terms (CREDITS.md lists each: CC0 textures
   and revolver, CC BY-SA water recordings, CC BY-SA wiki lore). An Unreal build
   must not encrypt the files carrying CC BY-SA material (CREDITS.md, "How the
   lore and software licenses fit together").
2. **Target machine.** Record the Mac's chip, memory, macOS and the Unreal
   version you will use. Check that version's release notes for what its Metal
   renderer supports on that chip (Lumen, Nanite, virtual shadow maps, hardware
   ray tracing). Do not assume a feature is available on Mac because it is
   available on Windows.
3. **Parity build.** Keep the Web build compiling until the port reaches M5.
   It is the reference for every parity check below.

## What carries over

| part | carries over | how |
|---|---|---|
| `shared/core/` | as source | compiled into the plugin module unchanged |
| `tests/golden`, `tools/contract_lib.cpp` | as the acceptance test | the same checks, run by an Unreal automation test |
| `shared/sim/` | as source | compiled into the same module |
| `tests/traces`, `shared/sim/trace.h` | as the sim's acceptance test | recorded input replayed through the sim, every frame's state compared |
| layout (`chunkLayout`, shared/core/layout.h) | as data | drives instanced meshes, light fittings and doors; the per-level choice of decorations is the `DECOR` table in layout.cpp |
| mesher (`shared/port/chunk_mesh.cpp`) | as source | moved to port in October 2026 (byte-identical); Unreal builds its chunks from it |
| shader, occupancy texture, `lightAtCPU`, texture painters | yes | the shaders ported to HLSL (Plugins/Backrooms/Shaders); `buildOccupancy` and `lightAtCPU` (port/levels) as source; the painters' output imported as textures |
| raylib audio, input, window | no | the game's own mix streamed through a procedural sound wave (shared/port/mixer.h), Enhanced Input, the engine |
| sounds, revolver GLB, CC0 textures | as assets | import; keep provenance beside each |

## Module setup

The project is in `unreal/` (its README has the layout and how to open it).

- One plugin, `Backrooms`, with one Runtime module, also `Backrooms`, holding
  core, the sim, `shared/port` and the Unreal classes. Two modules (core, then sim)
  do not link in an editor build: each module is a shared library with hidden
  symbols, and core has no export annotations to add.
- The module compiles the repository's files, not copies: each
  `Private/Shared/*.cpp` is one `#include` of a file in `shared/core`, `shared/sim`,
  `shared/port` or `tools/contract_lib.cpp`. A new source file there needs a
  wrapper; `tools/unreal-check.sh` fails until it has one.
- No PCH (`PCHUsageMode.NoPCHs`): a shared PCH is force-included into every
  file, core's included. No unity build, exceptions or RTTI.
- Floating point: core's generation compares noise against thresholds, and a
  compiler that fuses multiply-adds can flip a comparison and build a different
  maze. Core and sim carry this in source (`shared/core/fp_strict.h`, included
  first by every core, sim and port .cpp; docs/migration.md, "Floating point").
  Do not remove those pragmas, and do not enable fast math. The module sets
  `FPSemantics = FPSemanticsMode.Precise` as well. The sim's vector functions
  (`shared/sim/sim_math.h`) reproduce raymath bit for bit only without contraction.
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
| chunk streaming (`streamChunks`, `unloadFar`, `staleChunks`) | the subsystem keeps a chunk actor per (storey, cx, cz), wanted in shared/port/stream.h's order, as the Web build's; drain `World::staleChunks` every tick before touching a chunk actor |
| walls, floors, ceilings, stairs, pools | M2: a dynamic or procedural mesh per chunk built from the mesher's arrays; later a modular kit placed from core data |
| props, fixtures (`ChunkLayout`) | instanced static meshes per kind, one component per chunk |
| light fittings (`ChunkLayout`) | emissive panel meshes everywhere, plus a pool of real lights assigned to the fittings nearest the player each frame (the shader already sums only the 3x3 fittings round a point). Dead, dim and faulty states come from the layout; flicker and blackouts come from the sim's clock |
| storeys (`storeyH`, `changeStorey` rebasing) | core and the sim stay storey-local. Place storey `s` at `z = s * storeyH` in Unreal space and convert at the boundary; do not port the rebase into Unreal actors. In a run, `HideUnseenChunks` hides every chunk actor `visibleChunks` (shared/port/visible.h) leaves out, as the Web build leaves it undrawn: on the Mac, look up a stairwell and an atrium and turn round for a chunk that stays hidden when it should show |
| player movement and collision | the sim stays authoritative (it uses core's `collideCircle`/`groundAt`), and the pawn follows it, until parity is proven; Unreal collision is for the camera and effects |
| hunter, dogs (billboard sheets) | skeletal meshes driven by sim state; the sim's gait phase counts footfalls and can drive animation and footstep notifies together |
| `InputFrame` | filled from Enhanced Input actions |
| sound events | M6: `SoundMixer` (shared/port/mixer.h) mixes the synthesized clips, the tape voice and the ambience as raylib does, and `UBackroomsSound` streams it through one `USoundWaveProcedural`; the recordings play as imported sound waves. Later, positional sounds could move to Unreal attenuation at the emitter, replacing the sim's bearing pan |
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
| M4 sim | the sim ticks in Unreal from Enhanced Input; replaying a recorded input trace from the Web build reaches the same positions and events (`tests/traces`, `Backrooms.Sim.Replay`) |
| M5 look | lighting, materials and post per level; performance budget met on the Mac |
| M6 actors and sound | hunter, dogs, sound events, HUD |

## Status (September 2026)

`unreal/` builds and runs on the target: an M4 MacBook Pro, macOS 27, Unreal
Engine 5.8.3, first on 29 September 2026. The shared code (core, sim, port)
compiled there first time. The Unreal side needed four fixes, each now in
AGENTS.md "Unreal port": a local `TestHyd.h` from the editor's New C++ Class
menu, `FBackroomsHud` sharing UHT's name with `ABackroomsHUD`, `FAssetData`
needing its own include, and `init_unreal.py` crashing the game under
`-game`. `make unreal`, `unreal-open`, `unreal-play` and `unreal-test`
(`tools/unreal.sh`) build and run it; `tools/unreal-check.sh` covers what can
be checked without Unreal.

| milestone | state |
|---|---|
| M0 project | `unreal/BackroomsGame.uproject` (EngineAssociation 5.8) and the plugin exist; the code is MIT. Target: an Apple M4 Mac on the current macOS, Unreal Engine 5.8. Open: its memory, and what 5.8's Metal renderer supports on the M4 |
| M1 core | built on the Mac, not yet run there: the automation tests `Backrooms.Core.Contract` and `Backrooms.Sim.Replay` compare with `tests/golden` and `tests/traces`. Without Unreal, the same shared files built the module's way (clang, C++20, `-Werror`, FMA enabled, no `-ffp-contract=off`) pass both |
| M2 greybox | runs on the Mac: `UBackroomsWorldSubsystem` streams `ABackroomsChunkActor`s built from `shared/port/greybox.h` round a DefaultPawn free camera, storeys stacked. The greybox itself is checked against raylib frames (below). Frame time on the Mac not measured. Since 25 October the other storeys stream only round the openings that show them, in the Web build's order (shared/port/stream.h), not as full rings: on the Mac, look up a stairwell and an atrium (proof-shots' `stairwell` and `atrium` spots) for a missing chunk, and down a tall court for its far storeys |
| M3 layout | props (as their collision boxes), openings and light panels are in the greybox; a level look (below) can put a static mesh at every prop of a kind and at every light fitting, from `chunkLayout`. Fixtures (outlets, switches, grilles, exit signs, diffusers, sprinklers, conduit, lift doors, pipes, valves, streamers) are placed from `chunkLayout` as plain boxes at their real size, or a look's mesh per kind (`EBackroomsFixture` = `FixtureKind`), written 4 October 2026 and not yet built on the Mac; scrawl, spalls and the Manila Room's furnishings wait for meshes. `tests/golden/layout.txt` holds every layout field |
| M4 sim | runs on the Mac (a run on Level 0: walking, looking, the HUD, the revolver seen in hand): `ABackroomsPlayerController` fills an `InputFrame` from Enhanced Input actions it makes at start-up, `UBackroomsWorldSubsystem::TickRun` steps the sim as `Game::tick` does (title screen, pause, step), bullets trace against the chunk actors, and `ABackroomsPawn`'s camera follows `simView` (shared/port/view.h, which the raylib renderer now draws from too). `-BackroomsRecord=path` writes a trace from Unreal for `./replay`. Sound is M6 (below). What the sim puts in the world each frame (pickups, crates, the deck, flares, chalk, balloons, confetti, impacts, the hunter, the pack) comes from `simScene` (shared/port/scene.h) and `ABackroomsSceneActor` draws it, as plain shapes until a level look gives meshes; `ABackroomsHUD` draws the Web build's HUD from `hudDraws` (shared/port/hud.h). The revolver is `ABackroomsHeldActor`, placed and posed by `heldWeapon` and `revolverPose` (shared/port/held.h, which render.cpp and revolver.cpp now draw from): the editor imports assets/models/revolver.glb on first open (Plugins/Backrooms/Content/Python). Firing and the reload start; the reload clip did not show on the first run, a bone-by-bone copy is pushed but not yet confirmed, and each reload logs how far the drum moved in the clip and on the gun. The can, the deck and a flare in hand are placed by `heldCan`, `heldDeck` and `heldWeapon` and shown with the level look's item mesh (written 3 October 2026, not yet built on the Mac). render.cpp now draws pickups, crates, the deck, coins, balloons, confetti and chalk from `simScene` too, so the two builds cannot place them differently |
| M5 look | rewritten 22 October 2026, not yet built on the Mac. Unreal no longer lights anything: the chunks are the Web build's mesher (shared/port/chunk_mesh.h), every texture the Web build binds is imported as bytes, and every surface's colour is the Web build's world shader ported to HLSL (Plugins/Backrooms/Shaders), fed each frame from `frameLight` (shared/port/draw.h) through `MPC_Backrooms`: the 3x3 panels and their shadow march, faulty and dead tubes, blackouts, the torch and its beam, flares, the chandelier, the stairwell lamps, the hunter's dead light, fog and the tone curve. The post pass replaces Unreal's tonemapper. What render.cpp draws round the chunks, and the revolver, are shared code too (below and unreal/README.md, "How the world is drawn"). The greybox look (`UBackroomsLevelLook`, rect lights, plain shapes) is gone |
| M6 sound | written, not yet built on the Mac (1 October 2026). The clips and the ambience bed moved out of the raylib files into `shared/port/sounds.{h,cpp}` and `shared/port/ambience.{h,cpp}`, byte-identical (below); `SoundMixer` (shared/port/mixer.h) plays the sim's `AudioEvent`s into one stereo stream with raylib's pan law and linear resampling for pitch, and `UBackroomsSound` streams it through a `USoundWaveProcedural`, `SoundLatency` seconds ahead (Project Settings > Game > Backrooms > Sound). The recordings in assets/sounds are imported by the editor (`backrooms_import.import_sounds`) to `/Game/Backrooms/Sounds` and play on components of their own. `Backrooms.Port.Sound` tests the mixer. The hunter and the pack are billboards cut from the Web build's own sheets, posed by `shared/port/sprites.h` and lit by `lightAtCPU`, drawn from `sceneDraws` like everything else render.cpp draws round the chunks |

**Editing in Unreal.** Placement and appearance both come from code shared
with the Web build (unreal/README.md, "How the world is drawn"); controls and
tuning are editor data with code defaults. `UBackroomsSettings` (Project
Settings > Game > Backrooms) holds the input assets, sensitivity, streaming
reach and the revolver's file; `ABackroomsPreviewActor` builds the maze in the
editor viewport; the pawn, controller, chunk actor and game mode are
Blueprintable; `GetHud` feeds a UMG HUD.

**The greybox** (`shared/port/greybox.{h,cpp}`, core and the standard library
only) builds a chunk as flat-coloured boxes and quads from `chunkFloorplan`
(the mesher's source too, since October 2026) and `chunkLayout`: floors, ceilings, steps and soffits, walls, doorways and their
headers, windows, locked doors, exits, rails (sloped caps on flights), stairs as
treads, pillars, props as the boxes collision gives them, water, and light
panels (dead ones dark). Every face is emitted in both windings, so neither
Unreal's front-face convention nor the handedness swap can hide one.
`tools/greybox-view.cpp` draws it with raylib from the camera a capture uses;
at the proof-shot positions (lv0 to lv4, vending, pool, stairwell) it lines up
with the game's frames wall for wall. A capture's spawn spot is picked by
`findOpenSpot` before any level is entered, in Level 0's maze at visit 0 on
one storey, and the viewer does the same.

**Traces** (`shared/sim/trace.h`). `BACKROOMS_RECORD=path` writes every call the
Web build makes on the sim: the start (`SimStart`), level entries, pauses,
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

**Sound** (`shared/port/sounds.h`, `ambience.h`, `mixer.h`). The clip
generators and the ambience synth moved out of sfx.cpp and audio.cpp with
their arithmetic untouched. Proof: the old sfx.cpp, with each
`LoadSoundFromWave` replaced by a dump, and `clipPcm` wrote the same 2,289,228
bytes for every synthesized clip and the tape voice; the old
`AudioSynth::update` and `Ambience::render` wrote the same 1,474,560 bytes over
60 changes of `AmbienceMix` touching every field. raylib keeps playing them as
before (`GameAudio` now loads from `clipSpec`). `SoundMixer` is new and has no
raylib counterpart to compare against; `Backrooms.Port.Sound` checks what it
must do.

What to expect on the Mac:

- **Contract**: should pass, `floorplan.txt` (October 2026) included. Core calls no transcendental libm function: the
  tube hash's `sinf` became core's own `portableSin` on 11 October 2026
  (docs/migration.md, "Floating point").
- **Replay**: should pass. Since 13 October 2026 the sim takes sin, cos, exp,
  atan2 and hypot from `core/portable_math.h` too, so neither calls a
  transcendental libm function (`tools/core-check.sh` enforces it). A replay
  failure names the first frame and field that moved; with libm out of the
  way, look at the compiler's flags first (fast math, contraction).

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
- AGENTS.md "Known problems" is mostly about the Web build's renderer and
  tools. The generator, wall and sim entries apply to both builds.

## Next

The list below is what is waiting on the Mac. It is not the work queue. Each
session's main item is a step forward for the game that a player would notice
(AGENTS.md, "Every session makes the game better to play"), not more port
plumbing.

As of 22 October 2026 the world is drawn as the Web build draws it, written
off the Mac. In order:

0. **Build and open.** `make unreal`, then `make unreal-open`. The Output Log
   should say "imported 12 sounds" and "built the world's materials". A
   compile error in the new classes (`UBackroomsRender`,
   `ABackroomsSceneActor`, `ABackroomsHeldActor`, `BackroomsChunkBuild`) or
   a shader error in `M_BackroomsWorld` or `M_BackroomsPost` (Output Log,
   LogShaderCompilers) is the likeliest first failure. The new files passed a
   syntax check against a mock of the engine's API, not the engine; the
   shaders compiled with DXC, not the engine's compiler.
1. **Compare with the Web build.** `make unreal-play` and a capture of the
   Web build at the same seed and level (`tools/shot.sh`, BACKROOMS_POS):
   panels and their pools of light, a dead panel dark, relief lit from above,
   the fog's colour at the end of a hall, the torch (F) with its beam in the
   air. "the world is not drawn" in the log means the editor has not built
   the materials. Everything uniformly too bright or dark points at exposure
   (the post volume pins it at 1); upside-down relief at `BR_DFDY`.
2. **The reload.** Fire, press R: the cylinder swings out and back as in the
   Web build. "revolver: cannot read" in the log is the GLB's path
   (Project Settings > Game > Backrooms > Revolver Model).
3. **Run the automation tests on the Mac** (`make unreal-test`).
4. **Hear it (M6).** As before: the hum under a live fitting, footsteps, the
   shot, a splash in the Poolrooms.
5. **The HUD.** `ABackroomsHUD` draws the Web build's HUD from `hudDraws`
   (shared/port/hud.h, 23 October 2026, not yet built on the Mac): the title
   card, the intro's fade and level name, the meters, the inventory, the
   death card. Check the type's size against a Web build capture at the same
   window height (each line is scaled so its line height is raylib's size)
   and that centred lines sit centred. The touch hints are the Web build's alone.
6. **The camcorder** (24 October 2026, not yet built on the Mac). V raises it
   with a motor whine and a beep; the revolver leaves the frame, the OSD comes
   up (REC, the counter, the date, the battery) and the inventory goes. The
   wheel and RMB zoom (the W-T bar), LMB switches night shot, whose lamp lights
   the room as the Web build's does.
7. **The tape** (27 October 2026, not yet built on the Mac). Through the
   viewfinder the picture is tape: soft colour smeared right, scanlines, a
   rolling tear when Clark is near (night shot green). Die after a minute of
   play and the card plays the last seconds back: snow, the run, slowing,
   a shaking still. A shader error in `M_BackroomsTape` (LogShaderCompilers)
   is the likeliest first failure, then the reel: a playback that is black
   means the capture or the copy into the reel (`UBackroomsReel::Grab`) drew
   nothing; one upside down, that the reel's rows do not run down as
   `cellY` does. `tools/tape-check.sh` proved the shader and the material's
   code against `TAPE_FS` pixel for pixel, with DXC and spirv-cross; the
   reel's capture and copy have not run anywhere.

8. **Performance** (28 October 2026, not yet measured on the Mac). `stat
   unit` and `stat gpu` while playing; then Project Settings > Game >
   Backrooms > Performance: `LightQuality` Medium or Low, `ScreenPercentage`
   below 100, `MaxFPS` (60 by default). High must look as it did.

9. **The title screen, records and F3** (28 October 2026). `make unreal-play`
   with no LEVEL or SEED opens on the drifting title card; Enter or a click
   begins. Die once: the death card's records line counts the run, and
   `unreal/Saved/backrooms_best` holds it. F3 in a run draws the fps,
   position, chunk and hunter line in the top left.

10. **Footsteps** (29 October 2026). Walk on Level 0 (carpet), Level 1
    (`LEVEL=1 make unreal-play`, concrete), Level 2 (tile) and in the Manila
    Room (wood). No Unreal code changed; the mixer loads the new clips, so
    they should match the Web build.

11. **Distant sounds** (30 October 2026). Wait on Level 0 for up to 70 s:
    a muffled door sound plays, panned. `LEVEL=1`: clang, `LEVEL=3`: pipe
    knocks, `LEVEL=4`: balloon pop, `LEVEL=2`: none. No Unreal code changed;
    the mixer loads every clip up to `SFX_COUNT`.

12. **The hunter walks toward noise** (31 October 2026). On Level 0, F3 then
    `E`, turn away and fire: his footsteps come closer. Turn to face him and
    he stands still. No Unreal code changed; it is sim behaviour.

A session on the Mac itself (`claude remote-control` in the repository) could
build, run the tests and read the log directly; from the cloud each Unreal
change has waited on a build and a pasted log.

## Parity

What the Web build does that the Mac build does not yet. As of 28 October
2026 every gap a player would meet is closed in code, though not all of it has
been seen on the Mac:

- the title screen: a run with no `?level=`, `?seed=` or `?notitle` opens on it;
- the records file (shared/sim/records.h), at `unreal/Saved/backrooms_best`;
- F3's debug lines;
- the tape's echoes of recorded sounds (a splash or the party music heard
  again in the playback);
- the reel's frames: both builds tape the scene's colour, translucent
  surfaces included, over the level's clear colour.

Everything else (the world, its lighting, the sim, the HUD, the revolver,
sound, the camcorder and its tape) is the same code or a port proved against
it. What remains:

| gap | Web build | Mac build |
|---|---|---|
| touch controls | the browser shell's on-screen controls | none needed on the Mac |

New features go into both builds in the same change (AGENTS.md, "Two builds,
one repository").

## Not done yet

What the seams leave for the port, or for the Web build before it.

| item | where | why it matters |
|---|---|---|
| the camcorder's tape | written (27 October 2026): `UBackroomsReel` and `M_BackroomsTape` (`BackroomsTape.ush`); the reel's frames match the Web build's since 28 October (unblended colour, the clear colour behind) | not yet run on the Mac |
| real meshes | props, fixtures, items and actors | the looks can take a mesh for every kind; none is authored yet, so props are their collision boxes, fixtures and items plain shapes, and actors billboards |
| floorplan geometry as data | done (October 2026): `chunkFloorplan` (core/floorplan.h) describes floors, walls, risers, ceilings, soffits, flights and arches, and the mesher and the greybox build from it | Unreal's chunk actors draw it through the greybox; real meshes for walls and floors (skirting, door trim, stair nosing) would be placed from it, as fixtures are from `chunkLayout` |
| sound in Unreal | written (`UBackroomsSound`), not yet heard on the Mac | M6 |
| lighting numbers mirrored by hand | the panel half-size and light plane are core's (`PANEL_HALF`, `LIGHT_DROP`), read by the mesher, the shader's `uLY` and `lightAtCPU` callers (game.cpp, render.cpp), the greybox and Unreal's lights, and the shader gets `PANEL_HALF`, the storey hash offset's numbers, the tube hash's, the stutter's (`tubeStutter`), the pillar footprint and the occupancy grid's bits (`OccBit`) written in at load | done: a port reads them from core; the GLSL's lighting arithmetic is still its own |
| HUD strings are `const char *` sim state | shared/sim | a port maps them to its own text type |
