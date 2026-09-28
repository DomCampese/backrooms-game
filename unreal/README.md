# The Backrooms on Unreal Engine 5

The port described in docs/unreal-handoff.md. The raylib build in the rest of
this repository stays the reference until the port reaches M5; nothing here
replaces it, and nothing in the raylib build depends on this folder.

Nothing in this folder has been compiled by Unreal yet: it was written where no
engine was available. `tools/unreal-check.sh` checks what can be checked without
one (below). Expect the first build on a Mac to need small fixes to the Unreal
side; the shared code compiles as it does for the raylib build.

## Layout

| path | what |
|---|---|
| `BackroomsGame.uproject` | the project; its only module is a stub, the game lives in the plugin |
| `Plugins/Backrooms/` | one Runtime module, `Backrooms` |
| `.../Private/Shared/*.cpp` | one file per source in `src/core`, `src/sim`, `src/port` and `tools/contract_lib.cpp`, each a single `#include`: the module compiles the repository's files, not copies |
| `.../Public/BackroomsCoords.h` | the one conversion between core (metres, y up) and Unreal (centimetres, z up) |
| `.../BackroomsWorldSubsystem` | owns the `Sim` (and core's `World` in it); steps a run, streams chunk actors round the player, `GetHud` for a HUD |
| `.../BackroomsPlayerController` | the sim's controls as Enhanced Input actions; fills an `InputFrame` each frame |
| `.../BackroomsPawn` | the camera, placed where the sim's view is (src/port/view.h) |
| `.../BackroomsChunkActor`, `BackroomsChunkBuild` | a chunk: the greybox drawn with the level's look, plus prop and fitting meshes |
| `.../BackroomsLevelLook` | the data asset for a level's look: materials, prop meshes, fitting mesh |
| `.../BackroomsSettings` | Project Settings > Game > Backrooms |
| `.../BackroomsPreviewActor` | the generated maze in the editor viewport |
| `.../BackroomsTypes` | Blueprint mirrors of core's surfaces, props and controls; the HUD snapshot |
| `.../BackroomsGameMode` | starts a run (or a free camera with `?mode=free`) from the map's options |
| `.../Private/Tests/BackroomsTests.cpp` | automation tests: contract, trace replay, coordinates, greybox |

Why one module, not the core and sim modules the plan first described: core has
no export annotations, and editor builds link each module as a shared library
with hidden symbols, so a second module could not call core. Why no PCH: a
shared PCH is force-included into every file of the module, core's included,
and core must see nothing but itself and the standard library.

A new `.cpp` in `src/core`, `src/sim` or `src/port` needs a wrapper in
`Private/Shared`. `tools/unreal-check.sh` fails until it has one.

## Opening it

The project compiles files from the repository's `src/`, so check out the whole
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
4. Open the project: double-click `unreal/BackroomsGame.uproject`, or in the
   Launcher, Unreal Engine > Library > Launch 5.8 > Browse, and pick it. If the
   editor asks to rebuild missing modules, say yes (step 3 already did).
5. Press Play. The default map is the engine's empty Entry map; the game mode
   starts a run on Level 0 at seed 1337 and the sim drives the camera: WASD,
   the mouse, the raylib build's keys (F3 for the debug keys). No level has a
   look yet, so the maze is the greybox's flat colours, and the pickups,
   crates, balloons, the hunter and the pack are plain shapes at their real
   size (`ABackroomsSceneActor`). A text HUD (`ABackroomsHUD`) shows the meters,
   inventory, notes and the death card.

For an Xcode project (debugging, or browsing the code), run
`"/Users/Shared/Epic Games/UE_5.8/Engine/Build/BatchFiles/Mac/GenerateProjectFiles.sh" -project="$PWD/unreal/BackroomsGame.uproject" -game`
and open the `.xcworkspace` it writes into `unreal/`.

From the console: `open /Engine/Maps/Entry?level=1?seed=42` starts a run on
Level 1; `open /Engine/Maps/Entry?mode=free?level=2` flies a free camera over
the Poolrooms' greybox (WASD, Space up, Ctrl down, Shift faster).

## Working in the editor

Where things are is decided in code (core and the sim), so the maze, and every
prop, light and door in it, is the same one the raylib build generates. How
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
| tune | Project Settings > Game > Backrooms: look sensitivity, streaming reach, default seed and level, the camera light |

## Tests (M1)

Session Frontend > Automation, filter `Backrooms`, or:

```bash
UnrealEditor-Cmd BackroomsGame.uproject -ExecCmds="Automation RunTests Backrooms; Quit" -unattended -nullrhi
```

| test | passes when |
|---|---|
| `Backrooms.Core.Contract` | every line of `tests/golden` matches (docs/migration.md, "Contract tests") |
| `Backrooms.Sim.Replay` | every trace in `tests/traces` replays with every digest matching (src/sim/trace.h) |
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
