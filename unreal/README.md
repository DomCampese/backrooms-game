# The Backrooms on Unreal Engine 5

The port described in docs/unreal-handoff.md. The Web build in the rest of
this repository stays the reference until the port reaches M5; nothing here
replaces it, and nothing in the Web build depends on this folder.

It builds and runs on an M4 Mac with Unreal Engine 5.8.3 (29 September 2026):
a run on Level 0 streams the greybox, the sim moves the camera, the text HUD
draws, and the revolver is in your hand. It was written where no engine was
available, so each Mac build so far has turned up a fix or two on the Unreal
side; the shared code compiled first time. `tools/unreal-check.sh` checks what
can be checked without Unreal (below). docs/unreal-handoff.md, "Status", has
what is open.

The short version, on the Mac, from the repository's root:

```bash
make unreal-open    # build, open the editor (first time: imports the revolver)
make unreal-play    # build, run the game in a window
make unreal-test    # build, run the Backrooms automation tests
tools/unreal.sh log # the "Backrooms:" lines from the last run
```

## Layout

| path | what |
|---|---|
| `BackroomsGame.uproject` | the project; its only module is a stub, the game lives in the plugin |
| `Plugins/Backrooms/` | one Runtime module, `Backrooms` |
| `.../Private/Shared/*.cpp` | one file per source in `shared/core`, `shared/sim`, `shared/port` and `tools/contract_lib.cpp`, each a single `#include`: the module compiles the repository's files, not copies |
| `.../Public/BackroomsCoords.h` | the one conversion between core (metres, y up) and Unreal (centimetres, z up) |
| `.../BackroomsWorldSubsystem` | owns the `Sim` (and core's `World` in it); steps a run, streams chunk actors round the player, `GetHud` for a HUD |
| `.../BackroomsPlayerController` | the sim's controls as Enhanced Input actions; fills an `InputFrame` each frame |
| `.../BackroomsPawn` | the camera, placed where the sim's view is (shared/port/view.h) |
| `.../BackroomsChunkActor`, `BackroomsChunkBuild` | a chunk: the greybox drawn with the level's look, plus prop and fitting meshes |
| `.../BackroomsLevelLook` | the data asset for a level's look: materials, prop meshes, fitting mesh, the tubes' colour and output, fog |
| `.../BackroomsLightsActor` | rect lights at the live fittings nearest the player (dead ones dark, faulty ones stuttering, all out in a blackout), the level's fog, and a post-process volume: fixed exposure, the colour split |
| `.../BackroomsSpriteActor` | the hunter and the pack as billboards from the game's sprite sheets, frames and rows from `shared/port/sprites.h` |
| `.../Private/BackroomsFixtureShapes` | where a `chunkLayout` fixture stands, and its plain box until a look gives the kind a mesh |
| `Plugins/Backrooms/Content/Python` | run by the editor on open: imports the revolver and the recorded sounds, and builds the level looks from the game's surfaces (`backrooms_looks.py`) |
| `.../BackroomsSettings` | Project Settings > Game > Backrooms |
| `.../BackroomsPreviewActor` | the generated maze in the editor viewport |
| `.../BackroomsSound` | the sim's sound: the game's own mix (shared/port/mixer.h) streamed through a procedural wave, the imported recordings on components of their own |
| `.../BackroomsTypes` | Blueprint mirrors of core's surfaces, props and controls; the HUD snapshot |
| `.../BackroomsGameMode` | starts a run (or a free camera with `?mode=free`) from the map's options |
| `.../Private/Tests/BackroomsTests.cpp` | automation tests: contract, trace replay, coordinates, greybox |

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
   first time, the editor imports `assets/models/revolver.glb` to
   `Content/Backrooms/Revolver` and `assets/sounds` to `Content/Backrooms/Sounds`
   (both gitignored); the Output Log says "Backrooms: imported the revolver" and
   "imported 12 sounds". It also builds the five level looks (below), which
   says "built 5 level looks". `make unreal-play` cannot import, since the game
   has no editor, and warns if this has not happened yet.
5. Press Play. The default map is the engine's empty Entry map; the game mode
   starts a run on Level 0 at seed 1337 and the sim drives the camera: WASD,
   the mouse, the Web build's keys (F3 for the debug keys). Floors, walls
   and ceilings wear the game's own surfaces, lit by the fittings; fixtures
   are plain boxes and the rest of the greybox keeps its flat colours; the
   hunter and the pack are the game's own sprites (`ABackroomsSpriteActor`),
   and the pickups, crates and balloons plain shapes at their real size
   (`ABackroomsSceneActor`). A text HUD (`ABackroomsHUD`) shows the meters,
   inventory, notes and the death card. The revolver is in your hand
   (`ABackroomsHeldActor`): the editor imports `assets/models/revolver.glb` the
   first time it opens the project (the Output Log says "imported the
   revolver"), and a game started before that shows no gun.

For an Xcode project (debugging, or browsing the code), run
`"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="$PWD/unreal/BackroomsGame.uproject" -game`
and open the `.xcworkspace` it writes into `unreal/`.

From the console: `open /Engine/Maps/Entry?level=1?seed=42` starts a run on
Level 1; `open /Engine/Maps/Entry?mode=free?level=2` flies a free camera over
the Poolrooms' greybox (WASD, Space up, Ctrl down, Shift faster).

## The level looks

`make unreal-open` runs `tools/unreal.sh surfaces` before it opens the editor:
`texdump --unreal` writes every world surface the Web build generates
(`web/src/surfaces.cpp`) to `Saved/Surfaces`, each as a colour PNG and a detail PNG
(slopes in red and green, gloss in blue), and `looks.json`, which names each
level's floor, ceiling and wall surface and gives its tile sizes, gloss, tube
colour and output and fog, from `LEVEL_SURFACES` and `LEVELS`. The editor then
runs `backrooms_looks.py`, which, when those files changed:

- imports the PNGs to `/Game/Backrooms/Surfaces`, the detail maps linear and
  uncompressed;
- builds `M_BackroomsSurface`: the greybox's texture coordinates are metres
  (shared/port/greybox.h), so `TileU`/`TileV` are the metres one repeat covers
  (2 m on floors and ceilings, 3 m across walls, 3 m or Level 1's 4.2 m down);
  the detail map's slopes make the normal, scaled by `ReliefU` and `ReliefV`
  (across and down the texture), and its gloss
  mask picks the roughness between `RoughMatte` and `RoughGloss` as far as
  `Shine` (from the level's gloss) allows;
- adds the level's ambient floor (`LevelCfg::amb`, times `AMBIENT_GAIN`) as
  emissive, `Ambient` on each instance, so an unlit corner keeps a little of
  its colour;
- imports the actors' sheets to `/Game/Backrooms/Sprites` with
  `M_BackroomsSprite` (lit, translucent) and `M_BackroomsSpriteGlow` (unlit,
  the Smiler's eyes and grin), which the settings name by default;
- makes an instance per level and surface (`MI_L0_Floor`, ...) and the looks
  `DA_Level0` to `DA_Level4` in `/Game/Backrooms/Looks`, which the settings
  name by default. Floors and stairs take the floor, walls, steps and pillars
  the walls. A material you assign to a look in the editor is kept; the
  light and fog are rewritten from `web/src/levels.cpp`.

The tangents' handedness is worked out in code (`Tangent` in
BackroomsChunkBuild.cpp) and has not been seen on the Mac yet. If horizontal
relief looks lit from below (the skirting's bullnose dark on top, grout lines
across the pool tile standing proud), set `ReliefV` to -1 on
`M_BackroomsSurface`; for vertical relief, `ReliefU`. Then fix the sign in
`Tangent` and set it back.
`BACKROOMS_TEXTURED=1 ./greybox-view` (tools/greybox-view.cpp) draws the same
mapping with raylib, to hold against a capture of the game.

## Working in the editor

Where things are is decided in code (core and the sim), so the maze, and every
prop, light and door in it, is the same one the Web build generates. How
they look is set in the editor, and code has a default for everything, so the
project runs before any of it exists.

| to | do |
|---|---|
| see the maze without playing | make a new level, drop in a **Backrooms Preview Actor**, pick Level, Seed, Visit and Storey in its details. It builds the chunks round where it stands, at the height the game puts them, and removes itself when play starts |
| style a level | Content Browser > Miscellaneous > Data Asset > **Backrooms Level Look**. Give surfaces materials (floor, walls, ceiling, stairs, ...), prop kinds static meshes, and the light fittings a mesh. Assign it in Project Settings > Game > Backrooms > Level Looks at the level's index. Empty entries keep the greybox. Set it on a preview actor's Look to try it without assigning it |
| place a prop mesh | its origin is the middle of the prop's footprint on the floor; Offset moves and turns it from there. A prop with a mesh loses its greybox box |
| change the controls | make your own Input Actions and a Mapping Context, and assign them in the settings (Input Context, Input Actions by control). Controls you leave empty keep the built-in keys |
| change what a chunk or the player carries | Blueprint subclasses of Backrooms Chunk Actor (set it as the settings' Chunk Class), Backrooms Pawn and Backrooms Player Controller (set them on a Blueprint subclass of the game mode) |
| build a HUD | a UMG widget that calls Get World Subsystem (Backrooms World Subsystem) > Get Hud each tick: health, sanity, ammo, notes, the death card |
| give a fixture a mesh | a look's Fixtures, by kind. Its origin is the layout's point on the wall face or ceiling, X out of the face; a run (conduit, pipe, valve, streamer) has X along it, scaled so a mesh 100 units long spans it |
| tune | Project Settings > Game > Backrooms: look sensitivity, streaming reach, default seed and level, how many fittings get lights (`FittingLights`, `FittingShadows`), their output (`FittingLumens`) and reach, the camera light, the sprite sheets and materials. A look's `ExposureEV100` sets its brightness (lower is brighter) |

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
| `Backrooms.Port.Greybox` | every level's origin chunk has geometry and valid indices |

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
