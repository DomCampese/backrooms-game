# The Backrooms on Unreal Engine 5

The port described in docs/unreal-handoff.md. The Web build in the rest of
this repository stays the reference until the port reaches M5; nothing here
replaces it, and nothing in the Web build depends on this folder.

It builds and runs on an M4 Mac with Unreal Engine 5.8.3 (29 September 2026).
Since 22 October 2026 it draws the world as the Web build does: the Web
build's chunk meshes, its textures and its world shader (ported to HLSL), its
flashlight, flares and post pass, and the revolver posed by the Web build's
own arithmetic. That pass was written off the Mac and has not been built
there yet. It was written where no engine was
available, so each Mac build so far has turned up a fix or two on the Unreal
side; the shared code compiled first time. `tools/unreal-check.sh` checks what
can be checked without Unreal (below). docs/unreal-handoff.md, "Status", has
what is open.

The short version, on the Mac, from the repository's root:

```bash
make unreal-open    # build, open the editor (first time: imports the revolver)
make unreal-play    # build, run the game in a window
make unreal-materials  # build the sounds and the world's materials, no editor window
make unreal-test    # build, run the Backrooms automation tests
tools/unreal.sh log # the "Backrooms:" lines from the last run
```

## Layout

| path | what |
|---|---|
| `BackroomsGame.uproject` | the project; its only module is a stub, the game lives in the plugin |
| `Plugins/Backrooms/` | the `Backrooms` Runtime module, and `BackroomsShaders`, which only maps `Shaders/` to `/BackroomsShaders` (PostConfigInit) |
| `.../Private/Shared/*.cpp` | one file per source in `shared/core`, `shared/sim`, `shared/port` and `tools/contract_lib.cpp`, each a single `#include`: the module compiles the repository's files, not copies |
| `Plugins/Backrooms/Shaders` | `BackroomsWorld.ush`, `BackroomsPost.ush` and `BackroomsTape.ush`: the Web build's world, post and tape shaders (web/src/shaders.cpp) in HLSL, line for line |
| `.../Public/BackroomsCoords.h` | the one conversion between core (metres, y up) and Unreal (centimetres, z up) |
| `.../BackroomsWorldSubsystem` | owns the `Sim` (and core's `World` in it); steps a run, streams chunk actors round the player, sets each frame's light, `GetHud` for a HUD |
| `.../BackroomsRender` | the world's materials, textures and parameter collection, the shadow grid (`World::buildOccupancy`), the post volume |
| `.../BackroomsPlayerController` | the sim's controls as Enhanced Input actions; fills an `InputFrame` each frame |
| `.../BackroomsPawn` | the camera, placed where the sim's view is (shared/port/view.h) |
| `.../BackroomsChunkActor`, `BackroomsChunkBuild` | a chunk: the Web build's mesher (shared/port/chunk_mesh.h) as procedural mesh sections, masked and translucent |
| `.../BackroomsSceneActor` | what render.cpp draws round the chunks (shared/port/draw.h): crates, cans, the deck, coins, balloons, chalk, flares, the hunter and the pack, rounds, impacts, mist, the muzzle flash |
| `.../BackroomsHeldActor` | the revolver: assets/models/revolver.glb read at run time and posed by shared/port/revolver.h |
| `Plugins/Backrooms/Content/Python` | run by the editor on open: imports the recorded sounds, and the Web build's textures with the materials (`backrooms_world.py`) |
| `.../BackroomsSettings` | Project Settings > Game > Backrooms |
| `.../BackroomsPreviewActor` | the generated maze in the editor viewport |
| `.../BackroomsSound` | the sim's sound: the game's own mix (shared/port/mixer.h) streamed through a procedural wave, the imported recordings on components of their own |
| `.../BackroomsTypes` | the controls enum, the HUD snapshot |
| `.../BackroomsGameMode` | starts a run (or a free camera with `?mode=free`) from the map's options |
| `.../Private/Tests/BackroomsTests.cpp` | automation tests: contract, trace replay, coordinates, the chunk mesher, the mixer |

Why one module, not the core and sim modules the plan first described: core has
no export annotations, and editor builds link each module as a shared library
with hidden symbols, so a second module could not call core. Why no PCH: a
shared PCH is force-included into every file of the module, core's included,
and core must see nothing but itself and the standard library.

A new `.cpp` in `shared/core`, `shared/sim` or `shared/port` needs a wrapper in
`Private/Shared`. `tools/unreal-check.sh` fails until it has one.

## Opening it

The project compiles files from the repository's `shared/`, so check out the whole
repository, not just this folder. On the Mac:

1. Xcode, the version Unreal 5.8's release notes ask for, opened once so it
   finishes installing its components.
2. Unreal Engine 5.8 from the Epic Games Launcher.
3. Build the editor modules from a terminal, which prints any compile errors
   in full (the editor's own prompt hides them behind "could not be compiled"):

   ```bash
   "/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/Build.sh" \
       BackroomsGameEditor Mac Development \
       -Project="$PWD/unreal/BackroomsGame.uproject" -WaitMutex
   ```

   Run it from the repository's root. The path is the Launcher's default
   install; change it if the engine is elsewhere.

   `make unreal` from the repository's root does the same through
   `tools/unreal.sh`, and first refuses while the editor is open (it locks the
   plugin) and lists files under `unreal/` source folders that the repository
   does not have. Run it after every pull: opening the `.uproject` does not
   recompile changed source, it runs the last build. `make unreal-open`,
   `make unreal-play` (`LEVEL=1 SEED=42 make unreal-play`) and
   `make unreal-test` build first; `tools/unreal.sh log` prints the
   "Backrooms:" lines from the last run.
4. Open the project: double-click `unreal/BackroomsGame.uproject`, or in the
   Launcher, Unreal Engine > Library > Launch 5.8 > Browse, and pick it. If the
   editor asks to rebuild missing modules, say yes (step 3 already did). The
   first time, the editor imports `assets/sounds` to `Content/Backrooms/Sounds`
   and the Web build's textures to `Content/Backrooms/World` and builds the
   world's materials there (both gitignored; below); the Output Log says
   "imported 12 sounds" and "built the world's materials". `make
   unreal-materials` does the same without opening the editor (the editor's
   Python commandlet), and `make unreal-play` runs it first whenever a
   material is missing or what they are built from changed (the digest in
   `unreal/Saved/materials.digest`), so a pull needs only `make unreal-play`.
   Project Settings > Game > Backrooms > Performance has the frame cap (60),
   the world's resolution (`ScreenPercentage`) and `LightQuality`: High is
   the Web build's lighting, Medium traces hard shadows to 8 m, Low none.
5. Press Play. The default map is the engine's empty Entry map; the game mode
   starts a run on Level 0 at seed 1337 and the sim drives the camera: WASD,
   the mouse, the Web build's keys (F to switch the torch on, R to reload, F3
   for the debug keys). Everything is drawn as the Web build draws it (below).
   The HUD (`ABackroomsHUD`) is the Web build's, laid out by
   shared/port/hud.h and drawn in the engine's medium font. If the world is black, the log says why ("the world is not
   drawn: ...").

For an Xcode project (debugging, or browsing the code), run
`"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="$PWD/unreal/BackroomsGame.uproject" -game`
and open the `.xcworkspace` it writes into `unreal/`.

From the console: `open /Engine/Maps/Entry` opens the title screen, as the
Web build does; `open /Engine/Maps/Entry?level=1?seed=42` starts a run on
Level 1 straight away (any of `?level=`, `?seed=` or `?notitle` skips the
title); `open /Engine/Maps/Entry?mode=free?level=2` flies a free camera over
the Poolrooms (WASD, Space up, Ctrl down, Shift faster).

## How the world is drawn

As the Web build draws it, from the same code where the code is shared and
from a line-for-line port where it is not. Nothing is lit by Unreal: every
material is unlit, and its colour is the Web build's.

| part | Web build | here |
|---|---|---|
| walls, floors, ceilings, props, fixtures, panels, water, glass, decals | `bakeChunkGeometry` (shared/port/chunk_mesh.h) | the same, as procedural mesh sections (`BackroomsChunkBuild`) |
| their colour | `WORLD_FS` (web/src/shaders.cpp): the 3x3 panels, shadows marched over the occupancy grid, the torch and its beam in the air, the flare, the chandelier, fog, the tone curve | `Shaders/BackroomsWorld.ush`, the same arithmetic, in `M_BackroomsWorld` (masked) and `M_BackroomsWorldBlend` (translucent) |
| per-frame uniforms | `frameLight` (shared/port/draw.h) | the same, into `MPC_Backrooms` (`UBackroomsRender::Frame`) |
| shadow grid | `World::buildOccupancy`, 64 cells, recentred every 6 | the same, a transient texture (`UBackroomsRender::UpdateOccupancy`) |
| crates, cans, the deck, coins, balloons, flares, the hunter, the pack, rounds, impacts, mist | `sceneDraws` (shared/port/draw.h), lit by `lightAtCPU` | the same list (`ABackroomsSceneActor`), raylib's shapes tessellated by `shapeTriangles` |
| the revolver | raylib 5.5's glTF animation and CPU skinning | shared/port/revolver.h, bit-identical to raylib 5.5 over the whole reload |
| post pass | `POST_FS` | `Shaders/BackroomsPost.ush` in `M_BackroomsPost`, in place of Unreal's tonemapper |
| the HUD: title and death cards, intro, cards, meters, inventory, hints | `hudDraws` (shared/port/hud.h), measured with raylib's font | the same list (`ABackroomsHUD`), measured and drawn with the engine's medium font; gradients in 48 bands |
| the camcorder (V) | `Camcorder` (shared/port/camcorder.h): zoom, night shot's lamp, its OSD and sounds | the same rules, OSD and sounds (`SoundMixer::camera`) |
| the tape look, in the viewfinder and played back on the death card | `TAPE_FS` over the scene's render target, or over two frames on the reel (`CamcorderReel`: one render texture, a frame grabbed into a cell at 15 Hz) | `Shaders/BackroomsTape.ush` in `M_BackroomsTape`, which takes `M_BackroomsPost`'s place in the post volume while it shows (`UBackroomsRender::ShowPost`); the reel is `UBackroomsReel`, a scene capture at the cell's size copied into its slot. `tools/tape-check.sh` renders the GLSL, the .ush and the material's code on one GPU: no pixel differs |

`make unreal-open` runs `tools/unreal.sh surfaces` before it opens the
editor: `texdump --unreal` writes every texture the Web build binds to
`Saved/Surfaces`, and `world.json`, which says how each is sampled and gives
the core numbers the world shader has written in (its `BR_*` defines). The
editor then runs `backrooms_world.py`, which, when those files or its own
shader glue changed, imports the textures to `/Game/Backrooms/World/Textures`
(the bytes as written: no sRGB, uncompressed, mipmapped and repeating as the
Web build samples them) and builds the materials and `MPC_Backrooms` beside
them.

Things that hold only because of this:

- The post material divides the scene by Unreal's exposure, which the post
  volume pins at 1 (manual, bias 0). Auto exposure, bloom, Lumen and the rest
  are off in Config/DefaultEngine.ini as well.
- raylib blends every draw; Unreal's masked pass cannot. Faces whose alpha the
  shader leaves below 1 (vertex alpha 0.30 to 0.62: glass and water; and the
  scrawl, contact-shadow, water and glass slots) go to translucent sections.
- The shader reads GLSL's `dFdy`, which runs up the window: `BR_DFDY` is
  `-ddy`, or the relief is lit from below.

## Working in the editor

Where things are, and how they look, is decided in code shared with the Web
build, so the maze, and every prop, light and door in it, is the one the Web
build draws.

| to | do |
|---|---|
| see the maze without playing | make a new level, drop in a **Backrooms Preview Actor**, pick Level, Seed, Visit and Storey in its details. It builds the chunks round where it stands, at the height the game puts them, every tube lit and fogged from where it stands, and removes itself when play starts |
| change the controls | make your own Input Actions and a Mapping Context, and assign them in the settings (Input Context, Input Actions by control). Controls you leave empty keep the built-in keys |
| change what a chunk or the player carries | Blueprint subclasses of Backrooms Chunk Actor (set it as the settings' Chunk Class), Backrooms Pawn and Backrooms Player Controller (set them on a Blueprint subclass of the game mode) |
| build a HUD | a UMG widget that calls Get World Subsystem (Backrooms World Subsystem) > Get Hud each tick: health, sanity, ammo, notes, the death card |
| tune | Project Settings > Game > Backrooms: look sensitivity, streaming reach, default seed and level, the revolver's file, sound latency. The light itself is the Web build's numbers (shared/port/levels.cpp): change them there and both builds follow |

## Tests (M1)

Session Frontend > Automation, filter `Backrooms`, or:

```bash
UnrealEditor-Cmd BackroomsGame.uproject -ExecCmds="Automation RunTests Backrooms; Quit" -unattended -nullrhi
```

| test | passes when |
|---|---|
| `Backrooms.Core.Contract` | every line of `tests/golden` matches (docs/migration.md, "Contract tests") |
| `Backrooms.Sim.Replay` | every trace in `tests/traces` replays with every digest matching (shared/sim/trace.h) |
| `Backrooms.Port.Coords` | the conversion scales, swaps y and z, and does not mirror (forward and right land on Unreal's) |
| `Backrooms.Port.ChunkMesh` | every level's origin chunk has geometry, consistent arrays and valid indices |

The contract and the replay run core and the sim as Unreal's compiler and the
Mac's libm build them. What to expect if they fail is in docs/unreal-handoff.md,
"Status".

## Without Unreal

```bash
tools/unreal-check.sh
```

Checks that every shared source has a wrapper, compiles each wrapper alone with
the module's settings (C++20, no exceptions, no RTTI, warnings as errors), links
them with clang and FMA enabled but without `-ffp-contract=off` and runs the
contract and the traces, and parses the `.uproject` and `.uplugin`. The Unreal
classes themselves need the engine.
